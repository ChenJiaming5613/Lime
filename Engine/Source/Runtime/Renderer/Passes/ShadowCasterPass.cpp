#include "Renderer/Passes/ShadowCasterPass.h"

#include "Core/Logging/LogManager.h"
#include "Renderer/Renderer.h"
#include "Scene/Scene.h"

#include <nvrhi/utils.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Lime
{
	namespace
	{
		// Matches FShadowFrameConstants in ShadowDepth.hlsl.
		struct FFrameConstants
		{
			FMatrix4x4 LightViewProjection = FMatrix4x4::Identity();
		};

		// Matches FShadowDrawConstants.
		struct FDrawConstants
		{
			FMatrix4x4 World = FMatrix4x4::Identity();
		};

		// Leaves room around the fitted frustum so geometry just outside the bounds still casts. The bounds
		// cover what is in the scene, not what the light needs to see: a caster at the very edge would clip
		// against the near plane without this.
		constexpr float FrustumPadding = 1.05f;

		// Used when the scene is empty or its bounds are degenerate, so the pass produces a usable matrix
		// rather than one full of infinities.
		constexpr float FallbackFrustumExtent = 10.0f;
	} // namespace

	void FShadowCasterPass::Reflect(FRenderGraphPassTypeDesc& OutType) const
	{
		OutType.Description = "Renders scene depth from the light's point of view for use as a shadow map.";

		// Square and fixed, unlike the colour targets: a shadow map's resolution decides how much world
		// space one texel covers, which is unrelated to the size of the window. Pinning it here is what stops
		// the graph resolving it to the viewport size.
		FRenderGraphResourceDesc Depth = MakeTextureResource("shadowDepth", ERenderGraphResourceVisibility::Output, nvrhi::Format::D32);
		Depth.Width = ShadowMapSize;
		Depth.Height = ShadowMapSize;
		Depth.Description = "Depth from the light's point of view.";
		OutType.Outputs.push_back(std::move(Depth));
	}

	bool FShadowCasterPass::Initialize(FRenderer& Renderer)
	{
		Device = Renderer.GetDevice();
		if (Device == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "FShadowCasterPass requires a device");
			return false;
		}

		// No pixel shader: depth is the whole output, so one would only cost time.
		VertexShader = Renderer.GetShaderLibrary().GetShader("Passes/ShadowDepth.hlsl", "MainVS", nvrhi::ShaderType::Vertex);
		if (VertexShader == nullptr)
		{
			return false;
		}

		// The full vertex layout even though only the position is read. The layout describes the buffer, and
		// the same mesh buffers are drawn by the lit pass, so a shorter layout would misread the stride.
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
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createInputLayout failed for the shadow caster pass");
			return false;
		}

		FrameConstantBuffer =
		    Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(FFrameConstants), "ShadowFrameConstants", 16));
		DrawConstantBuffer =
		    Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(FDrawConstants), "ShadowDrawConstants", 256));
		if (FrameConstantBuffer == nullptr || DrawConstantBuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Failed to create the shadow caster constant buffers");
			return false;
		}

		// No texture or sampler: nothing is sampled while writing depth. One binding set serves every draw,
		// because the only thing that varies per draw is a volatile buffer's contents.
		const nvrhi::BindingLayoutDesc LayoutDesc = MakeBindingLayoutDesc(nvrhi::ShaderType::All)
		                                                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
		                                                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(1));

		BindingLayout = Device->createBindingLayout(LayoutDesc);
		if (BindingLayout == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createBindingLayout failed for the shadow caster pass");
			return false;
		}

		const nvrhi::BindingSetDesc BindingSetDesc = nvrhi::BindingSetDesc()
		                                                 .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, FrameConstantBuffer))
		                                                 .addItem(nvrhi::BindingSetItem::ConstantBuffer(1, DrawConstantBuffer));
		BindingSet = Device->createBindingSet(BindingSetDesc, BindingLayout);
		if (BindingSet == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createBindingSet failed for the shadow caster pass");
			return false;
		}

		// No Initialize on the resources: they upload lazily on the first frame with a scene, which also
		// covers the scene being replaced later.
		return true;
	}
	void FShadowCasterPass::Shutdown()
	{
		GpuResources.Release();
		Pipeline = nullptr;
		BindingSet = nullptr;
		BindingLayout = nullptr;
		DrawConstantBuffer = nullptr;
		FrameConstantBuffer = nullptr;
		InputLayout = nullptr;
		VertexShader = nullptr;
		CurrentFramebuffer = nullptr;
		Device = nullptr;
	}

	bool FShadowCasterPass::Compile(FRenderer& Renderer, const FRenderGraphPassResources& Resources)
	{
		LIME_UNUSED(Renderer);

		// The pipeline is compiled against the framebuffer's formats, so this is the first point at which it
		// can be built: before the graph allocated the depth target there was nothing to compile against.
		nvrhi::IFramebuffer* Framebuffer = Resources.GetFramebuffer();
		if (Framebuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "The shadow caster pass has no framebuffer, so its depth output is not connected");
			return false;
		}

		return CreatePipeline(Framebuffer);
	}

	bool FShadowCasterPass::CreatePipeline(nvrhi::IFramebuffer* Framebuffer)
	{
		if (Framebuffer == nullptr)
		{
			return false;
		}

		nvrhi::RenderState RenderState;
		RenderState.depthStencilState.depthTestEnable = true;
		RenderState.depthStencilState.depthWriteEnable = true;
		// LessOrEqual to match the lit pass: depth runs [0, 1] with 1 at the far plane.
		RenderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
		RenderState.depthStencilState.stencilEnable = false;

		// Front faces culled rather than back, which is the opposite of a colour pass. Recording the far side
		// of an object moves the recorded depth away from the lit surface, so the surface no longer shadows
		// itself and much less bias is needed.
		RenderState.rasterState.setCullFront();

		// Applied by the rasteriser rather than in the shader, so it scales with the actual depth slope of
		// each triangle. A constant offset in the vertex shader cannot do that.
		RenderState.rasterState.depthBias = static_cast<int32>(Settings.DepthBias * 100000.0f);
		RenderState.rasterState.slopeScaledDepthBias = Settings.SlopeScaledBias;

		const nvrhi::GraphicsPipelineDesc PipelineDesc = nvrhi::GraphicsPipelineDesc()
		                                                     .setPrimType(nvrhi::PrimitiveType::TriangleList)
		                                                     .setInputLayout(InputLayout)
		                                                     .setVertexShader(VertexShader)
		                                                     .addBindingLayout(BindingLayout)
		                                                     .setRenderState(RenderState);

		Pipeline = Device->createGraphicsPipeline(PipelineDesc, Framebuffer);
		if (Pipeline == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createGraphicsPipeline failed for the shadow caster pass");
			return false;
		}

		CurrentFramebuffer = Framebuffer;
		return true;
	}

	FMatrix4x4 FShadowCasterPass::ComputeLightViewProjection(const FScene& Scene, const FVector3& LightDirection)
	{
		const FBoundingBox Bounds = Scene.ComputeWorldBounds();

		// An empty or degenerate scene still needs a valid matrix, or the constant buffer would carry
		// infinities and the depth pass would produce nothing usable.
		const FVector3 Center = Bounds.bValid ? Bounds.GetCenter() : FVector3{ 0.0f, 0.0f, 0.0f };
		const float Radius = Bounds.bValid ? std::max(Bounds.GetLongestEdge() * 0.5f, 0.001f) : FallbackFrustumExtent;
		const float Extent = Radius * FrustumPadding;

		FVector3 Direction = LightDirection;
		const float DirectionLength = std::sqrt(Direction.X * Direction.X + Direction.Y * Direction.Y + Direction.Z * Direction.Z);
		if (DirectionLength < 0.0001f)
		{
			// A light with no direction would give a singular view matrix. Straight down is an arbitrary but
			// harmless choice, and it keeps the pass producing depth rather than failing.
			Direction = FVector3{ 0.0f, -1.0f, 0.0f };
		}
		else
		{
			Direction = FVector3{ Direction.X / DirectionLength, Direction.Y / DirectionLength, Direction.Z / DirectionLength };
		}

		// Positioned back along the direction far enough that the whole scene sits in front of the near
		// plane. Directional light has no position of its own, so this is chosen rather than given.
		const FVector3 Eye{ Center.X - Direction.X * Extent * 2.0f, Center.Y - Direction.Y * Extent * 2.0f,
			                Center.Z - Direction.Z * Extent * 2.0f };

		// Up is chosen to not be parallel to the direction, which would make the cross product in LookAtLH
		// degenerate. A light pointing straight up or down needs a different reference.
		const FVector3 Up = std::abs(Direction.Y) > 0.99f ? FVector3{ 0.0f, 0.0f, 1.0f } : FVector3{ 0.0f, 1.0f, 0.0f };

		const FMatrix4x4 View = FMatrix4x4::LookAtLH(Eye, Center, Up);
		// Orthographic, because a directional light's rays are parallel. A perspective projection would make
		// shadows converge towards a point the light does not have.
		const FMatrix4x4 Projection = FMatrix4x4::OrthographicOffCenterLH(-Extent, Extent, -Extent, Extent, 0.001f, Extent * 4.0f);

		return Multiply(Projection, View);
	}

	void FShadowCasterPass::Render(const FFrameContext& Context)
	{
		if (!Settings.bEnabled || Context.Scene == nullptr || Context.CommandList == nullptr || Pipeline == nullptr)
		{
			return;
		}

		FScene& Scene = *Context.Scene;
		if (Scene.GetMeshes().empty())
		{
			// The depth target was already cleared to the far plane, which reads as "nothing casts" when the
			// lit pass samples it.
			return;
		}

		if (!GpuResources.EnsureUploaded(Device, Context.CommandList, Scene))
		{
			return;
		}

		// One directional light, the first one found, matching what the lit pass shades with. Two passes
		// picking different lights would put the shadows somewhere the lighting does not.
		FVector3 LightDirection{ 0.0f, -1.0f, 0.0f };
		for (const auto [Entity, Light] : Scene.GetRegistry().view<const FDirectionalLightComponent>().each())
		{
			LightDirection = Light.Direction;
			break;
		}

		LightViewProjection = ComputeLightViewProjection(Scene, LightDirection);

		FFrameConstants FrameConstants;
		FrameConstants.LightViewProjection = LightViewProjection;
		Context.CommandList->writeBuffer(FrameConstantBuffer, &FrameConstants, sizeof(FrameConstants));

		// The shadow map's own size, not the viewport's: this pass renders into a fixed size target.
		const nvrhi::ViewportState ViewportState = nvrhi::ViewportState().addViewportAndScissorRect(
		    nvrhi::Viewport(static_cast<float>(ShadowMapSize), static_cast<float>(ShadowMapSize)));

		nvrhi::IFramebuffer* Framebuffer = Context.Resources != nullptr ? Context.Resources->GetFramebuffer() : Context.Framebuffer;
		if (Framebuffer == nullptr)
		{
			return;
		}

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

			FDrawConstants DrawConstants;
			DrawConstants.World = Transform.LocalToWorldMatrix;
			Context.CommandList->writeBuffer(DrawConstantBuffer, &DrawConstants, sizeof(DrawConstants));

			const FMeshData& Mesh = Scene.GetMeshes()[MeshRenderer.MeshIndex];

			nvrhi::GraphicsState GraphicsState;
			GraphicsState.pipeline = Pipeline;
			GraphicsState.framebuffer = Framebuffer;
			GraphicsState.viewport = ViewportState;
			GraphicsState.addBindingSet(BindingSet);
			GraphicsState.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(MeshGpu->VertexBuffer).setSlot(0));
			GraphicsState.indexBuffer = nvrhi::IndexBufferBinding().setBuffer(MeshGpu->IndexBuffer).setFormat(nvrhi::Format::R32_UINT);

			Context.CommandList->setGraphicsState(GraphicsState);

			// Sections are not walked separately: they exist to change material, and depth does not depend on
			// the material. One draw covers every index in the mesh instead.
			nvrhi::DrawArguments DrawArguments;
			DrawArguments.vertexCount = static_cast<uint32>(Mesh.Indices.size());
			Context.CommandList->drawIndexed(DrawArguments);
		}
	}
} // namespace Lime
