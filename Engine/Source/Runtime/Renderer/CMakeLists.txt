# LimeRenderer: frame orchestration, the pass registry, the built-in passes and the NVRHI based ImGui
# backend.
#
# Depends on LimeScene and LimeCamera because the built-in passes draw a scene through a camera. The
# dependency only runs this way: LimeScene knows nothing about passes, which is what keeps the two from
# forming a cycle.
#
# Depends on LimeRenderGraph because a pass declares its own inputs and outputs in that vocabulary, and
# because the frame is driven by a compiled graph. The dependency only runs this way as well: the graph
# knows nothing about the renderer.

lime_add_module(
	NAME LimeRenderer
	SOURCES
		RenderTypes.h
		RenderPassRegistry.h
		RenderPassRegistry.cpp
		Renderer.h
		Renderer.cpp
		RenderGraphResources.h
		RenderGraphResources.cpp
		RenderGraphExecutor.h
		RenderGraphExecutor.cpp
		ViewportTarget.h
		ViewportTarget.cpp
		Passes/BlinnPhongForwardLitPass.h
		Passes/BlinnPhongForwardLitPass.cpp
		Passes/ShadowCasterPass.h
		Passes/ShadowCasterPass.cpp
		Passes/PostProcessPass.h
		Passes/PostProcessPass.cpp
		Passes/DebugVisualizerPass.h
		Passes/DebugVisualizerPass.cpp
		Passes/BuiltinPasses.h
		Passes/BuiltinPasses.cpp
		ImGui/EditorUIPass.h
		ImGui/EditorUIPass.cpp
	PUBLIC_DEPS
		LimeRHI
		LimeCamera
		LimeScene
		LimeRenderGraph
		imgui
	FOLDER "Engine/Runtime"
)
