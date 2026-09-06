# LimeRenderer: frame orchestration, the pass registry and every pass the engine provides.
#
# That includes the NVRHI based ImGui backend, which is a pass like any other: the engine injects it into
# the graph in editor mode rather than running it as a separate stage. The blit sits alongside it for the
# same reason — both are engine owned passes the graph file never names.
#
# Depends on LimeScene and LimeCamera because the built-in passes draw a scene through a camera. The
# dependency only runs this way: LimeScene knows nothing about passes, which is what keeps the two from
# forming a cycle.
#
# Depends on LimeRenderGraph because a pass declares its own inputs and outputs in that vocabulary, and
# because the frame is driven by a compiled graph. The dependency only runs this way as well: the graph
# defines the pass interface but knows nothing about the renderer or any concrete pass.

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
		Passes/SkyboxPass.h
		Passes/SkyboxPass.cpp
		Passes/BlitPass.h
		Passes/BlitPass.cpp
		Passes/EditorUIPass.h
		Passes/EditorUIPass.cpp
		Passes/BuiltinPasses.h
		Passes/BuiltinPasses.cpp
	PUBLIC_DEPS
		LimeRHI
		LimeCamera
		LimeScene
		LimeRenderGraph
		imgui
	# stb_image comes from the tinygltf target, which already compiles its implementation. The skybox pass
	# includes only the declarations and decodes the HDR through that single copy.
	PRIVATE_DEPS tinygltf
	FOLDER "Engine/Runtime"
)
