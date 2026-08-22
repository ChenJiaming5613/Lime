#include "TrianglePass.h"

#include "Core/Logging/LogManager.h"
#include "Core/Math/Matrix.h"
#include "Renderer/RenderPassRegistry.h"
#include "Renderer/Renderer.h"

#include <nvrhi/utils.h>

#include <array>

namespace HelloTriangle
{
	using namespace Lime;

	namespace
	{
		// Must match FTriangleConstants in Triangle.hlsl.
		struct FTriangleConstants
		{
			FMatrix4x4 WorldViewProjection;
			FVector4 Tint;
		};

		constexpr std::array<FSimpleVertex, 3> TriangleVertices = { {
			{ { 0.0f, 0.6f, 0.0f }, { 1.0f, 0.25f, 0.25f, 1.0f } },
			{ { 0.55f, -0.4f, 0.0f }, { 0.25f, 1.0f, 0.35f, 1.0f } },
			{ { -0.55f, -0.4f, 0.0f }, { 0.3f, 0.45f, 1.0f, 1.0f } },
		} };
	} // namespace

	bool FTrianglePass::Initialize(FRenderer& InRenderer)
	{
		Renderer = &InRenderer;
		Device = InRenderer.GetDevice();
		if (Device == nullptr)
		{
			return false;
		}

		// Resolved from the project shader root, which the engine registers automatically.
		FShaderLibrary& Shaders = InRenderer.GetShaderLibrary();
		VertexShader = Shaders.GetShader("Triangle/Triangle.hlsl", "MainVS", nvrhi::ShaderType::Vertex);
		PixelShader = Shaders.GetShader("Triangle/Triangle.hlsl", "MainPS", nvrhi::ShaderType::Pixel);
		if (VertexShader == nullptr || PixelShader == nullptr)
		{
			return false;
		}

		// Semantic names must match the HLSL declarations; the VS handle lets D3D validate the layout.
		const std::array<nvrhi::VertexAttributeDesc, 2> Attributes = { {
			nvrhi::VertexAttributeDesc()
			    .setName("POSITION")
			    .setFormat(nvrhi::Format::RGB32_FLOAT)
			    .setOffset(offsetof(FSimpleVertex, Position))
			    .setElementStride(sizeof(FSimpleVertex)),
			nvrhi::VertexAttributeDesc()
			    .setName("COLOR")
			    .setFormat(nvrhi::Format::RGBA32_FLOAT)
			    .setOffset(offsetof(FSimpleVertex, Color))
			    .setElementStride(sizeof(FSimpleVertex)),
		} };

		InputLayout = Device->createInputLayout(Attributes.data(), static_cast<uint32_t>(Attributes.size()), VertexShader);
		if (InputLayout == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_APP, "createInputLayout failed for the triangle pass");
			return false;
		}

		const nvrhi::BufferDesc VertexBufferDesc = nvrhi::BufferDesc()
		                                               .setByteSize(sizeof(TriangleVertices))
		                                               .setIsVertexBuffer(true)
		                                               .setInitialState(nvrhi::ResourceStates::VertexBuffer)
		                                               .setKeepInitialState(true)
		                                               .setDebugName("TriangleVertices");

		VertexBuffer = Device->createBuffer(VertexBufferDesc);
		if (VertexBuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_APP, "createBuffer failed for the triangle vertex buffer");
			return false;
		}

		// Volatile constant buffers map onto the cheap per-draw constant path on both backends.
		ConstantBuffer =
		    Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(FTriangleConstants), "TriangleConstants", 16));
		if (ConstantBuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_APP, "createBuffer failed for the triangle constant buffer");
			return false;
		}

		// Built through MakeBindingLayoutDesc so the Vulkan binding offsets are applied.
		const nvrhi::BindingLayoutDesc LayoutDesc =
		    MakeBindingLayoutDesc(nvrhi::ShaderType::All).addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));

		BindingLayout = Device->createBindingLayout(LayoutDesc);
		if (BindingLayout == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_APP, "createBindingLayout failed for the triangle pass");
			return false;
		}

		const nvrhi::BindingSetDesc BindingSetDesc =
		    nvrhi::BindingSetDesc().addItem(nvrhi::BindingSetItem::ConstantBuffer(0, ConstantBuffer));

		BindingSet = Device->createBindingSet(BindingSetDesc, BindingLayout);
		if (BindingSet == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_APP, "createBindingSet failed for the triangle pass");
			return false;
		}

		// Upload the static geometry once.
		nvrhi::CommandListHandle UploadList = Device->createCommandList();
		UploadList->open();
		UploadList->writeBuffer(VertexBuffer, TriangleVertices.data(), sizeof(TriangleVertices));
		UploadList->close();
		Device->executeCommandList(UploadList);
		Device->waitForIdle();

		return true;
	}

	bool FTrianglePass::CreatePipeline(nvrhi::IFramebuffer* Framebuffer)
	{
		if (Framebuffer == nullptr)
		{
			return false;
		}

		nvrhi::RenderState RenderState;
		RenderState.depthStencilState.depthTestEnable = false;
		RenderState.depthStencilState.depthWriteEnable = false;
		RenderState.depthStencilState.stencilEnable = false;
		RenderState.rasterState.setCullNone();

		const nvrhi::GraphicsPipelineDesc PipelineDesc = nvrhi::GraphicsPipelineDesc()
		                                                     .setPrimType(nvrhi::PrimitiveType::TriangleList)
		                                                     .setInputLayout(InputLayout)
		                                                     .setVertexShader(VertexShader)
		                                                     .setPixelShader(PixelShader)
		                                                     .addBindingLayout(BindingLayout)
		                                                     .setRenderState(RenderState);

		Pipeline = Device->createGraphicsPipeline(PipelineDesc, Framebuffer->getFramebufferInfo());
		if (Pipeline == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_APP, "createGraphicsPipeline failed for the triangle pass");
			return false;
		}

		return true;
	}

	void FTrianglePass::OnFramebufferChanged(nvrhi::IFramebuffer* Framebuffer)
	{
		Pipeline = nullptr;
		CreatePipeline(Framebuffer);
	}

	void FTrianglePass::Shutdown()
	{
		Pipeline = nullptr;
		BindingSet = nullptr;
		BindingLayout = nullptr;
		ConstantBuffer = nullptr;
		VertexBuffer = nullptr;
		InputLayout = nullptr;
		PixelShader = nullptr;
		VertexShader = nullptr;
		Device = nullptr;
		Renderer = nullptr;
	}

	void FTrianglePass::Render(const FFrameContext& Context)
	{
		if (Pipeline == nullptr && !CreatePipeline(Context.Framebuffer))
		{
			return;
		}

		if (Context.CommandList == nullptr || Context.ViewportWidth == 0 || Context.ViewportHeight == 0)
		{
			return;
		}

		// The pass advances its own rotation, so no application update hook is needed.
		if (!Settings.bPaused)
		{
			RotationRadians = WrapAngle(RotationRadians + Settings.RotationSpeed * Context.DeltaSeconds);
		}

		// The clear colour lives in the settings, so the inspector can drive it without extra wiring.
		if (Renderer != nullptr)
		{
			Renderer->SetClearColor(Settings.BackgroundColor);
		}

		const FMatrix4x4 World = FMatrix4x4::RotationZ(RotationRadians);
		const FMatrix4x4 View = FMatrix4x4::LookAtLH({ 0.0f, 0.0f, -2.5f }, FVector3::Zero(), FVector3::UnitY());
		const FMatrix4x4 Projection =
		    FMatrix4x4::PerspectiveFovLH(DegreesToRadians(Settings.FovDegrees), Context.GetAspectRatio(), 0.1f, 100.0f);

		FTriangleConstants Constants;
		Constants.WorldViewProjection = Multiply(Projection, Multiply(View, World));
		Constants.Tint = Settings.Tint;

		Context.CommandList->writeBuffer(ConstantBuffer, &Constants, sizeof(Constants));

		const nvrhi::GraphicsState State =
		    nvrhi::GraphicsState()
		        .setPipeline(Pipeline)
		        .setFramebuffer(Context.Framebuffer)
		        .addBindingSet(BindingSet)
		        .addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(VertexBuffer).setSlot(0).setOffset(0))
		        .setViewport(nvrhi::ViewportState().addViewportAndScissorRect(
		            nvrhi::Viewport(static_cast<float>(Context.ViewportWidth), static_cast<float>(Context.ViewportHeight))));

		Context.CommandList->setGraphicsState(State);
		Context.CommandList->draw(nvrhi::DrawArguments().setVertexCount(static_cast<uint32_t>(TriangleVertices.size())));
	}
} // namespace HelloTriangle

LIME_REGISTER_RENDER_PASS(HelloTriangle::FTrianglePass);
