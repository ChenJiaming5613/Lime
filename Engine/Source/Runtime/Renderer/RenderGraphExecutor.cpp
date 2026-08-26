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
	                        const FVector4& ClearColor)
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

			// Each pass gets its own context: the framebuffer and the resource view differ per pass, while
			// the timing and the scene do not. Copied rather than mutated in place so a pass cannot leak
			// state into the next one.
			FFrameContext PassContext = FrameContext;
			PassContext.Resources = View;

			nvrhi::IFramebuffer* Framebuffer = View->GetFramebuffer();
			if (Framebuffer != nullptr)
			{
				PassContext.Framebuffer = Framebuffer;

				const nvrhi::FramebufferDesc& Desc = Framebuffer->getDesc();

				// Cleared here rather than by the pass. Every attachment is written this frame, and a target
				// left holding the previous frame's contents would blend two frames together wherever the
				// pass did not cover the whole surface. Doing it centrally also means a pass cannot forget.
				for (uint32 Slot = 0; Slot < static_cast<uint32>(Desc.colorAttachments.size()); ++Slot)
				{
					nvrhi::utils::ClearColorAttachment(FrameContext.CommandList, Framebuffer, Slot,
					                                   nvrhi::Color(ClearColor.X, ClearColor.Y, ClearColor.Z, ClearColor.W));
				}

				if (Desc.depthAttachment.texture != nullptr)
				{
					nvrhi::utils::ClearDepthStencilAttachment(FrameContext.CommandList, Framebuffer, FarPlaneDepth, 0);
				}
			}

			Pass->Render(PassContext);
		}
	}
} // namespace Lime
