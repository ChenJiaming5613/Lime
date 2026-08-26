#include "Renderer/Passes/PostProcessPass.h"

#include "Core/Logging/LogManager.h"
#include "Renderer/Renderer.h"

#include <nvrhi/utils.h>

namespace Lime
{
	namespace
	{
		// Matches FPostProcessConstants in PostProcess.hlsl.
		struct FConstants
		{
			float Exposure = 1.0f;
			float Gamma = 2.2f;
			float Padding[2] = { 0.0f, 0.0f };
		};

		// The field this pass reads. Named once so the reflection and the lookup at execution time cannot
		// drift apart.
		constexpr const char* SceneColorField = "sceneColor";
	} // namespace

	void FPostProcessPass::Reflect(FRenderGraphPassTypeDesc& OutType) const
	{
		OutType.Description = "Applies exposure and a gamma curve to the scene colour.";

		// Format left open so this works after a float lit pass or an 8 bit one; the graph resolves it from
		// whatever is connected.
		FRenderGraphResourceDesc Input = MakeTextureResource(SceneColorField, ERenderGraphResourceVisibility::Input);
		Input.Description = "Scene colour to grade.";
		OutType.Inputs.push_back(std::move(Input));

		FRenderGraphResourceDesc Output = MakeTextureResource("color", ERenderGraphResourceVisibility::Output);
		Output.Description = "Graded colour.";
		OutType.Outputs.push_back(std::move(Output));
	}

	bool FPostProcessPass::Initialize(FRenderer& Renderer)
	{
		Device = Renderer.GetDevice();
		if (Device == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "FPostProcessPass requires a device");
			return false;
		}

		FShaderLibrary& Shaders = Renderer.GetShaderLibrary();
		VertexShader = Shaders.GetShader("Passes/PostProcess.hlsl", "MainVS", nvrhi::ShaderType::Vertex);
		PixelShader = Shaders.GetShader("Passes/PostProcess.hlsl", "MainPS", nvrhi::ShaderType::Pixel);
		if (VertexShader == nullptr || PixelShader == nullptr)
		{
			return false;
		}

		ConstantBuffer = Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(FConstants), "PostProcessConstants", 16));
		if (ConstantBuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Failed to create the post process constant buffer");
			return false;
		}

		// Clamped rather than wrapped: the triangle covers exactly the target, and a sample landing a texel
		// outside at the edge would otherwise read from the opposite side.
		const nvrhi::SamplerDesc SamplerDesc =
		    nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::ClampToEdge);
		Sampler = Device->createSampler(SamplerDesc);
		if (Sampler == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createSampler failed for the post process pass");
			return false;
		}

		const nvrhi::BindingLayoutDesc LayoutDesc = MakeBindingLayoutDesc(nvrhi::ShaderType::All)
		                                                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
		                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
		                                                .addItem(nvrhi::BindingLayoutItem::Sampler(0));

		BindingLayout = Device->createBindingLayout(LayoutDesc);
		if (BindingLayout == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createBindingLayout failed for the post process pass");
			return false;
		}

		return true;
	}

	void FPostProcessPass::Shutdown()
	{
		Pipeline = nullptr;
		BindingSet = nullptr;
		BindingLayout = nullptr;
		Sampler = nullptr;
		ConstantBuffer = nullptr;
		PixelShader = nullptr;
		VertexShader = nullptr;
		BoundInput = nullptr;
		CurrentFramebuffer = nullptr;
		Device = nullptr;
	}

	bool FPostProcessPass::Compile(FRenderer& Renderer, const FRenderGraphPassResources& Resources)
	{
		LIME_UNUSED(Renderer);

		nvrhi::IFramebuffer* Framebuffer = Resources.GetFramebuffer();
		if (Framebuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "The post process pass has no framebuffer, so its colour output is not connected");
			return false;
		}

		nvrhi::ITexture* Input = Resources.FindTexture(SceneColorField);
		if (Input == nullptr)
		{
			// Required rather than optional: grading nothing has no meaning, and a pass that silently drew
			// black would look like the graph had broken somewhere earlier.
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "The post process pass has no '{}' input connected", SceneColorField);
			return false;
		}

		// Rebuilt here rather than reused, because a binding set names one specific texture and the graph
		// allocates new ones whenever the target size changes.
		const nvrhi::BindingSetDesc BindingSetDesc = nvrhi::BindingSetDesc()
		                                                 .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, ConstantBuffer))
		                                                 .addItem(nvrhi::BindingSetItem::Texture_SRV(0, Input))
		                                                 .addItem(nvrhi::BindingSetItem::Sampler(0, Sampler));
		BindingSet = Device->createBindingSet(BindingSetDesc, BindingLayout);
		if (BindingSet == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createBindingSet failed for the post process pass");
			return false;
		}

		BoundInput = Input;
		return CreatePipeline(Framebuffer);
	}

	bool FPostProcessPass::CreatePipeline(nvrhi::IFramebuffer* Framebuffer)
	{
		if (Framebuffer == nullptr)
		{
			return false;
		}

		nvrhi::RenderState RenderState;
		// No depth at all: the triangle covers the target and there is nothing to occlude it. Testing against
		// a depth buffer this pass does not own would reject the whole draw.
		RenderState.depthStencilState.depthTestEnable = false;
		RenderState.depthStencilState.depthWriteEnable = false;
		RenderState.depthStencilState.stencilEnable = false;
		// The generated triangle has a fixed winding, so culling either face risks discarding it depending on
		// which way the backend considers front.
		RenderState.rasterState.setCullNone();

		const nvrhi::GraphicsPipelineDesc PipelineDesc = nvrhi::GraphicsPipelineDesc()
		                                                     .setPrimType(nvrhi::PrimitiveType::TriangleList)
		                                                     .setVertexShader(VertexShader)
		                                                     .setPixelShader(PixelShader)
		                                                     .addBindingLayout(BindingLayout)
		                                                     .setRenderState(RenderState);

		Pipeline = Device->createGraphicsPipeline(PipelineDesc, Framebuffer);
		if (Pipeline == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createGraphicsPipeline failed for the post process pass");
			return false;
		}

		CurrentFramebuffer = Framebuffer;
		return true;
	}

	void FPostProcessPass::Render(const FFrameContext& Context)
	{
		if (Context.CommandList == nullptr || Pipeline == nullptr || BindingSet == nullptr)
		{
			return;
		}

		nvrhi::IFramebuffer* Framebuffer = Context.Resources != nullptr ? Context.Resources->GetFramebuffer() : Context.Framebuffer;
		if (Framebuffer == nullptr)
		{
			return;
		}

		FConstants Constants;
		// Disabled means pass the colour through rather than skip the draw: skipping would leave the output
		// target holding only its clear colour, so turning the pass off would blank the viewport.
		Constants.Exposure = Settings.bEnabled ? Settings.Exposure : 1.0f;
		Constants.Gamma = Settings.bEnabled ? Settings.Gamma : 1.0f;
		Context.CommandList->writeBuffer(ConstantBuffer, &Constants, sizeof(Constants));

		const nvrhi::ViewportState ViewportState = nvrhi::ViewportState().addViewportAndScissorRect(
		    nvrhi::Viewport(static_cast<float>(Context.ViewportWidth), static_cast<float>(Context.ViewportHeight)));

		nvrhi::GraphicsState GraphicsState;
		GraphicsState.pipeline = Pipeline;
		GraphicsState.framebuffer = Framebuffer;
		GraphicsState.viewport = ViewportState;
		GraphicsState.addBindingSet(BindingSet);
		// No vertex or index buffer: the positions come from the vertex id.
		Context.CommandList->setGraphicsState(GraphicsState);

		nvrhi::DrawArguments DrawArguments;
		DrawArguments.vertexCount = 3;
		Context.CommandList->draw(DrawArguments);
	}
} // namespace Lime
