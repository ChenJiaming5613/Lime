#include "Renderer/Passes/DebugVisualizerPass.h"

#include "Camera/Camera.h"
#include "Core/Logging/LogManager.h"
#include "Renderer/Renderer.h"

#include <nvrhi/utils.h>

namespace Lime
{
	namespace
	{
		// Matches FDebugVisualizeConstants in DebugVisualize.hlsl.
		//
		// The mask leads, because a float4 cannot straddle a 16 byte register boundary in a cbuffer: placing
		// it after the scalars would make HLSL pad ahead of it, and every field from there on would be read
		// from an offset this struct does not use.
		struct FConstants
		{
			float ChannelMask[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
			int32 LinearizeDepth = 0;
			float NearPlane = 0.1f;
			float FarPlane = 1000.0f;
			float RangeMin = 0.0f;
			float RangeMax = 1.0f;
			float Padding[3] = { 0.0f, 0.0f, 0.0f };
		};

		// Asserted rather than trusted: a layout that disagrees with the shader produces plausible looking
		// wrong values instead of a failure, which is the hardest kind of mismatch to notice.
		static_assert(sizeof(FConstants) == 48, "FConstants must match the cbuffer layout in DebugVisualize.hlsl");

		// The field this pass reads. Named once so the reflection and the lookup at execution time cannot
		// drift apart.
		constexpr const char* SourceField = "source";
	} // namespace

	void FDebugVisualizerPass::Reflect(FRenderGraphPassTypeDesc& OutType) const
	{
		OutType.Description = "Shows a graph resource in a viewable form: linearises depth and expands single channels to greyscale.";

		// Format left open, which is what lets one pass type serve every resource: connecting a D32 depth
		// target and connecting an 8 bit colour target both resolve, because the graph takes the format from
		// whichever end specifies one.
		FRenderGraphResourceDesc Source = MakeTextureResource(SourceField, ERenderGraphResourceVisibility::Input);
		Source.Description = "Any resource to inspect. Depth is linearised automatically.";
		OutType.Inputs.push_back(std::move(Source));

		// Deliberately left at the graph's default, which is a colour format. That is the whole point of the
		// pass: it converts something unpresentable into something a graph output can carry.
		FRenderGraphResourceDesc Output = MakeTextureResource("color", ERenderGraphResourceVisibility::Output);
		Output.Description = "The visualisation, safe to mark as a graph output.";
		OutType.Outputs.push_back(std::move(Output));
	}

	bool FDebugVisualizerPass::Initialize(FRenderer& Renderer)
	{
		Device = Renderer.GetDevice();
		if (Device == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "FDebugVisualizerPass requires a device");
			return false;
		}

		FShaderLibrary& Shaders = Renderer.GetShaderLibrary();
		VertexShader = Shaders.GetShader("Passes/DebugVisualize.hlsl", "MainVS", nvrhi::ShaderType::Vertex);
		PixelShader = Shaders.GetShader("Passes/DebugVisualize.hlsl", "MainPS", nvrhi::ShaderType::Pixel);
		if (VertexShader == nullptr || PixelShader == nullptr)
		{
			return false;
		}

		ConstantBuffer =
		    Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(FConstants), "DebugVisualizeConstants", 16));
		if (ConstantBuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Failed to create the debug visualizer constant buffer");
			return false;
		}

		// Point sampling rather than linear, which matters for a debug view: filtering would average
		// neighbouring texels and show a value that is in no texel, and averaging depths is meaningless in
		// particular. Clamped so a sample at the edge cannot wrap to the opposite side.
		const nvrhi::SamplerDesc SamplerDesc =
		    nvrhi::SamplerDesc().setAllFilters(false).setAllAddressModes(nvrhi::SamplerAddressMode::ClampToEdge);
		Sampler = Device->createSampler(SamplerDesc);
		if (Sampler == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createSampler failed for the debug visualizer pass");
			return false;
		}

		const nvrhi::BindingLayoutDesc LayoutDesc = MakeBindingLayoutDesc(nvrhi::ShaderType::All)
		                                                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
		                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
		                                                .addItem(nvrhi::BindingLayoutItem::Sampler(0));

		BindingLayout = Device->createBindingLayout(LayoutDesc);
		if (BindingLayout == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createBindingLayout failed for the debug visualizer pass");
			return false;
		}

		return true;
	}

	void FDebugVisualizerPass::Shutdown()
	{
		Pipeline = nullptr;
		BindingSet = nullptr;
		BindingLayout = nullptr;
		Sampler = nullptr;
		ConstantBuffer = nullptr;
		PixelShader = nullptr;
		VertexShader = nullptr;
		BoundSource = nullptr;
		CurrentFramebuffer = nullptr;
		bSourceIsDepth = false;
		Device = nullptr;
	}

	bool FDebugVisualizerPass::Compile(FRenderer& Renderer, const FRenderGraphPassResources& Resources)
	{
		LIME_UNUSED(Renderer);

		nvrhi::IFramebuffer* Framebuffer = Resources.GetFramebuffer();
		if (Framebuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "The debug visualizer has no framebuffer, so its colour output is not connected");
			return false;
		}

		nvrhi::ITexture* Source = Resources.FindTexture(SourceField);
		if (Source == nullptr)
		{
			// Required rather than optional: a visualiser with nothing to visualise would draw a flat colour,
			// which looks exactly like a broken graph.
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "The debug visualizer has no '{}' input connected", SourceField);
			return false;
		}

		// Asked of nvrhi rather than matched against a list here, so the answer cannot drift from what the
		// RHI considers a depth format.
		const nvrhi::Format SourceFormat = Source->getDesc().format;
		bSourceIsDepth = nvrhi::getFormatInfo(SourceFormat).hasDepth;

		// Rebuilt rather than reused, because a binding set names one specific texture and the graph
		// allocates new ones whenever the target size changes.
		const nvrhi::BindingSetDesc BindingSetDesc = nvrhi::BindingSetDesc()
		                                                 .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, ConstantBuffer))
		                                                 .addItem(nvrhi::BindingSetItem::Texture_SRV(0, Source))
		                                                 .addItem(nvrhi::BindingSetItem::Sampler(0, Sampler));
		BindingSet = Device->createBindingSet(BindingSetDesc, BindingLayout);
		if (BindingSet == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createBindingSet failed for the debug visualizer pass");
			return false;
		}

		BoundSource = Source;

		LIME_LOG_INFO(LIME_LOG_CATEGORY_RENDERER, "Debug visualizer bound to '{}' ({}){}", Source->getDesc().debugName,
		              nvrhi::utils::FormatToString(SourceFormat), bSourceIsDepth ? ", linearising depth" : "");

		return CreatePipeline(Framebuffer);
	}

	bool FDebugVisualizerPass::CreatePipeline(nvrhi::IFramebuffer* Framebuffer)
	{
		if (Framebuffer == nullptr)
		{
			return false;
		}

		nvrhi::RenderState RenderState;
		// No depth at all: the triangle covers the target and there is nothing to occlude it. Testing against
		// a depth buffer this pass does not own would reject the whole draw — and the buffer it is inspecting
		// is bound for reading, so it must not also be tested against.
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
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createGraphicsPipeline failed for the debug visualizer pass");
			return false;
		}

		CurrentFramebuffer = Framebuffer;
		return true;
	}

	void FDebugVisualizerPass::Render(const FFrameContext& Context)
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

		// Disabled means show the source untouched rather than skip the draw: skipping would leave the output
		// holding its clear colour, so turning the pass off would blank the viewport instead of revealing what
		// the raw data looks like. The mask goes back to plain RGB for the same reason.
		Constants.ChannelMask[0] = !Settings.bEnabled || Settings.bShowRed ? 1.0f : 0.0f;
		Constants.ChannelMask[1] = !Settings.bEnabled || Settings.bShowGreen ? 1.0f : 0.0f;
		Constants.ChannelMask[2] = !Settings.bEnabled || Settings.bShowBlue ? 1.0f : 0.0f;
		Constants.ChannelMask[3] = Settings.bEnabled && Settings.bShowAlpha ? 1.0f : 0.0f;

		Constants.LinearizeDepth = Settings.bEnabled && bSourceIsDepth ? 1 : 0;
		Constants.RangeMin = Settings.bEnabled ? Settings.RangeMin : 0.0f;
		Constants.RangeMax = Settings.bEnabled ? Settings.RangeMax : 1.0f;

		// From the live camera rather than captured at compile time, so the image stays correct when the clip
		// planes change — the scene camera reframes itself whenever a model is loaded.
		if (Context.Camera != nullptr)
		{
			Constants.NearPlane = Context.Camera->GetNearPlane();
			Constants.FarPlane = Context.Camera->GetFarPlane();
		}

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
