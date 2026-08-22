# HelloTriangle: rotating triangle plus the editor UI. Console subsystem so logs stay visible.

lime_add_executable(
	NAME HelloTriangle
	SOURCES
		Source/Main.cpp
		Source/HelloTriangleApp.h
		Source/HelloTriangleApp.cpp
	DEPS LimeRuntime
	FOLDER "Projects"
)

add_dependencies(HelloTriangle LimeShaders)

# Built-in content is copied next to the executable so it runs without the source tree.
add_custom_command(TARGET HelloTriangle POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${LIME_ENGINE_DIR}/Content" "$<TARGET_FILE_DIR:HelloTriangle>/Content"
	COMMENT "Copying engine content"
	VERBATIM
)

set_property(DIRECTORY "${LIME_ROOT_DIR}" PROPERTY VS_STARTUP_PROJECT HelloTriangle)
