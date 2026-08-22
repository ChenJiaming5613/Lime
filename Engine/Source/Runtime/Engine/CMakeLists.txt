# LimeRuntime: engine loop and the application interface projects implement.

set(RuntimeDeps LimeRenderer tinygltf)
if(LIME_BUILD_EDITOR)
	list(APPEND RuntimeDeps LimeEditor)
endif()

lime_add_module(
	NAME LimeRuntime
	SOURCES
		ApplicationInterface.h
		Engine.h
		Engine.cpp
		EngineConfig.h
		EngineConfig.cpp
	PUBLIC_DEPS ${RuntimeDeps}
	FOLDER "Engine/Runtime"
)

add_dependencies(LimeRuntime LimeShaders)
