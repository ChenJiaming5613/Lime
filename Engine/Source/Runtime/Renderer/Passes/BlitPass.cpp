#include "Renderer/Passes/BlitPass.h"

#include "Core/Logging/LogManager.h"

#include <nvrhi/utils.h>

namespace Lime
{
	void FBlitPass::Reflect(FRenderGraphPassTypeDesc& OutType) const
	{
		OutType.Description = "Copies one resource into another. Injected by the engine to present a graph output.";

		// Optional because the engine injects this pass before it knows whether the graph produced anything.
		// A graph that failed to compile has no output to present, and that has to leave a running frame
		// rather than a failed one: the fallback the renderer relies on is that the editor still draws.
		FRenderGraphResourceDesc Source = MakeTextureResource(SourceField, ERenderGraphResourceVisibility::Input);
		Source.bOptional = true;
		Source.Description = "The resource to copy from.";
		OutType.Inputs.push_back(std::move(Source));

		// Imported: the destination is the engine's, not the graph's. Which texture it is changes per frame
		// for the swap chain, so it is bound rather than allocated.
		FRenderGraphResourceDesc Target = MakeTextureResource(TargetField, ERenderGraphResourceVisibility::Output);
		Target.Source = ERenderGraphResourceSource::Imported;
		// The copy overwrites every pixel it touches, so there is nothing to preserve and nothing to clear.
		// Clearing first would be a write the copy immediately discards.
		Target.LoadAction = ERenderGraphLoadAction::DontCare;
		Target.Description = "The engine supplied texture to copy into.";
		OutType.Outputs.push_back(std::move(Target));
	}

	void FBlitPass::Render(const FFrameContext& Context)
	{
		if (Context.CommandList == nullptr || Context.Resources == nullptr)
		{
			return;
		}

		nvrhi::ITexture* Source = Context.Resources->FindTexture(SourceField);
		nvrhi::ITexture* Target = Context.Resources->FindTexture(TargetField);
		if (Source == nullptr || Target == nullptr)
		{
			// A normal state rather than an error: the input is optional, so an unbound source means the
			// graph produced nothing to present this frame.
			return;
		}

		// A copy needs both sides to agree on format and size. They can disagree legitimately: the graph may
		// end in a float target while the destination is 8 bit, and the sizes differ for a frame after a
		// resize. Skipped rather than converted, since a conversion is a pass and belongs in the graph.
		const nvrhi::TextureDesc& SourceDesc = Source->getDesc();
		const nvrhi::TextureDesc& TargetDesc = Target->getDesc();
		if (SourceDesc.format != TargetDesc.format || SourceDesc.width != TargetDesc.width ||
		    SourceDesc.height != TargetDesc.height)
		{
			// Reported, because the visible result is a black viewport on a graph that compiled and is
			// running — which looks like a rendering bug rather than a mismatch that was skipped on purpose.
			//
			// A size difference lasts a frame after a resize and is not worth mentioning; a format difference
			// never resolves on its own, so only that is logged. Once per offending format rather than per
			// frame, or the log fills at frame rate while the viewport shows nothing.
			if (SourceDesc.format != TargetDesc.format && SourceDesc.format != ReportedFormatMismatch)
			{
				ReportedFormatMismatch = SourceDesc.format;
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_RENDERER,
				  "The blit source {} is {} but the target is {}, so it cannot be copied and the viewport "
				  "will stay black. End the graph in a pass that writes the target's format.",
				  SourceDesc.debugName, nvrhi::utils::FormatToString(SourceDesc.format),
				  nvrhi::utils::FormatToString(TargetDesc.format));
			}
			return;
		}

		ReportedFormatMismatch = nvrhi::Format::UNKNOWN;
		Context.CommandList->copyTexture(Target, nvrhi::TextureSlice(), Source, nvrhi::TextureSlice());
	}
} // namespace Lime
