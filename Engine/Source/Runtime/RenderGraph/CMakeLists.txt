# LimeRenderGraph: the pass dependency graph as plain data, plus its JSON form and its layout.
#
# Deliberately has no GPU and no UI dependency, for the same reason LimeAsset has none: the graph, its
# validation and the layering algorithm are all decidable from data alone, so they can be covered by
# unit tests on a machine with no graphics device and with no ImGui context.
#
# The editor panel that draws this lives in LimeEditor. Nothing here knows that it is being drawn.

lime_add_module(
	NAME LimeRenderGraph
	SOURCES
		RenderGraphTypes.h
		RenderGraphPassType.h
		RenderGraphPassType.cpp
		RenderGraphDesc.h
		RenderGraphDesc.cpp
		RenderGraphJson.h
		RenderGraphJson.cpp
		RenderGraphLayout.h
		RenderGraphLayout.cpp
	PUBLIC_DEPS
		LimeCore
	FOLDER "Engine/Runtime"
)
