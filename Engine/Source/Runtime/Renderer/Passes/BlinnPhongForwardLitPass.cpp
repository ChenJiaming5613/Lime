#include "Renderer/Passes/BlinnPhongForwardLitPass.h"

#include "Core/Logging/LogManager.h"
#include "Renderer/Passes/ShadowCasterPass.h"
#include "Renderer/RenderPassRegistry.h"
#include "Renderer/Renderer.h"

#include "Camera/Camera.h"
#include "Scene/Scene.h"

#include <nvrhi/utils.h>

#include <array>
#include <vector>

namespace Lime
{
	namespace
	{
		// The importer produces its own vertex struct so that LimeAsset needs no renderer dependency. The two
		// must stay byte compatible, because mesh data is uploaded straight from the imported array with no
		// conversion pass. Checked here, which is where the imported layout and the shader input meet.
		static_assert(sizeof(FMeshVertex) == sizeof(FStaticMeshVertex),
		              "FMeshVertex and FStaticMeshVertex must match so imported data can be uploaded directly");
		static_assert(offsetof(FMeshVertex, Position) == offsetof(FStaticMeshVertex, Position), "Vertex position offset mismatch");
		static_assert(offsetof(FMeshVertex, Normal) == offsetof(FStaticMeshVertex, Normal), "Vertex normal offset mismatch");
		static_assert(offsetof(FMeshVertex, TexCoord) == offsetof(FStaticMeshVertex, TexCoord), "Vertex texcoord offset mismatch");

		// Must match FSceneFrameConstants in BlinnPhong.hlsl, including the padding: HLSL constant buffers
		// pack on 16 byte boundaries, so a float3 followed by a float shares one register.
		struct FFrameConstants
		{
			FMatrix4x4 ViewProjection;
			FVector3 CameraPosition;
			float CameraPadding = 0.0f;
			FVector3 LightDirection;
			float LightIntensity = 0.0f;
			FVector3 LightColor;
			float LightColorPadding = 0.0f;
			FVector3 AmbientColor;
			float AmbientPadding = 0.0f;
			FMatrix4x4 LightViewProjection = FMatrix4x4::Identity();
			// 0 disables the lookup in the shader, which is how an unconnected shadow map is handled.
			float ShadowStrength = 0.0f;
			float ShadowPadding[3] = { 0.0f, 0.0f, 0.0f };
		};

		// Must match FSceneDrawConstants in BlinnPhong.hlsl.
		struct FDrawConstants
		{
			FMatrix4x4 World;
			FMatrix4x4 NormalMatrix;
			FVector4 BaseColorFactor;
			float AlphaCutoff = 0.0f;
			float SpecularPower = 32.0f;
			float DrawPadding[2] = { 0.0f, 0.0f };
		};

		// Stand-in for a primitive that references no material, so drawing needs no special case.
		//
		// A function rather than a namespace scope object: FMaterialData holds a std::string, and a static
		// with a throwing constructor cannot have that exception caught. The local static is initialized on
		// first use and costs one guard check.
		const FMaterialData& GetDefaultMaterial()
		{
			static const FMaterialData Default;
			return Default;
		}
	} // namespace

	void FBlinnPhongForwardLitPass::Reflect(FRenderGraphPassTypeDesc& OutType) const
	{
		OutType.Description = "Shades the scene with Blinn-Phong lighting, optionally sampling a shadow map.";

		// Optional, so the pass compiles into a graph with no shadow caster and simply draws unshadowed.
		// Making it required would mean every graph had to include a caster to render at all.
		FRenderGraphResourceDesc ShadowDepth =
		    MakeTextureResource("shadowDepth", ERenderGraphResourceVisibility::Input, nvrhi::Format::D32);
		ShadowDepth.bOptional = true;
		ShadowDepth.Description = "Depth from the light's point of view. Unconnected means no shadows.";
		OutType.Inputs.push_back(std::move(ShadowDepth));

		// Left at the graph's size and format: this pass draws whatever it is given, so pinning either
		// would stop it being reused between the viewport and a smaller offscreen target.
		FRenderGraphResourceDesc Colour = MakeTextureResource("color", ERenderGraphResourceVisibility::Output);
		Colour.Description = "Shaded scene colour.";
		OutType.Outputs.push_back(std::move(Colour));

		// Written as well as the colour, because the depth test needs somewhere to write and a later pass
		// may want to read it. The format is pinned: it has to match what the pipeline is compiled against.
		FRenderGraphResourceDesc Depth = MakeTextureResource("depth", ERenderGraphResourceVisibility::Output, nvrhi::Format::D32);
		Depth.Description = "Scene depth produced while shading.";
		OutType.Outputs.push_back(std::move(Depth));
	}

	bool FBlinnPhongForwardLitPass::Initialize(FRenderer& Renderer)
	{
		Device = Renderer.GetDevice();
		if (Device == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "FBlinnPhongForwardLitPass requires a device");
			return false;
		}

		FShaderLibrary& Shaders = Renderer.GetShaderLibrary();
		VertexShader = Shaders.GetShader("Passes/BlinnPhong.hlsl", "MainVS", nvrhi::ShaderType::Vertex);
		PixelShader = Shaders.GetShader("Passes/BlinnPhong.hlsl", "MainPS", nvrhi::ShaderType::Pixel);
		if (VertexShader == nullptr || PixelShader == nullptr)
		{
			return false;
		}

		// Layout must match FStaticMeshVertex, which the static_asserts above tie to the imported data.
		const std::array<nvrhi::VertexAttributeDesc, 3> Attributes = { {
			nvrhi::VertexAttributeDesc()
			    .setName("POSITION")
			    .setFormat(nvrhi::Format::RGB32_FLOAT)
			    .setOffset(offsetof(FStaticMeshVertex, Position))
			    .setElementStride(sizeof(FStaticMeshVertex)),
			nvrhi::VertexAttributeDesc()
			    .setName("NORMAL")
			    .setFormat(nvrhi::Format::RGB32_FLOAT)
			    .setOffset(offsetof(FStaticMeshVertex, Normal))
			    .setElementStride(sizeof(FStaticMeshVertex)),
			nvrhi::VertexAttributeDesc()
			    .setName("TEXCOORD")
			    .setFormat(nvrhi::Format::RG32_FLOAT)
			    .setOffset(offsetof(FStaticMeshVertex, TexCoord))
			    .setElementStride(sizeof(FStaticMeshVertex)),
		} };

		InputLayout = Device->createInputLayout(Attributes.data(), static_cast<uint32_t>(Attributes.size()), VertexShader);
		if (InputLayout == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createInputLayout failed for the Blinn-Phong pass");
			return false;
		}

		FrameConstantBuffer =
		    Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(FFrameConstants), "SceneFrameConstants", 16));
		DrawConstantBuffer =
		    Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(FDrawConstants), "SceneDrawConstants", 256));
		if (FrameConstantBuffer == nullptr || DrawConstantBuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Failed to create the Blinn-Phong constant buffers");
			return false;
		}

		// Built through the helper so the Vulkan binding offsets are applied and one layout works on both
		// backends.
		//
		// The shadow map and its comparison sampler are always in the layout, because a shader cannot declare
		// a resource conditionally. When no caster is connected the pass binds a 1x1 stand-in and tells the
		// shader to skip the lookup.
		const nvrhi::BindingLayoutDesc LayoutDesc = MakeBindingLayoutDesc(nvrhi::ShaderType::All)
		                                                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
		                                                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(1))
		                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
		                                                .addItem(nvrhi::BindingLayoutItem::Sampler(0))
		                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
		                                                .addItem(nvrhi::BindingLayoutItem::Sampler(1));

		BindingLayout = Device->createBindingLayout(LayoutDesc);
		if (BindingLayout == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createBindingLayout failed for the Blinn-Phong pass");
			return false;
		}

		// Comparison filtering, which is what makes SampleCmpLevelZero average the depth test results rather
		// than the depths. Averaging depths first and then comparing would give one hard edge instead of a
		// soft one.
		//
		// The comparison itself is chosen by the shader's SampleCmp call, so the sampler only has to be marked
		// as a comparison sampler; nvrhi's SamplerDesc carries no comparison function of its own.
		//
		// Clamped so a lookup just outside the map reads its edge rather than wrapping to the far side, which
		// would put a stripe of shadow along the opposite boundary.
		const nvrhi::SamplerDesc ShadowSamplerDesc = nvrhi::SamplerDesc()
		                                                 .setAllFilters(true)
		                                                 .setAllAddressModes(nvrhi::SamplerAddressMode::ClampToEdge)
		                                                 .setReductionType(nvrhi::SamplerReductionType::Comparison);
		ShadowSampler = Device->createSampler(ShadowSamplerDesc);
		if (ShadowSampler == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createSampler failed for the Blinn-Phong shadow sampler");
			return false;
		}

		// A 1x1 depth texture standing in for an unconnected shadow map.
		//
		// Needed because the binding layout always includes the shadow slot: a shader cannot declare a
		// resource conditionally, and leaving the binding empty is invalid. Its contents never matter, since
		// the shader is told to skip the lookup, but it has to be a real depth texture so the descriptor
		// matches what the layout expects.
		const nvrhi::TextureDesc FallbackDesc = nvrhi::TextureDesc()
		                                            .setDimension(nvrhi::TextureDimension::Texture2D)
		                                            .setWidth(1)
		                                            .setHeight(1)
		                                            .setFormat(nvrhi::Format::D32)
		                                            .setIsRenderTarget(true)
		                                            // Typeless, for the same reason the viewport's depth target
		                                            // is: D3D12 needs the typeless form to build both a depth
		                                            // view and a shader resource view over one texture.
		                                            .setIsTypeless(true)
		                                            .setInitialState(nvrhi::ResourceStates::ShaderResource)
		                                            .setKeepInitialState(true)
		                                            .setDebugName("BlinnPhongFallbackShadow");
		FallbackShadowTexture = Device->createTexture(FallbackDesc);
		if (FallbackShadowTexture == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Failed to create the Blinn-Phong fallback shadow texture");
			return false;
		}

		return true;
	}

	void FBlinnPhongForwardLitPass::Shutdown()
	{
		// Before the layout and the resources they reference, so nothing outlives what it points at.
		MaterialBindingSets.clear();
		CachedSceneRevision = 0;

		GpuResources.Release();
		Pipeline = nullptr;
		BindingLayout = nullptr;
		DrawConstantBuffer = nullptr;
		FrameConstantBuffer = nullptr;
		InputLayout = nullptr;
		PixelShader = nullptr;
		VertexShader = nullptr;
		CurrentFramebuffer = nullptr;
		ShadowTexture = nullptr;
		FallbackShadowTexture = nullptr;
		ShadowSampler = nullptr;
		ShadowStrength = 0.0f;
		Device = nullptr;
	}

	bool FBlinnPhongForwardLitPass::Compile(FRenderer& Renderer, const FRenderGraphPassResources& Resources)
	{
		LIME_UNUSED(Renderer);

		nvrhi::IFramebuffer* Framebuffer = Resources.GetFramebuffer();
		if (Framebuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "The Blinn-Phong pass has no framebuffer, so its colour output is not connected");
			return false;
		}

		// Null when the graph has no shadow caster feeding this pass, which the reflection allows. The
		// fallback texture is bound in its place and the shader skips the lookup.
		nvrhi::ITexture* NewShadowTexture = Resources.FindTexture("shadowDepth");
		ShadowStrength = NewShadowTexture != nullptr ? 1.0f : 0.0f;

		if (NewShadowTexture != ShadowTexture)
		{
			ShadowTexture = NewShadowTexture;
			// The material sets name the shadow texture, so they describe the old one now. Dropping them here
			// rather than checking per draw keeps the hot path free of the comparison.
			MaterialBindingSets.clear();
		}

		return CreatePipeline(Framebuffer);
	}

	bool FBlinnPhongForwardLitPass::CreatePipeline(nvrhi::IFramebuffer* Framebuffer)
	{
		if (Framebuffer == nullptr)
		{
			return false;
		}

		nvrhi::RenderState RenderState;
		// Depth testing is the whole point of having a depth buffer: without it, triangles would be shaded in
		// submission order and near surfaces would be overwritten by far ones.
		RenderState.depthStencilState.depthTestEnable = true;
		RenderState.depthStencilState.depthWriteEnable = true;
		// LessOrEqual, not Less: the depth range is [0, 1] with 1 at the far plane, so nearer fragments have
		// smaller values.
		RenderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
		RenderState.depthStencilState.stencilEnable = false;

		// Culling disabled rather than set to back faces. glTF winding varies with the sign of the node
		// scale, and a negative scale flips it: culling would make those meshes vanish. The pixel shader
		// flips the normal towards the viewer instead, so back faces still shade correctly.
		RenderState.rasterState.setCullNone();

		const nvrhi::GraphicsPipelineDesc PipelineDesc = nvrhi::GraphicsPipelineDesc()
		                                                     .setPrimType(nvrhi::PrimitiveType::TriangleList)
		                                                     .setInputLayout(InputLayout)
		                                                     .setVertexShader(VertexShader)
		                                                     .setPixelShader(PixelShader)
		                                                     .addBindingLayout(BindingLayout)
		                                                     .setRenderState(RenderState);

		Pipeline = Device->createGraphicsPipeline(PipelineDesc, Framebuffer);
		if (Pipeline == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createGraphicsPipeline failed for the Blinn-Phong pass");
			return false;
		}

		CurrentFramebuffer = Framebuffer;
		return true;
	}

	void FBlinnPhongForwardLitPass::OnFramebufferChanged(nvrhi::IFramebuffer* Framebuffer)
	{
		// Dropped rather than rebuilt here: the pass may be notified while no command list is open, and the
		// next Render recreates it against whatever framebuffer is current then.
		Pipeline = nullptr;
		CurrentFramebuffer = nullptr;
		LIME_UNUSED(Framebuffer);
	}

	nvrhi::IBindingSet* FBlinnPhongForwardLitPass::GetOrCreateBindingSet(int32 MaterialIndex)
	{
		// -1 means the default material and maps to slot zero; a real index maps to itself plus one. The
		// addition happens before widening so a negative value cannot wrap into an enormous slot number.
		const SizeType Slot = MaterialIndex < 0 ? 0 : static_cast<SizeType>(MaterialIndex) + 1;
		if (Slot >= MaterialBindingSets.size())
		{
			MaterialBindingSets.resize(Slot + 1);
		}

		if (MaterialBindingSets[Slot] != nullptr)
		{
			return MaterialBindingSets[Slot].Get();
		}

		// The draw constant buffer is volatile and rewritten before every draw, so it is bound here by handle
		// and its contents are not part of what makes a set reusable.
		const nvrhi::BindingSetDesc Desc =
		    nvrhi::BindingSetDesc()
		        .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, FrameConstantBuffer))
		        .addItem(nvrhi::BindingSetItem::ConstantBuffer(1, DrawConstantBuffer))
		        .addItem(nvrhi::BindingSetItem::Texture_SRV(0, GpuResources.GetBaseColorTexture(MaterialIndex)))
		        .addItem(nvrhi::BindingSetItem::Sampler(0, GpuResources.GetSampler()))
		        // The graph's shadow map when one is connected, otherwise the stand-in. Either way the binding
		        // is present, because the layout requires it.
		        .addItem(nvrhi::BindingSetItem::Texture_SRV(1, ShadowTexture != nullptr ? ShadowTexture : FallbackShadowTexture.Get()))
		        .addItem(nvrhi::BindingSetItem::Sampler(1, ShadowSampler));

		MaterialBindingSets[Slot] = Device->createBindingSet(Desc, BindingLayout);
		return MaterialBindingSets[Slot].Get();
	}

	void FBlinnPhongForwardLitPass::OnBeginFrame(FRenderer& Renderer, const FFrameContext& Context)
	{
		LIME_UNUSED(Context);

		// Taken from the caster rather than recomputed here. Both would have to fit the same frustum to the
		// same bounds, and any difference between them would show up as shadows landing in the wrong place.
		if (const FShadowCasterPass* Caster = Renderer.FindPass<FShadowCasterPass>())
		{
			ShadowViewProjection = Caster->GetLightViewProjection();
		}
	}

	void FBlinnPhongForwardLitPass::Render(const FFrameContext& Context)
	{
		// Silent when there is nothing to draw. A project without a scene is a normal configuration, and
		// logging here would flood the log at frame rate.
		if (!Settings.bEnabled || Context.Scene == nullptr || Context.Camera == nullptr || Context.CommandList == nullptr)
		{
			return;
		}

		const FScene& Scene = *Context.Scene;
		if (Scene.GetMeshes().empty())
		{
			return;
		}

		if (!GpuResources.EnsureUploaded(Device, Context.CommandList, Scene))
		{
			return;
		}

		// Dropped when the scene changes: the cached sets reference that scene's textures, which
		// EnsureUploaded has just released.
		if (CachedSceneRevision != Scene.GetAssetRevision())
		{
			MaterialBindingSets.clear();
			CachedSceneRevision = Scene.GetAssetRevision();
		}

		// The graph's target when one is driving this pass, otherwise whatever the renderer set up. Compile
		// already built the pipeline for the graph's framebuffer, so this only rebuilds outside a graph.
		nvrhi::IFramebuffer* Framebuffer = Context.Resources != nullptr ? Context.Resources->GetFramebuffer() : Context.Framebuffer;
		if (Framebuffer == nullptr)
		{
			return;
		}

		if (Pipeline == nullptr || CurrentFramebuffer != Framebuffer)
		{
			if (!CreatePipeline(Framebuffer))
			{
				return;
			}
		}

		// One directional light is supported; the first one found wins. More would need the constant buffer
		// to carry an array, which is beyond what this pass is for.
		FVector3 LightDirection{ 0.0f, -1.0f, 0.0f };
		FVector3 LightColor = FVector3::One();
		for (const auto [Entity, Light] : Scene.GetRegistry().view<const FDirectionalLightComponent>().each())
		{
			LightDirection = Light.Direction;
			LightColor = Light.Color * Light.Intensity;
			break;
		}

		FFrameConstants FrameConstants;
		FrameConstants.ViewProjection = Context.Camera->GetViewProjectionMatrix();
		FrameConstants.CameraPosition = Context.Camera->GetPosition();
		FrameConstants.LightDirection = LightDirection;
		FrameConstants.LightIntensity = Settings.LightIntensity;
		FrameConstants.LightColor = LightColor;
		FrameConstants.AmbientColor = FVector3::One() * Settings.AmbientStrength;
		FrameConstants.LightViewProjection = ShadowViewProjection;
		FrameConstants.ShadowStrength = ShadowStrength;
		Context.CommandList->writeBuffer(FrameConstantBuffer, &FrameConstants, sizeof(FrameConstants));

		const nvrhi::ViewportState ViewportState = nvrhi::ViewportState().addViewportAndScissorRect(
		    nvrhi::Viewport(static_cast<float>(Context.ViewportWidth), static_cast<float>(Context.ViewportHeight)));

		const std::vector<FMaterialData>& Materials = Scene.GetMaterials();

		for (const auto [Entity, NodeComponent, MeshRenderer, Transform] :
		     Scene.GetRegistry().view<const FNodeComponent, const FMeshRendererComponent, const FTransformComponent>().each())
		{
			if (!NodeComponent.Enabled || !MeshRenderer.bVisible)
			{
				continue;
			}

			const FMeshGpuData* MeshGpu = GpuResources.GetMesh(MeshRenderer.MeshIndex);
			if (MeshGpu == nullptr || MeshRenderer.MeshIndex >= Scene.GetMeshes().size())
			{
				continue;
			}

			const FMeshData& Mesh = Scene.GetMeshes()[MeshRenderer.MeshIndex];

			// Sections are drawn in order rather than sorted by material. Each already corresponds to one
			// material, and glTF authors them grouped, so sorting would add a pass over the data for no
			// reduction in binding changes.
			for (const FMeshSection& Section : Mesh.Sections)
			{
				const FMaterialData& Material =
				    Section.MaterialIndex >= 0 && static_cast<SizeType>(Section.MaterialIndex) < Materials.size()
				        ? Materials[static_cast<SizeType>(Section.MaterialIndex)]
				        : GetDefaultMaterial();

				FDrawConstants DrawConstants;
				DrawConstants.World = Transform.LocalToWorldMatrix;
				DrawConstants.NormalMatrix = Transform.NormalMatrix;
				DrawConstants.BaseColorFactor = Material.BaseColorFactor;
				DrawConstants.AlphaCutoff = Material.AlphaCutoff;
				DrawConstants.SpecularPower = Settings.SpecularPower;
				Context.CommandList->writeBuffer(DrawConstantBuffer, &DrawConstants, sizeof(DrawConstants));

				// Cached by material: createBindingSet allocates descriptors on every call with no caching of
				// its own, and the D3D12 sampler heap holds at most 2048. Building one per draw exhausts it in
				// a single frame on a scene of any size.
				nvrhi::IBindingSet* BindingSet = GetOrCreateBindingSet(Section.MaterialIndex);
				if (BindingSet == nullptr)
				{
					continue;
				}

				nvrhi::GraphicsState GraphicsState;
				GraphicsState.pipeline = Pipeline;
				GraphicsState.framebuffer = Framebuffer;
				GraphicsState.viewport = ViewportState;
				GraphicsState.addBindingSet(BindingSet);
				GraphicsState.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(MeshGpu->VertexBuffer).setSlot(0));
				GraphicsState.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(MeshGpu->IndexBuffer).setFormat(nvrhi::Format::R32_UINT);

				Context.CommandList->setGraphicsState(GraphicsState);

				nvrhi::DrawArguments DrawArguments;
				DrawArguments.vertexCount = Section.IndexCount;
				DrawArguments.startIndexLocation = Section.FirstIndex;
				Context.CommandList->drawIndexed(DrawArguments);
			}
		}
	}
} // namespace Lime
