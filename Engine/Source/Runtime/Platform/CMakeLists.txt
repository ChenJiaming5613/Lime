# LimePlatform: windowing, input, timing and path resolution.

lime_add_module(
	NAME LimePlatform
	SOURCES
		Input.h
		Input.cpp
		PlatformPaths.h
		PlatformPaths.cpp
		PlatformTime.h
		PlatformTime.cpp
		Window.h
		Window.cpp
	PUBLIC_DEPS
		LimeCore
	PRIVATE_DEPS
		glfw
	FOLDER "Engine/Runtime"
)

# Lets a development build resolve Engine/Shaders and Engine/Content from the source tree.
target_compile_definitions(LimePlatform PRIVATE LIME_SOURCE_DIR="${LIME_ROOT_DIR}")
