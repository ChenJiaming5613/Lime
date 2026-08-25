# Catch2 based unit tests.

file(GLOB_RECURSE TestSources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(NOT TestSources)
	return()
endif()

# LimePlatform and LimeCamera are always built, so they need no guard. Only value logic is exercised
# here: the camera produces matrices and the fly controller is a pure function, both testable without a
# window or a device.
set(TestDeps LimeCompilerOptions LimeCore LimePlatform LimeCamera)
if(TARGET LimeRHI)
	list(APPEND TestDeps LimeRHI)
endif()
# Needed for the pass ordering tests; the registry itself has no GPU dependency.
if(TARGET LimeRenderer)
	list(APPEND TestDeps LimeRenderer)
endif()
# Asset import is pure value logic with no GPU dependency, which is why it is unit tested at all.
if(TARGET LimeAsset)
	list(APPEND TestDeps LimeAsset)
else()
	list(REMOVE_ITEM TestSources
		"${CMAKE_CURRENT_SOURCE_DIR}/Asset/AsyncGltfLoaderTests.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/Asset/DdsLoaderTests.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/Asset/GltfImporterTests.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/Asset/GltfSampleAssetTests.cpp"
	)
endif()
# The frame graph is plain data with no GPU or UI dependency, so the model, its JSON form and the
# layering algorithm are all covered here. LimeFrameGraph is always built, but the guard keeps the
# pattern uniform and would catch the module being made optional later.
if(TARGET LimeFrameGraph)
	list(APPEND TestDeps LimeFrameGraph)
else()
	list(REMOVE_ITEM TestSources
		"${CMAKE_CURRENT_SOURCE_DIR}/FrameGraph/FrameGraphDescTests.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/FrameGraph/FrameGraphExampleTests.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/FrameGraph/FrameGraphJsonTests.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/FrameGraph/FrameGraphLayoutTests.cpp"
	)
endif()
# The scene graph itself holds no GPU objects, so the hierarchy and transform logic is testable here.
# Guarded because LimeScene depends on LimeRenderer, which only exists when an RHI backend is enabled.
if(TARGET LimeScene)
	list(APPEND TestDeps LimeScene)
else()
	list(REMOVE_ITEM TestSources "${CMAKE_CURRENT_SOURCE_DIR}/Scene/SceneTests.cpp")
endif()
# Project settings live in LimeRuntime; only their value mapping is exercised, not the engine loop.
if(TARGET LimeRuntime)
	list(APPEND TestDeps LimeRuntime)
endif()
# Editor settings are value logic too, but the target only exists when the editor is built.
if(TARGET LimeEditor)
	list(APPEND TestDeps LimeEditor)
else()
	list(REMOVE_ITEM TestSources "${CMAKE_CURRENT_SOURCE_DIR}/Editor/EditorSettingsTests.cpp")
endif()
# The reflection bridge and script discovery are pure value logic, so they are tested without
# starting a server.
if(TARGET LimeAutomation)
	list(APPEND TestDeps LimeAutomation)
else()
	list(REMOVE_ITEM TestSources
		"${CMAKE_CURRENT_SOURCE_DIR}/Automation/AutomationReflectionTests.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/Automation/AutomationScriptTests.cpp"
	)
endif()

add_executable(LimeTests ${TestSources})
target_link_libraries(LimeTests PRIVATE ${TestDeps} Catch2::Catch2WithMain)
set_target_properties(LimeTests PROPERTIES FOLDER "Tests")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${TestSources})

include(Catch)
catch_discover_tests(LimeTests)
