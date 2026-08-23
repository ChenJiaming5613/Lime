# LimeRenderer: frame orchestration, the pass registry, the built-in passes and the NVRHI based ImGui
# backend.
#
# Depends on LimeScene and LimeCamera because the built-in passes draw a scene through a camera. The
# dependency only runs this way: LimeScene knows nothing about passes, which is what keeps the two from
# forming a cycle.

lime_add_module(
	NAME LimeRenderer
	SOURCES
		RenderTypes.h
		RenderPassRegistry.h
		RenderPassRegistry.cpp
		Renderer.h
		Renderer.cpp
		ViewportTarget.h
		ViewportTarget.cpp
		Passes/BlinnPhongForwardPass.h
		Passes/BlinnPhongForwardPass.cpp
		ImGui/ImGuiRenderer.h
		ImGui/ImGuiRenderer.cpp
	PUBLIC_DEPS
		LimeRHI
		LimeCamera
		LimeScene
		imgui
	FOLDER "Engine/Runtime"
)
