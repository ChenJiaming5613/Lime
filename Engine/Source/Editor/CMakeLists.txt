# LimeEditor: editor layer, dock layout, panel registry and built-in panels.
# Optional via LIME_BUILD_EDITOR.

set(EditorSources
	EditorContext.h
	EditorLayer.h
	EditorLayer.cpp
	EditorPanelRegistry.h
	EditorPanelRegistry.cpp
	EditorSelection.h
	EditorSelection.cpp
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
	Panels/SceneHierarchyPanel.h
	Panels/SceneHierarchyPanel.cpp
	Panels/StatsPanel.h
	Panels/StatsPanel.cpp
	Panels/ViewportPanel.h
	Panels/ViewportPanel.cpp
)

# LimeRenderer brings LimeScene with it, which the hierarchy panel reads.
set(EditorPublicDeps LimeRenderer imgui)

# Render graph editor. Optional because it carries the node editor submodule, which a build without it
# should not need.
if(LIME_BUILD_NODE_EDITOR)
	list(APPEND EditorSources
		RenderGraph/RenderGraphNodeView.h
		RenderGraph/RenderGraphNodeView.cpp
		RenderGraph/RenderGraphPanel.h
		RenderGraph/RenderGraphPanel.cpp
	)
	# LimeRenderGraph is the data the panel draws; the node widget is how it draws it.
	list(APPEND EditorPublicDeps imgui_node_editor LimeRenderGraph)
endif()

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

if(LIME_BUILD_NODE_EDITOR)
	target_compile_definitions(LimeEditor PUBLIC LIME_WITH_NODE_EDITOR=1)
else()
	target_compile_definitions(LimeEditor PUBLIC LIME_WITH_NODE_EDITOR=0)
endif()

target_include_directories(LimeEditor PUBLIC "${LIME_ENGINE_DIR}/Source")
