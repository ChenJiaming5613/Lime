# LimeEditor: editor layer, dock layout, panel registry and built-in panels.
# Optional via LIME_BUILD_EDITOR.

lime_add_module(
	NAME LimeEditor
	SOURCES
		EditorContext.h
		EditorLayer.h
		EditorLayer.cpp
		EditorPanelRegistry.h
		EditorPanelRegistry.cpp
		PropertyDrawer.h
		PropertyDrawer.cpp
		Panels/ConsolePanel.h
		Panels/ConsolePanel.cpp
		Panels/EditorPanel.h
		Panels/InspectorPanel.h
		Panels/InspectorPanel.cpp
		Panels/ProjectSettingsPanel.h
		Panels/ProjectSettingsPanel.cpp
		Panels/StatsPanel.h
		Panels/StatsPanel.cpp
		Panels/ViewportPanel.h
		Panels/ViewportPanel.cpp
	PUBLIC_DEPS
		LimeRenderer
		imgui
	FOLDER "Engine/Editor"
)

target_include_directories(LimeEditor PUBLIC "${LIME_ENGINE_DIR}/Source")
