// Passes the engine adds to a compiled graph that the graph file never mentions.
//
// Injection happens after the compile, never in FRenderGraphDesc. The description is the single source of
// truth that the panel draws and that saving serialises, so a pass placed there would have to be filtered
// out again in both, and those two filters are exactly how the file and the screen end up disagreeing.
// Appending to the compile result instead makes the three properties fall out for free: an injected pass
// is not in the file, not on the panel, and exists only for the run it was injected into.
//
// Deliberately free of GPU calls, like the compiler it extends. What gets injected is decidable from the
// compile result alone, so the wiring is covered by tests that need no device.

#pragma once

#include "RenderGraph/RenderGraphCompiler.h"

#include <string>
#include <vector>

namespace Lime
{
	// One pass to append, and how to wire it up.
	//
	// The input is named by output slot rather than by resource, because that is what the engine actually
	// knows: it asks for "whatever the graph was told to produce first", and which pass ends up producing
	// it is the graph's business. A slot that the graph does not have leaves the input unbound, which is
	// why an injected pass declares its inputs optional.
	struct FRenderGraphInjection
	{
		// Must carry the reserved prefix, so it can never collide with a hand written name.
		std::string PassName;
		std::string TypeName;

		// The graph output slot to bind this pass's input to, or -1 to leave it unbound.
		int32 InputFromOutputSlot = 0;
		// The pass field the slot is bound to. Empty means the pass's first declared input.
		std::string InputFieldName;

		// Bound to the engine's imported target of this name, e.g. "$BackBuffer". Every imported output the
		// pass declares resolves to it.
		std::string ImportedTarget;

		// Forces the executor to end the current command list before this pass. Kept explicit rather than
		// inferred from the target, because whether a submission boundary is needed is a synchronisation
		// question and not something the graph shape implies.
		bool bBeginsNewSubmission = false;
	};

	// Appends each injection to the compiled result, resolving its input and its imported target.
	//
	// Order is preserved and every injected pass runs after everything the file described, which is the
	// only placement that makes sense for a pass consuming a graph output.
	//
	// A failure to wire one injection is reported as a warning and that injection is skipped, rather than
	// failing the whole graph: the editor UI failing to find a scene to draw over must still leave a
	// usable editor, which is the same fallback the renderer already relies on.
	void InjectRenderGraphPasses(FRenderGraphCompileResult& Compiled, const std::vector<FRenderGraphInjection>& Injections,
	              const FRenderGraphPassTypeRegistry& Types);
} // namespace Lime
