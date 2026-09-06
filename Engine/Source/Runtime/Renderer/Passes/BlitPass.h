// Copies one graph resource into another. The engine injects it to put a graph output on screen.
//
// A copy rather than a full screen draw, deliberately. Presenting through a draw would need a pipeline
// compiled against the destination's framebuffer, which for the swap chain means rebuilding it whenever
// the back buffer rotates. A copy also states the intent exactly: the destination is replaced, so there
// is nothing to blend and no load action to respect.
//
// Owns nothing. There is no shader, no pipeline and no scene: both sides arrive through the resource view
// every pass already gets, so Initialize has nothing to ask the renderer for. That is why it sits with
// the built-in passes despite being graph plumbing rather than rendering.

#pragma once

#include "Renderer/RenderTypes.h"

namespace Lime
{
	class FBlitPass final : public TRenderPass<FBlitPass>
	{
	public:
		// Ordering only, and only within the registry listing: an injected pass is placed by the injector,
		// after everything the graph file described.
		static constexpr ERenderPassPriority Priority = ERenderPassPriority::Overlay;

		// The field names this pass reads and writes, so the injector can name them without repeating
		// string literals that would drift from Reflect.
		static constexpr const char* SourceField = "source";
		static constexpr const char* TargetField = "target";

		const char* GetName() const override { return "Blit"; }

		void Reflect(FRenderGraphPassTypeDesc& OutType) const override;

		// Nothing to create: the copy needs only the command list the frame context carries.
		bool Initialize(FRenderer& Renderer) override
		{
			LIME_UNUSED(Renderer);
			return true;
		}
		void Shutdown() override {}

		void Render(const FFrameContext& Context) override;

	private:
		// The source format last reported as unable to reach the destination.
		//
		// Remembered so the warning is logged once rather than every frame, and reset on a successful copy
		// so a later mismatch is reported again. Per instance, since two injected blits can fail
		// differently.
		nvrhi::Format ReportedFormatMismatch = nvrhi::Format::UNKNOWN;
	};
} // namespace Lime
