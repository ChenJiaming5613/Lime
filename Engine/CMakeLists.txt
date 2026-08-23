# Engine build script: built-in shader compilation plus the runtime and editor modules.

lime_compile_shaders(
	TARGET LimeShaders
	CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/LimeShaders.cfg"
	OUTPUT_DIR "${LIME_SHADER_OUTPUT_DIR}"
	INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Include"
)

add_subdirectory(Source/Runtime/Core)
add_subdirectory(Source/Runtime/Platform)
# Cameras need input from Platform but nothing graphics related, so they come before the RHI.
add_subdirectory(Source/Runtime/Camera)
# Asset import needs only Core, so it can be built before anything graphics related.
add_subdirectory(Source/Runtime/Asset)
add_subdirectory(Source/Runtime/RHI)
# The scene uploads through the RHI but knows nothing about passes, so it precedes the renderer.
add_subdirectory(Source/Runtime/Scene)
# Its built-in passes draw a scene through a camera, so it comes after both.
add_subdirectory(Source/Runtime/Renderer)

if(LIME_BUILD_EDITOR)
	add_subdirectory(Source/Editor)
endif()

# Depends on the editor when it is enabled, so it comes after it.
if(LIME_BUILD_AUTOMATION)
	add_subdirectory(Source/Runtime/Automation)
endif()

add_subdirectory(Source/Runtime/Engine)
# Depends on LimeRuntime, so it comes last.
add_subdirectory(Source/Runtime/Launch)
