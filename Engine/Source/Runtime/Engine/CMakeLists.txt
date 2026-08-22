# LimeRuntime: engine loop, project settings and the optional application hooks.

set(RuntimeDeps LimeRenderer tinygltf)
if(LIME_BUILD_EDITOR)
	list(APPEND RuntimeDeps LimeEditor)
endif()
if(LIME_BUILD_AUTOMATION)
	list(APPEND RuntimeDeps LimeAutomation)
endif()

lime_add_module(
	NAME LimeRuntime
	SOURCES
		ApplicationInterface.h
		ApplicationInterface.cpp
		Engine.h
		Engine.cpp
		ProjectSettings.h
		ProjectSettings.cpp
	PUBLIC_DEPS ${RuntimeDeps}
	FOLDER "Engine/Runtime"
)

add_dependencies(LimeRuntime LimeShaders)
