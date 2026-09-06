# LimeRenderGraph: the pass dependency graph as plain data, plus its JSON form and its layout.
#
# Also defines the pass abstraction itself. The base class belongs here because this module is what calls
# every one of its methods: a pass declares its resources in this vocabulary, is compiled against sizes
# this compiler negotiated, and runs in an order this module produced.
#
# The passes themselves live in LimeRenderer, including the generic ones the engine injects. Defining the
# interface here and the implementations there is what keeps the dependency running one way.
#
# Has no UI dependency, for the same reason LimeAsset has none: the graph, its validation and the
# layering algorithm are all decidable from data alone, so they can be covered by unit tests with no
# ImGui context. The injection is data too, and is tested the same way.
#
# Depends on LimeRHI only for nvrhi::Format and the pure functions over it. That is not a GPU
# dependency: no device is needed to describe a resource, and the test binary already links LimeRHI, so
# the validation and layout tests still run on a machine without a graphics device. Describing formats
# in the RHI's own vocabulary is worth more than a private enum plus a translation table that could
# drift from it.
#
# Still no dependency on LimeRenderer, which is what the pass base class has to preserve. FRenderer
# travels through Initialize and Compile as a forward declared reference, so only the passes that
# dereference it take that dependency on.
#
# The editor panel that draws this lives in LimeEditor. Nothing here knows that it is being drawn.

lime_add_module(
	NAME LimeRenderGraph
	SOURCES
		RenderGraphTypes.h
		RenderGraphTypes.cpp
		RenderGraphPassType.h
		RenderGraphPassType.cpp
		RenderGraphPass.h
		RenderGraphDesc.h
		RenderGraphDesc.cpp
		RenderGraphCompiler.h
		RenderGraphCompiler.cpp
		RenderGraphInjection.h
		RenderGraphInjection.cpp
		RenderGraphJson.h
		RenderGraphJson.cpp
		RenderGraphLayout.h
		RenderGraphLayout.cpp
	PUBLIC_DEPS
		LimeCore
		LimeRHI
	FOLDER "Engine/Runtime"
)
