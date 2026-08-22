# LimeEngine root build script. Windows x64 only.

cmake_minimum_required(VERSION 3.24)

# Several third party projects still declare an old policy floor; keep them configurable on CMake 4.x.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
	set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
endif()

project(LimeEngine VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
	message(FATAL_ERROR "LimeEngine currently targets Windows only.")
endif()
if(CMAKE_SIZEOF_VOID_P LESS 8)
	message(FATAL_ERROR "LimeEngine requires a 64-bit toolchain (x64).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(LIME_BUILD_EDITOR "Build the editor module" ON)
option(LIME_BUILD_TESTS "Build unit tests" ON)
# Debug only by default: the automation server exposes full engine state over a local socket, which
# is a development facility rather than something to ship.
option(LIME_BUILD_AUTOMATION "Build the automation server used by external test scripts" ON)
option(LIME_ENABLE_RHI_D3D12 "Enable the Direct3D 12 backend" ON)
option(LIME_ENABLE_RHI_VULKAN "Enable the Vulkan backend" ON)

if(NOT LIME_ENABLE_RHI_D3D12 AND NOT LIME_ENABLE_RHI_VULKAN)
	message(FATAL_ERROR "At least one RHI backend must be enabled.")
endif()

# Single source of truth for HLSL register shifts on Vulkan.
# These values are fed to DXC (-fvk-*-shift) and to nvrhi::VulkanBindingOffsets at device creation.
# Both sides must agree or descriptor bindings will be mismatched.
set(LIME_VK_SHIFT_SRV 0 CACHE STRING "Vulkan binding offset for HLSL t registers")
set(LIME_VK_SHIFT_SAMPLER 128 CACHE STRING "Vulkan binding offset for HLSL s registers")
set(LIME_VK_SHIFT_CBV 256 CACHE STRING "Vulkan binding offset for HLSL b registers")
set(LIME_VK_SHIFT_UAV 384 CACHE STRING "Vulkan binding offset for HLSL u registers")

# All binaries land next to each other so shaders and content resolve relative to the executable.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Bin/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/Lib/$<CONFIG>")

set(LIME_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(LIME_ENGINE_DIR "${LIME_ROOT_DIR}/Engine")
set(LIME_THIRD_PARTY_DIR "${LIME_ROOT_DIR}/ThirdParty")

# Include roots shared by every engine module, so headers read as "Core/...", "RHI/...", "Editor/...".
set(LIME_ENGINE_INCLUDE_DIRS
	"${LIME_ENGINE_DIR}/Source"
	"${LIME_ENGINE_DIR}/Source/Runtime"
)

# Compiled shader blobs and copied content are emitted beside the executable.
set(LIME_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/Bin/$<CONFIG>/Shaders")
set(LIME_CONTENT_OUTPUT_DIR "${CMAKE_BINARY_DIR}/Bin/$<CONFIG>/Content")

list(APPEND CMAKE_MODULE_PATH "${LIME_ROOT_DIR}/CMake")
include(LimeCompilerOptions)
include(LimeTargetHelpers)
include(LimeShaderCompile)
include(LimeProject)

if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
	message(STATUS "Windows SDK: ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}")
endif()

add_subdirectory(ThirdParty)
add_subdirectory(Engine)
add_subdirectory(Projects)

if(LIME_BUILD_TESTS)
	enable_testing()
	add_subdirectory(Tests)
	# Python suite driving a real engine over the automation server. Registered after Tests so the C++
	# tests, which need no GPU, run first.
	add_subdirectory(Automation)
endif()

message(STATUS "LimeEngine ${PROJECT_VERSION} | Editor: ${LIME_BUILD_EDITOR} | Tests: ${LIME_BUILD_TESTS} | Automation: ${LIME_BUILD_AUTOMATION} | D3D12: ${LIME_ENABLE_RHI_D3D12} | Vulkan: ${LIME_ENABLE_RHI_VULKAN}")
