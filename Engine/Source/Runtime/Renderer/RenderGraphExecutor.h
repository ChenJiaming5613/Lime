// Runs a compiled render graph for one frame.
//
// Thin on purpose. The ordering was decided when the graph was compiled and the textures when they were
// allocated, so this only walks the order, points each pass at its own resources, and clears what a pass
// is about to write.
//
// Barriers are left to nvrhi, which tracks resource states from the initial state each texture was
// created with. That is why the allocation sets those states rather than leaving them undefined: getting
// them wrong here would show up as a validation error rather than a wrong image.

#pragma once

#include "RenderGraph/RenderGraphCompiler.h"
#include "Renderer/RenderGraphResources.h"
#include "Renderer/RenderTypes.h"

#include <memory>
#include <vector>

namespace Lime
{
	// A compiled graph paired with the passes that implement it.
	//
	// Built once at startup. An empty plan is a valid state and means the graph could not be compiled: the
	// frame then renders nothing, which is what the caller falls back to rather than rendering something
	// wrong.
	struct FRenderGraphPlan
	{
		FRenderGraphCompileResult Compiled;
		// Parallel to Compiled.ExecutionOrder, so index N is the pass that entry describes. Held as shared
		// pointers because the renderer owns the same instances.
		std::vector<std::shared_ptr<IRenderPass>> Passes;

		bool IsRunnable() const { return Compiled.bSucceeded && !Passes.empty(); }
		void Reset()
		{
			Compiled = {};
			Passes.clear();
		}
	};

	// Executes the plan into the given command list.
	//
	// The command list is opened and submitted by the caller, so several plans can share one submission and
	// the caller keeps control of where the boundaries are.
	void ExecuteRenderGraph(const FRenderGraphPlan& Plan, const FRenderGraphResources& Resources, const FFrameContext& FrameContext,
	                        const FVector4& ClearColor);
} // namespace Lime
