# LimeEditor: editor layer, dock layout and panels. Optional via LIME_BUILD_EDITOR.

lime_add_module(
	NAME LimeEditor
	SOURCES
		EditorContext.h
		EditorLayer.h
		EditorLayer.cpp
		Panels/ConsolePanel.h
		Panels/ConsolePanel.cpp
		Panels/EditorPanel.h
		Panels/InspectorPanel.h
		Panels/InspectorPanel.cpp
	PUBLIC_DEPS
		LimeRenderer
		imgui
	FOLDER "Engine/Editor"
)

target_include_directories(LimeEditor PUBLIC "${LIME_ENGINE_DIR}/Source")
