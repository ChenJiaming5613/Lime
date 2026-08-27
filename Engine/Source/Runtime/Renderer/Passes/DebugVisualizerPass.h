// Debug visualiser: shows any texture the graph produced in a form that can actually be looked at.
//
// The problem it solves is that most render targets are not viewable as they stand. A perspective depth
// buffer is non-linear, so displaying it raw gives a white rectangle; single channel data such as roughness
// or occlusion sits in the red channel and shows as a red tint rather than as greyscale. Marking either as
// a graph output is rejected at compile time for exactly this reason, and this pass is the supported way to
// look at one.
//
// Owns no geometry: the vertex shader generates a screen covering triangle from the vertex id.
//
// Takes its input through the render graph rather than from a fixed target, so the same pass can be pointed
// at any resource by editing the graph. Depth is detected from the connected resource's format rather than
// configured, so connecting one is enough to get a sensible image.

#pragma once

#include "Core/Reflection/Reflection.h"
#include "Renderer/RenderTypes.h"

namespace Lime
{
	struct FDebugVisualizerSettings
	{
		// Which channels reach the image, one toggle each.
		//
		// A single enabled channel is shown as greyscale rather than left in its own slot: a lone channel in
		// place tints the whole image, and a tint is much harder to read a magnitude from than a grey ramp.
		// That is what makes this usable for packed data such as roughness, metallic or occlusion.
		//
		// With more than one enabled each keeps its slot and the rest read as zero, which is the case for
		// comparing channels against each other. Alpha is only displayable on its own.
		bool bShowRed = true;
		bool bShowGreen = true;
		bool bShowBlue = true;
		bool bShowAlpha = false;
		// The input range mapped onto 0..1. Narrowing it is what makes low contrast data readable.
		float RangeMin = 0.0f;
		float RangeMax = 1.0f;
		// Off shows the source unchanged, which is the way to confirm what the raw values look like.
		bool bEnabled = true;
	};

	class FDebugVisualizerPass final : public TRenderPass<FDebugVisualizerPass>
	{
	public:
		// Runs after the passes it inspects. In a graph the topological order decides anyway; this only
		// matters for the ordering outside one.
		static constexpr ERenderPassPriority Priority = ERenderPassPriority::PostProcess;

		const char* GetName() const override { return "DebugVisualizer"; }

		void Reflect(FRenderGraphPassTypeDesc& OutType) const override;

		bool Initialize(FRenderer& Renderer) override;
		void Shutdown() override;
		bool Compile(FRenderer& Renderer, const FRenderGraphPassResources& Resources) override;
		void Render(const FFrameContext& Context) override;

		FReflectedRef GetReflectedSettings() override { return MakeReflectedRef(Settings); }

		FDebugVisualizerSettings& GetSettings() { return Settings; }

	private:
		bool CreatePipeline(nvrhi::IFramebuffer* Framebuffer);

		nvrhi::IDevice* Device = nullptr;
		nvrhi::ShaderHandle VertexShader;
		nvrhi::ShaderHandle PixelShader;
		nvrhi::BindingLayoutHandle BindingLayout;
		nvrhi::BindingSetHandle BindingSet;
		nvrhi::GraphicsPipelineHandle Pipeline;
		nvrhi::BufferHandle ConstantBuffer;
		nvrhi::SamplerHandle Sampler;

		FDebugVisualizerSettings Settings;

		// Whether the connected source holds perspective depth. Taken from the resource's format at compile
		// time rather than exposed as a setting: the graph already knows, and asking the user to say so again
		// only creates a way to get it wrong.
		bool bSourceIsDepth = false;

		// The texture the binding set was built against. A binding set names a specific texture, so it has
		// to be rebuilt when the graph reallocates after a resize.
		nvrhi::ITexture* BoundSource = nullptr;
		nvrhi::IFramebuffer* CurrentFramebuffer = nullptr;
	};
} // namespace Lime

LIME_REFLECT(Lime::FDebugVisualizerSettings)
{
	LIME_REFLECT_TYPE_NAME("Debug Visualizer");
	LIME_PROPERTY(bEnabled, Lime::FProp("Enabled"));
	LIME_PROPERTY(bShowRed, Lime::FProp("Red").Tooltip("Enable one channel alone to read it as greyscale"));
	LIME_PROPERTY(bShowGreen, Lime::FProp("Green"));
	LIME_PROPERTY(bShowBlue, Lime::FProp("Blue"));
	LIME_PROPERTY(bShowAlpha, Lime::FProp("Alpha").Tooltip("Only displayable on its own; ignored alongside colour channels"));
	LIME_PROPERTY(RangeMin, Lime::FProp("Range Min").Range(0.0f, 1.0f));
	LIME_PROPERTY(RangeMax, Lime::FProp("Range Max").Range(0.0f, 1.0f));
}
