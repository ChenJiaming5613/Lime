# Third party integration. Every library is a submodule; nothing is downloaded at configure time.

include(FetchContent)

# Older third party projects declare a pre-3.10 policy floor that CMake 4.x rejects.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_FOLDER "ThirdParty")

# Third party sources are UTF-8 but MSVC defaults to the system code page, which produces C4819.
if(MSVC)
	add_compile_options(/utf-8)
endif()

# ---------------------------------------------------------------------------------------
# Vulkan-Headers. Provides the Vulkan::Headers target consumed by NVRHI and by LimeRHI.
# The Vulkan loader is resolved at runtime through vulkan-hpp's DynamicLoader, so nothing
# links vulkan-1.lib and a machine without a Vulkan runtime can still run the D3D12 backend.
# ---------------------------------------------------------------------------------------
if(LIME_ENABLE_RHI_VULKAN)
	add_subdirectory(VulkanHeaders EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------------------
# DirectX-Headers. Required because the installed Windows SDK predates 10.0.22621.0.
# ---------------------------------------------------------------------------------------
if(LIME_ENABLE_RHI_D3D12)
	set(DXHEADERS_BUILD_TEST OFF CACHE BOOL "" FORCE)
	set(DXHEADERS_BUILD_GOOGLE_TEST OFF CACHE BOOL "" FORCE)
	set(DXHEADERS_INSTALL OFF CACHE BOOL "" FORCE)
	add_subdirectory(DirectXHeaders EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------------------
# NVRHI. Static targets: nvrhi, nvrhi_d3d12, nvrhi_vk.
# ---------------------------------------------------------------------------------------
set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX12 ${LIME_ENABLE_RHI_D3D12} CACHE BOOL "" FORCE)
set(NVRHI_WITH_VULKAN ${LIME_ENABLE_RHI_VULKAN} CACHE BOOL "" FORCE)
set(NVRHI_WITH_RTXMU OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_NVAPI OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_AFTERMATH OFF CACHE BOOL "" FORCE)
set(NVRHI_FETCH_VULKAN_HEADERS OFF CACHE BOOL "" FORCE)
set(NVRHI_FETCH_DIRECTX_HEADERS OFF CACHE BOOL "" FORCE)
add_subdirectory(NVRHI)

# ---------------------------------------------------------------------------------------
# ShaderMake. Compiles the engine shader tree; ShaderMakeBlob reads permutation blobs.
# ---------------------------------------------------------------------------------------
lime_configure_shadermake_options()
add_subdirectory(ShaderMake)
lime_validate_shader_compiler()

# ---------------------------------------------------------------------------------------
# GLFW.
# ---------------------------------------------------------------------------------------
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(GLFW EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------------------
# spdlog. Static library mode; the header only path would bloat compile times.
# ---------------------------------------------------------------------------------------
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "" FORCE)
add_subdirectory(SpdLog EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------------------
# Dear ImGui (docking branch). Core plus the GLFW platform backend only; the renderer
# backend is implemented on top of NVRHI so D3D12 and Vulkan share one code path.
# ---------------------------------------------------------------------------------------
add_library(imgui STATIC
	ImGui/imgui.cpp
	ImGui/imgui.h
	ImGui/imgui_draw.cpp
	ImGui/imgui_tables.cpp
	ImGui/imgui_widgets.cpp
	ImGui/imgui_demo.cpp
	ImGui/imgui_internal.h
	ImGui/misc/cpp/imgui_stdlib.cpp
	ImGui/misc/cpp/imgui_stdlib.h
	ImGui/backends/imgui_impl_glfw.cpp
	ImGui/backends/imgui_impl_glfw.h
)
target_include_directories(imgui SYSTEM PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/ImGui
	${CMAKE_CURRENT_SOURCE_DIR}/ImGui/backends
	${CMAKE_CURRENT_SOURCE_DIR}/ImGui/misc/cpp
)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(imgui PUBLIC glfw)

# ---------------------------------------------------------------------------------------
# tinygltf. tiny_gltf.cc is the vendored single implementation translation unit.
# ---------------------------------------------------------------------------------------
add_library(tinygltf STATIC
	TinyGLTF/tiny_gltf.cc
	TinyGLTF/tiny_gltf.h
)
target_include_directories(tinygltf SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/TinyGLTF)
if(MSVC)
	target_compile_options(tinygltf PRIVATE /W0)
endif()

# ---------------------------------------------------------------------------------------
# EnTT. Header only; provides the runtime reflection (meta) system used to drive the
# inspector UI and settings serialization, and the ECS for later use.
# Pinned to v3.16.0: the meta API changed between releases (entt::meta<T>() in 3.13 and
# earlier versus entt::meta_factory<T>{} from 3.15 on), so the version is not free to move.
# ---------------------------------------------------------------------------------------
add_library(entt INTERFACE)
target_include_directories(entt SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/EnTT/src)

# ---------------------------------------------------------------------------------------
# nlohmann/json. Header only, used for ProjectSettings.json. Deliberately a separate
# submodule rather than reusing the copy vendored inside tinygltf, which is an internal
# detail of that library and lags behind.
# ---------------------------------------------------------------------------------------
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Json/include)

# ---------------------------------------------------------------------------------------
# cpp-httplib. Header only, used by the automation server. Replaces hand written socket
# code, so that module contains no platform specific networking at all.
#
# Deliberately not add_subdirectory: its CMakeLists builds an install/package configuration
# and probes for OpenSSL, zlib and Brotli. None of that is wanted here, since the server is
# plain HTTP on the loopback interface. An INTERFACE target over the single header is the
# whole integration.
#
# CPPHTTPLIB_NO_EXCEPTIONS is not set: the engine already relies on exceptions for JSON
# parsing, and httplib uses them to report malformed requests.
# ---------------------------------------------------------------------------------------
if(LIME_BUILD_AUTOMATION)
	add_library(httplib INTERFACE)
	target_include_directories(httplib SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/HttpLib)
	# The header pulls in <windows.h>; without this it defines the min/max macros that break
	# std::min and std::max, and drags in a large amount of unrelated API surface.
	if(WIN32)
		target_compile_definitions(httplib INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX)
	endif()
	# Threads are required on POSIX; on Windows this resolves to a no-op.
	find_package(Threads REQUIRED)
	target_link_libraries(httplib INTERFACE Threads::Threads)
endif()

# ---------------------------------------------------------------------------------------
# Catch2.
# ---------------------------------------------------------------------------------------
if(LIME_BUILD_TESTS)
	set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
	set(CATCH_INSTALL_EXTRAS ON CACHE BOOL "" FORCE)
	set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
	set(CATCH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	add_subdirectory(Catch2 EXCLUDE_FROM_ALL)
	list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Catch2/extras")
	set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)
endif()

# ---------------------------------------------------------------------------------------
# Keep third party warnings out of our build log and group the targets in the IDE.
# ---------------------------------------------------------------------------------------
lime_mark_system_includes(nvrhi nvrhi_d3d12 nvrhi_vk glfw spdlog imgui tinygltf ShaderMakeBlob)
lime_set_third_party_folder(
	nvrhi nvrhi_d3d12 nvrhi_vk
	glfw spdlog imgui tinygltf
	ShaderMake ShaderMakeBlob
	DirectX-Headers DirectX-Guids
	Catch2 Catch2WithMain
	update_mappings
)
