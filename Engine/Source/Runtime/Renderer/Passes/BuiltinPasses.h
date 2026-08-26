// Registration of the passes the engine itself provides.
//
// A single explicit call rather than self registration in each pass file. These live in a static library,
// and the linker is free to discard a translation unit nothing references: a pass that only registered
// itself would simply never appear, and the graph naming it would look like it referred to a pass this
// build lacks.
//
// Kept apart from the passes so adding one is a change to this list rather than to whichever pass file
// happened to hold the function.

#pragma once

#include "RenderGraph/RenderGraphPassType.h"

namespace Lime
{
	// Registers every engine provided pass. Called by FEngine before instantiating the registry, so that
	// built-in and project passes end up in one ordered list.
	void RegisterBuiltinRenderPasses();

	// The pass types a render graph may name, reflected from the built-in passes.
	//
	// Registers the built-ins first, so a caller gets a populated table without having to know the order.
	// Useful on its own for validating a graph file before a device exists, which is what the tests do.
	FRenderGraphPassTypeRegistry BuildRenderGraphPassTypes();
} // namespace Lime
