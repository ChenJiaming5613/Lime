# Engine build script: built-in shader compilation plus the runtime and editor modules.

lime_compile_shaders(
	TARGET LimeShaders
	CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/LimeShaders.cfg"
	OUTPUT_DIR "${LIME_SHADER_OUTPUT_DIR}"
	INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Include"
)

add_subdirectory(Source/Runtime/Core)
add_subdirectory(Source/Runtime/Platform)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/RHI/CMakeLists.txt")
	add_subdirectory(Source/Runtime/RHI)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/Renderer/CMakeLists.txt")
	add_subdirectory(Source/Runtime/Renderer)
endif()

if(LIME_BUILD_EDITOR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Editor/CMakeLists.txt")
	add_subdirectory(Source/Editor)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime/Engine/CMakeLists.txt")
	add_subdirectory(Source/Runtime/Engine)
endif()
