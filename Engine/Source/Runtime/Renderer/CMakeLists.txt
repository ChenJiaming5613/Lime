# LimeRenderer: frame orchestration, the pass registry and the NVRHI based ImGui backend.

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
		ImGui/ImGuiRenderer.h
		ImGui/ImGuiRenderer.cpp
	PUBLIC_DEPS
		LimeRHI
		imgui
	FOLDER "Engine/Runtime"
)
