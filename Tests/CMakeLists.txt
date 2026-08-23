# Catch2 based unit tests.

file(GLOB_RECURSE TestSources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

if(NOT TestSources)
	return()
endif()

set(TestDeps LimeCompilerOptions LimeCore)
if(TARGET LimeRHI)
	list(APPEND TestDeps LimeRHI)
endif()
# Needed for the pass ordering tests; the registry itself has no GPU dependency.
if(TARGET LimeRenderer)
	list(APPEND TestDeps LimeRenderer)
endif()
# glTF import is pure value logic with no GPU dependency, which is why it is unit tested at all.
if(TARGET LimeAsset)
	list(APPEND TestDeps LimeAsset)
else()
	list(REMOVE_ITEM TestSources
		"${CMAKE_CURRENT_SOURCE_DIR}/Asset/GltfImporterTests.cpp"
		"${CMAKE_CURRENT_SOURCE_DIR}/Asset/GltfSampleAssetTests.cpp"
	)
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
