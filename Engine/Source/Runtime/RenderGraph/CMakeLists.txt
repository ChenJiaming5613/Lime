# LimeRenderGraph: the pass dependency graph as plain data, plus its JSON form and its layout.
#
# Has no UI dependency, for the same reason LimeAsset has none: the graph, its validation and the
# layering algorithm are all decidable from data alone, so they can be covered by unit tests with no
# ImGui context.
#
# Depends on LimeRHI only for nvrhi::Format and the pure functions over it. That is not a GPU
# dependency: no device is needed to describe a resource, and the test binary already links LimeRHI, so
# the validation and layout tests still run on a machine without a graphics device. Describing formats
# in the RHI's own vocabulary is worth more than a private enum plus a translation table that could
# drift from it.
#
# The editor panel that draws this lives in LimeEditor. Nothing here knows that it is being drawn.

lime_add_module(
	NAME LimeRenderGraph
	SOURCES
		RenderGraphTypes.h
		RenderGraphTypes.cpp
		RenderGraphPassType.h
		RenderGraphPassType.cpp
		RenderGraphDesc.h
		RenderGraphDesc.cpp
		RenderGraphCompiler.h
		RenderGraphCompiler.cpp
		RenderGraphJson.h
		RenderGraphJson.cpp
		RenderGraphLayout.h
		RenderGraphLayout.cpp
	PUBLIC_DEPS
		LimeCore
		LimeRHI
	FOLDER "Engine/Runtime"
)
