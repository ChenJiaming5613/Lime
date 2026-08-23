# LimeEditor: editor layer, dock layout, panel registry and built-in panels.
# Optional via LIME_BUILD_EDITOR.

set(EditorSources
	EditorContext.h
	EditorLayer.h
	EditorLayer.cpp
	EditorPanelRegistry.h
	EditorPanelRegistry.cpp
	EditorSettings.h
	EditorSettings.cpp
	PropertyDrawer.h
	PropertyDrawer.cpp
	Panels/ConsolePanel.h
	Panels/ConsolePanel.cpp
	Panels/EditorPanel.h
	Panels/EditorSettingsPanel.h
	Panels/EditorSettingsPanel.cpp
	Panels/InspectorPanel.h
	Panels/InspectorPanel.cpp
	Panels/ProjectSettingsPanel.h
	Panels/ProjectSettingsPanel.cpp
	Panels/StatsPanel.h
	Panels/StatsPanel.cpp
	Panels/ViewportPanel.h
	Panels/ViewportPanel.cpp
)

set(EditorPublicDeps LimeRenderer imgui)

# UI automation. Kept out of the source list entirely when disabled, so the module carries no trace
# of a dependency it cannot use.
if(LIME_BUILD_IMGUI_TEST_ENGINE)
	list(APPEND EditorSources
		TestEngine/EditorTestEngine.h
		TestEngine/EditorTestEngine.cpp
		TestEngine/UITestRegistry.h
		TestEngine/UITestRegistry.cpp
		TestEngine/BuiltinUITests.cpp
	)
	list(APPEND EditorPublicDeps imgui_test_engine)
endif()

lime_add_module(
	NAME LimeEditor
	SOURCES ${EditorSources}
	PUBLIC_DEPS ${EditorPublicDeps}
	FOLDER "Engine/Editor"
)

# PUBLIC so the automation module can compile out its uitest commands the same way.
if(LIME_BUILD_IMGUI_TEST_ENGINE)
	target_compile_definitions(LimeEditor PUBLIC LIME_WITH_IMGUI_TEST_ENGINE=1)
else()
	target_compile_definitions(LimeEditor PUBLIC LIME_WITH_IMGUI_TEST_ENGINE=0)
endif()

target_include_directories(LimeEditor PUBLIC "${LIME_ENGINE_DIR}/Source")
