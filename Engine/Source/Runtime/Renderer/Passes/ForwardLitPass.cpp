#include "Renderer/Passes/ForwardLitPass.h"

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
		// conversion pass. Checked here as well as in the Blinn-Phong pass, since both build an input layout
		// from these offsets and either could be the one edited.
		static_assert(sizeof(FMeshVertex) == sizeof(FStaticMeshVertex),
		              "FMeshVertex and FStaticMeshVertex must match so imported data can be uploaded directly");
		static_assert(offsetof(FMeshVertex, Position) == offsetof(FStaticMeshVertex, Position), "Vertex position offset mismatch");
		static_assert(offsetof(FMeshVertex, Normal) == offsetof(FStaticMeshVertex, Normal), "Vertex normal offset mismatch");
		static_assert(offsetof(FMeshVertex, Tangent) == offsetof(FStaticMeshVertex, Tangent), "Vertex tangent offset mismatch");
		static_assert(offsetof(FMeshVertex, TexCoord) == offsetof(FStaticMeshVertex, TexCoord), "Vertex texcoord offset mismatch");

		// Must match FForwardFrameConstants in ForwardLit.hlsl, including the padding: HLSL constant buffers
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

		// Must match FForwardDrawConstants in ForwardLit.hlsl.
		//
		// The field order is not arbitrary: EmissiveFactor is a float3 and AlphaCutoff the float that shares
		// its 16 byte register, so the two have to stay adjacent and in that order. Reordering them here
		// without reordering the shader would silently read the cutoff out of the emissive colour's blue
		// channel.
		struct FDrawConstants
		{
			FMatrix4x4 World;
			FMatrix4x4 NormalMatrix;
			FVector4 BaseColorFactor;
			FVector3 EmissiveFactor;
			float AlphaCutoff = 0.0f;
			float MetallicFactor = 1.0f;
			float RoughnessFactor = 1.0f;
			// Zero means "leave the vertex normal alone", which is how both an absent map and the disabled
			// setting are expressed. The shader then needs no branch and no second pipeline.
			float NormalScale = 0.0f;
			// Zero means "ignore the occlusion map", expressed the same way.
			float OcclusionStrength = 0.0f;
		};

		// Sized so the four trailing floats complete their register. A mismatch here is otherwise only found
		// as a visual artefact, which is a slow way to discover a packing bug.
		static_assert(sizeof(FDrawConstants) % 16 == 0, "Draw constants must be a whole number of 16 byte registers");

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

	void FForwardLitPass::Reflect(FRenderGraphPassTypeDesc& OutType) const
	{
		OutType.Description = "Shades the scene with a physically based BRDF, optionally sampling a shadow map.";

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

	bool FForwardLitPass::Initialize(FRenderer& Renderer)
	{
		Device = Renderer.GetDevice();
		if (Device == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "FForwardLitPass requires a device");
			return false;
		}

		FShaderLibrary& Shaders = Renderer.GetShaderLibrary();
		VertexShader = Shaders.GetShader("Passes/ForwardLit.hlsl", "MainVS", nvrhi::ShaderType::Vertex);
		PixelShader = Shaders.GetShader("Passes/ForwardLit.hlsl", "MainPS", nvrhi::ShaderType::Pixel);
		if (VertexShader == nullptr || PixelShader == nullptr)
		{
			return false;
		}

		// Layout must match FStaticMeshVertex, which the static_asserts above tie to the imported data.
		const std::array<nvrhi::VertexAttributeDesc, 4> Attributes = { {
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
			// Four components, the last being the bitangent's handedness rather than a coordinate.
			nvrhi::VertexAttributeDesc()
			    .setName("TANGENT")
			    .setFormat(nvrhi::Format::RGBA32_FLOAT)
			    .setOffset(offsetof(FStaticMeshVertex, Tangent))
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
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createInputLayout failed for the forward lit pass");
			return false;
		}

		FrameConstantBuffer =
		    Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(FFrameConstants), "ForwardFrameConstants", 16));
		DrawConstantBuffer =
		    Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(FDrawConstants), "ForwardDrawConstants", 256));
		if (FrameConstantBuffer == nullptr || DrawConstantBuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Failed to create the forward lit constant buffers");
			return false;
		}

		// Built through the helper so the Vulkan binding offsets are applied and one layout works on both
		// backends.
		//
		// The shadow map and its comparison sampler are always in the layout, because a shader cannot declare
		// a resource conditionally. When no caster is connected the pass binds a 1x1 stand-in and tells the
		// shader to skip the lookup. The material maps work the same way, resolving to a neutral 1x1 texture.
		//
		// One sampler for all five material maps: they want identical wrapping and filtering, and a sampler
		// each would consume five descriptors per material out of a heap capped at 2048.
		const nvrhi::BindingLayoutDesc LayoutDesc = MakeBindingLayoutDesc(nvrhi::ShaderType::All)
		                                                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
		                                                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(1))
		                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
		                                                .addItem(nvrhi::BindingLayoutItem::Sampler(0))
		                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
		                                                .addItem(nvrhi::BindingLayoutItem::Sampler(1))
		                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(2))
		                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(3))
		                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(4))
		                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(5));

		BindingLayout = Device->createBindingLayout(LayoutDesc);
		if (BindingLayout == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createBindingLayout failed for the forward lit pass");
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
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createSampler failed for the forward lit shadow sampler");
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
		                                            .setDebugName("ForwardLitFallbackShadow");
		FallbackShadowTexture = Device->createTexture(FallbackDesc);
		if (FallbackShadowTexture == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Failed to create the forward lit fallback shadow texture");
			return false;
		}

		return true;
	}

	void FForwardLitPass::Shutdown()
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

	bool FForwardLitPass::Compile(FRenderer& Renderer, const FRenderGraphPassResources& Resources)
	{
		LIME_UNUSED(Renderer);

		nvrhi::IFramebuffer* Framebuffer = Resources.GetFramebuffer();
		if (Framebuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "The forward lit pass has no framebuffer, so its colour output is not connected");
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

	bool FForwardLitPass::CreatePipeline(nvrhi::IFramebuffer* Framebuffer)
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
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createGraphicsPipeline failed for the forward lit pass");
			return false;
		}

		CurrentFramebuffer = Framebuffer;
		return true;
	}

	void FForwardLitPass::OnFramebufferChanged(nvrhi::IFramebuffer* Framebuffer)
	{
		// Dropped rather than rebuilt here: the pass may be notified while no command list is open, and the
		// next Render recreates it against whatever framebuffer is current then.
		Pipeline = nullptr;
		CurrentFramebuffer = nullptr;
		LIME_UNUSED(Framebuffer);
	}

	nvrhi::IBindingSet* FForwardLitPass::GetOrCreateBindingSet(int32 MaterialIndex)
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

		// Every material map resolves to something valid: the scene's texture when the material declares one,
		// otherwise that slot's neutral 1x1 stand-in.
		//
		// The draw constant buffer is volatile and rewritten before every draw, so it is bound here by handle
		// and its contents are not part of what makes a set reusable.
		const nvrhi::BindingSetDesc Desc =
		    nvrhi::BindingSetDesc()
		        .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, FrameConstantBuffer))
		        .addItem(nvrhi::BindingSetItem::ConstantBuffer(1, DrawConstantBuffer))
		        .addItem(nvrhi::BindingSetItem::Texture_SRV(
		            0, GpuResources.GetMaterialTexture(MaterialIndex, EMaterialTextureSlot::BaseColor)))
		        .addItem(nvrhi::BindingSetItem::Sampler(0, GpuResources.GetSampler()))
		        // The graph's shadow map when one is connected, otherwise the stand-in. Either way the binding
		        // is present, because the layout requires it.
		        .addItem(nvrhi::BindingSetItem::Texture_SRV(1, ShadowTexture != nullptr ? ShadowTexture : FallbackShadowTexture.Get()))
		        .addItem(nvrhi::BindingSetItem::Sampler(1, ShadowSampler))
		        .addItem(
		            nvrhi::BindingSetItem::Texture_SRV(2, GpuResources.GetMaterialTexture(MaterialIndex, EMaterialTextureSlot::Normal)))
		        .addItem(nvrhi::BindingSetItem::Texture_SRV(
		            3, GpuResources.GetMaterialTexture(MaterialIndex, EMaterialTextureSlot::MetallicRoughness)))
		        .addItem(
		            nvrhi::BindingSetItem::Texture_SRV(4, GpuResources.GetMaterialTexture(MaterialIndex, EMaterialTextureSlot::Emissive)))
		        .addItem(nvrhi::BindingSetItem::Texture_SRV(
		            5, GpuResources.GetMaterialTexture(MaterialIndex, EMaterialTextureSlot::Occlusion)));

		MaterialBindingSets[Slot] = Device->createBindingSet(Desc, BindingLayout);
		return MaterialBindingSets[Slot].Get();
	}

	void FForwardLitPass::OnBeginFrame(FRenderer& Renderer, const FFrameContext& Context)
	{
		LIME_UNUSED(Context);

		// Taken from the caster rather than recomputed here. Both would have to fit the same frustum to the
		// same bounds, and any difference between them would show up as shadows landing in the wrong place.
		if (const FShadowCasterPass* Caster = Renderer.FindPass<FShadowCasterPass>())
		{
			ShadowViewProjection = Caster->GetLightViewProjection();
		}
	}

	void FForwardLitPass::Render(const FFrameContext& Context)
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

				// A material with no metallic-roughness map is bound the white stand-in, which decodes to
				// metalness and roughness of 1: glTF's own defaults, and what the factors alone should then
				// describe. So unlike the normal and occlusion maps, this one needs no neutralising and the
				// setting simply forces the factors to act on their own.
				const bool bUseMetallicRoughness =
				    Settings.bEnableMetallicRoughnessMaps || Material.MetallicRoughnessImage == FMaterialData::NoImage;
				DrawConstants.MetallicFactor = Material.MetallicFactor;
				DrawConstants.RoughnessFactor = Material.RoughnessFactor;
				if (!bUseMetallicRoughness)
				{
					// The stand-in is still bound and still decodes to 1, so zeroing the factors is what
					// removes the map's influence without needing a second binding set.
					DrawConstants.MetallicFactor = 0.0f;
					DrawConstants.RoughnessFactor = 1.0f;
				}

				// Left at zero unless this material actually has a map and the setting allows it. The stand-in
				// texture bound in that case holds the flat normal, but 0.5 is not representable in 8 bit
				// UNORM, so it decodes to 0.004 rather than 0 and would tilt the surface by a fraction of a
				// degree. Zeroing the scale removes the tangential terms exactly instead.
				const bool bHasNormalMap = Material.NormalImage != FMaterialData::NoImage;
				DrawConstants.NormalScale = Settings.bEnableNormalMaps && bHasNormalMap ? Material.NormalScale : 0.0f;

				// Zero rather than the material's strength when there is no map, so the shader's lerp from 1
				// leaves the ambient untouched instead of multiplying it by the stand-in's value.
				const bool bHasOcclusionMap = Material.OcclusionImage != FMaterialData::NoImage;
				DrawConstants.OcclusionStrength = Settings.bEnableOcclusionMaps && bHasOcclusionMap ? Material.OcclusionStrength : 0.0f;

				// Black when disabled, which removes the term exactly: the stand-in texture is white, so the
				// factor is the only thing that can switch emission off.
				DrawConstants.EmissiveFactor = Settings.bEnableEmissive ? Material.EmissiveFactor : FVector3::Zero();

				Context.CommandList->writeBuffer(DrawConstantBuffer, &DrawConstants, sizeof(DrawConstants));

				// Cached by material: createBindingSet allocates descriptors on every call with no caching of
				// its own, and the D3D12 sampler heap holds at most 2048. This pass binds five material
				// textures per set, so building one per draw exhausts the heap even faster than it would with
				// a single texture.
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
