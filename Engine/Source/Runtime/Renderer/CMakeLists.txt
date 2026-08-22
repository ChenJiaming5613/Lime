# LimeRenderer: frame orchestration, render passes and the NVRHI based ImGui backend.

lime_add_module(
	NAME LimeRenderer
	SOURCES
		RenderTypes.h
		Renderer.h
		Renderer.cpp
		ImGui/ImGuiRenderer.h
		ImGui/ImGuiRenderer.cpp
		Passes/TrianglePass.h
		Passes/TrianglePass.cpp
	PUBLIC_DEPS
		LimeRHI
		imgui
	FOLDER "Engine/Runtime"
)
