#include "Renderer/RenderGraphExecutor.h"

#include <nvrhi/utils.h>

namespace Lime
{
	namespace
	{
		// Matches the clear value the depth textures were created with, which is what keeps D3D12 on its
		// fast clear path.
		constexpr float FarPlaneDepth = 1.0f;
	} // namespace

	void ExecuteRenderGraph(const FRenderGraphPlan& Plan, const FRenderGraphResources& Resources, const FFrameContext& FrameContext,
	    const FVector4& ClearColor, const FRenderGraphSubmitFunc& Submit)
	{
		if (!Plan.IsRunnable() || FrameContext.CommandList == nullptr)
		{
			return;
		}

		const std::vector<FCompiledPass>& Order = Plan.Compiled.ExecutionOrder;
		for (SizeType Index = 0; Index < Order.size(); ++Index)
		{
			IRenderPass* Pass = Index < Plan.Passes.size() ? Plan.Passes[Index].get() : nullptr;
			if (Pass == nullptr)
			{
				continue;
			}

			const FRenderGraphPassView* View = Resources.FindView(Index);
			if (View == nullptr)
			{
				continue;
			}

			const FCompiledPass& Compiled = Order[Index];

			// Before the framebuffer is resolved, so a pass that has to observe earlier work sees it. The
			// boundary is declared by the pass rather than inferred, since nvrhi already covers the ordinary
			// write-then-read within one list.
			if (Compiled.bBeginsNewSubmission && Submit != nullptr)
			{
				Submit();
			}

			// Each pass gets its own context: the framebuffer and the resource view differ per pass, while
			// the timing and the scene do not. Copied rather than mutated in place so a pass cannot leak
			// state into the next one.
			FFrameContext PassContext = FrameContext;
			PassContext.Resources = View;

			nvrhi::IFramebuffer* Framebuffer = View->GetFramebuffer();
			if (Framebuffer != nullptr)
			{
				PassContext.Framebuffer = Framebuffer;

				// The viewport follows the target the pass actually writes, which is what lets an injected pass
				// draw into a back buffer that is a different size from the graph's own textures.
				// FramebufferInfoEx rather than FramebufferInfo: only the extended form carries dimensions.
				const nvrhi::FramebufferInfoEx& Info = Framebuffer->getFramebufferInfo();
				PassContext.ViewportWidth = Info.width;
				PassContext.ViewportHeight = Info.height;

				const nvrhi::FramebufferDesc& Desc = Framebuffer->getDesc();

				// Cleared here rather than by the pass, so a pass cannot forget. Which attachments get cleared
				// is the compile's decision: a transient target is fully written each frame and would otherwise
				// blend two frames together, while an imported one may already hold work this frame that the
				// pass is drawing over. Clearing the latter is what would erase the scene the editor draws on
				// top of.
				for (uint32 Slot = 0; Slot < static_cast<uint32>(Desc.colorAttachments.size()); ++Slot)
				{
					if (Slot < View->ColorAttachmentCount() && View->GetColorLoadAction(Slot) != ERenderGraphLoadAction::Clear)
					{
						continue;
					}

					nvrhi::utils::ClearColorAttachment(FrameContext.CommandList, Framebuffer, Slot,
					        nvrhi::Color(ClearColor.X, ClearColor.Y, ClearColor.Z, ClearColor.W));
				}

				if (Desc.depthAttachment.texture != nullptr && View->GetDepthLoadAction() == ERenderGraphLoadAction::Clear)
				{
					nvrhi::utils::ClearDepthStencilAttachment(FrameContext.CommandList, Framebuffer, FarPlaneDepth, 0);
				}
			}

			Pass->Render(PassContext);
		}
	}
} // namespace Lime
