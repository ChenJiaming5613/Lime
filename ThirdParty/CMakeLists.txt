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
# Dear ImGui Test Engine. Drives the UI from an end user's point of view by injecting
# input events, which is what lets a test click and drag real widgets.
#
# IMGUI_ENABLE_TEST_ENGINE changes the layout of ImGuiContext, so it has to be PUBLIC on
# the imgui target itself: a module that saw a different layout than imgui.cpp would read
# the wrong offsets, and the resulting corruption would be silent and hard to trace.
#
# LICENSING: the imgui_test_engine directory is NOT MIT. It is free for individuals,
# education, open source and small companies; larger companies need a paid license. See
# ImGuiTestEngine/imgui_test_engine/LICENSE.txt before shipping commercially.
# ---------------------------------------------------------------------------------------
if(LIME_BUILD_IMGUI_TEST_ENGINE)
	target_compile_definitions(imgui PUBLIC
		IMGUI_ENABLE_TEST_ENGINE
		# The engine ships a std::thread based coroutine implementation. A test reads as
		# straight line code while its actions span many frames, and the coroutine is what
		# suspends it between them.
		IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
		# Tests are registered with std::function so a project can capture state in a lambda.
		IMGUI_TEST_ENGINE_ENABLE_STD_FUNCTION=1
		# Screen capture is out of scope here and would pull in stb_image_write and ffmpeg.
		# The engine already guards every capture path behind this flag.
		IMGUI_TEST_ENGINE_ENABLE_CAPTURE=0
		# Routes ImGui's own assertions through the test engine instead of the CRT.
		#
		# This matters far more than it looks. The default IM_ASSERT calls abort(), which on
		# Windows opens a modal dialog. That dialog blocks the main thread, so the frame loop
		# stops, the automation server never answers again and every command times out with
		# "Timed out waiting for the main thread" - a message that says nothing about the real
		# cause. Worse, the test engine's own watchdog cannot fire either, because it is driven
		# by test time that only advances while frames are running.
		#
		# The test engine's macro logs the failing expression with the running test's name and
		# then breaks only under a debugger, so a headless run reports the failure and keeps
		# going.
		#
		# imgui_te_imconfig.h has to arrive through IMGUI_USER_CONFIG rather than
		# IMGUI_USER_H_FILENAME: imgui.h includes the former at the top, before its default
		# IM_ASSERT is defined, and the latter only at the very end, far too late to override
		# anything. It also declares ImGuiTestEngine_AssertLog, which the macro calls.
		IMGUI_USER_CONFIG="imgui_test_engine/imgui_te_imconfig.h"
		IM_ASSERT=IM_TEST_ENGINE_ASSERT
	)
	# imgui.h now includes a test engine header, so the directory holding it has to be visible
	# to imgui itself and to everything that includes imgui.h. This is added to the imgui target
	# rather than only to imgui_test_engine, which would create a circular dependency.
	target_include_directories(imgui SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ImGuiTestEngine)

	add_library(imgui_test_engine STATIC
		# The capture tool and perf tool are compiled in even though neither feature is used here:
		# imgui_te_engine.cpp constructs ImGuiCaptureToolUI and ImGuiPerfTool unconditionally.
		# IMGUI_TEST_ENGINE_ENABLE_CAPTURE only guards the code that saves images, not the types.
		ImGuiTestEngine/imgui_test_engine/imgui_capture_tool.cpp
		ImGuiTestEngine/imgui_test_engine/imgui_capture_tool.h
		ImGuiTestEngine/imgui_test_engine/imgui_te_context.cpp
		ImGuiTestEngine/imgui_test_engine/imgui_te_context.h
		ImGuiTestEngine/imgui_test_engine/imgui_te_coroutine.cpp
		ImGuiTestEngine/imgui_test_engine/imgui_te_coroutine.h
		ImGuiTestEngine/imgui_test_engine/imgui_te_engine.cpp
		ImGuiTestEngine/imgui_test_engine/imgui_te_engine.h
		ImGuiTestEngine/imgui_test_engine/imgui_te_exporters.cpp
		ImGuiTestEngine/imgui_test_engine/imgui_te_exporters.h
		ImGuiTestEngine/imgui_test_engine/imgui_te_internal.h
		ImGuiTestEngine/imgui_test_engine/imgui_te_perftool.cpp
		ImGuiTestEngine/imgui_test_engine/imgui_te_perftool.h
		ImGuiTestEngine/imgui_test_engine/imgui_te_ui.cpp
		ImGuiTestEngine/imgui_test_engine/imgui_te_ui.h
		ImGuiTestEngine/imgui_test_engine/imgui_te_utils.cpp
		ImGuiTestEngine/imgui_test_engine/imgui_te_utils.h
	)
	# Str.h is included unqualified by the engine sources, so its directory has to be on the
	# include path rather than reached through a relative path.
	target_include_directories(imgui_test_engine SYSTEM PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/ImGuiTestEngine
		${CMAKE_CURRENT_SOURCE_DIR}/ImGuiTestEngine/imgui_test_engine/thirdparty/Str
	)
	target_link_libraries(imgui_test_engine PUBLIC imgui)
	# These sources define IMGUI_DEFINE_MATH_OPERATORS themselves while the imgui target already
	# passes it on the command line. The definitions agree, so the redefinition warning is noise.
	if(MSVC)
		target_compile_options(imgui_test_engine PRIVATE /wd4005)
	endif()
endif()

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
# imgui-node-editor. Node graph editing widget, shown through the editor's node graph panel.
#
# Only the library is built. The upstream examples ship their own Application framework with
# DX11 and OpenGL backends and their own window handling, none of which is usable here: this
# engine owns the window, the ImGui context and the renderer, and draws through NVRHI. The
# panel supplies a small stand-in for that framework instead.
#
# crude_json.cpp is part of the library rather than a sample: the editor uses it to serialise
# node positions, so leaving it out fails at link time.
#
# LOCAL PATCHES: the library has not caught up with Dear ImGui 1.92, so three files carry small
# fixes. They are applied at configure time from ImGuiNodeEditor-ImGui192.patch, because a change
# inside a submodule is not part of this repository's history: a fresh clone would check out the
# unpatched upstream and fail to compile with errors that say nothing about why.
#
# Applied with --reverse --check first to see whether the patch is already in place, so re-running
# configure is harmless. A failure here is reported and left alone rather than retried, since the
# likely cause is the submodule having moved and the patch needing to be rebuilt by hand.
# ---------------------------------------------------------------------------------------
if(LIME_BUILD_NODE_EDITOR)
	set(NodeEditorPatch "${CMAKE_CURRENT_SOURCE_DIR}/ImGuiNodeEditor-ImGui192.patch")
	# Only needed for this, so it is looked up here rather than at the top level.
	find_package(Git QUIET)
	if(NOT Git_FOUND)
		message(WARNING "Git was not found, so the ImGuiNodeEditor compatibility patch cannot be applied; "
		                "the node editor will not compile against Dear ImGui 1.92.")
	else()
		execute_process(
			COMMAND ${GIT_EXECUTABLE} apply --reverse --check "${NodeEditorPatch}"
			WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/ImGuiNodeEditor"
			RESULT_VARIABLE NodeEditorPatchApplied
			OUTPUT_QUIET
			ERROR_QUIET
		)
		if(NOT NodeEditorPatchApplied EQUAL 0)
			execute_process(
				COMMAND ${GIT_EXECUTABLE} apply "${NodeEditorPatch}"
				WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/ImGuiNodeEditor"
				RESULT_VARIABLE NodeEditorPatchResult
				ERROR_VARIABLE NodeEditorPatchError
			)
			if(NodeEditorPatchResult EQUAL 0)
				message(STATUS "Applied the Dear ImGui 1.92 compatibility patch to ImGuiNodeEditor")
			else()
				message(WARNING "Could not patch ImGuiNodeEditor for Dear ImGui 1.92; it will not compile. "
				                "The submodule may have moved and the patch may need rebuilding. ${NodeEditorPatchError}")
			endif()
		endif()
	endif()

	add_library(imgui_node_editor STATIC
		ImGuiNodeEditor/crude_json.cpp
		ImGuiNodeEditor/crude_json.h
		ImGuiNodeEditor/imgui_canvas.cpp
		ImGuiNodeEditor/imgui_canvas.h
		ImGuiNodeEditor/imgui_node_editor.cpp
		ImGuiNodeEditor/imgui_node_editor.h
		ImGuiNodeEditor/imgui_node_editor_api.cpp
		ImGuiNodeEditor/imgui_node_editor_internal.h
		ImGuiNodeEditor/imgui_node_editor_internal.inl
	)
	target_include_directories(imgui_node_editor SYSTEM PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/ImGuiNodeEditor
	)
	target_link_libraries(imgui_node_editor PUBLIC imgui)
	if(MSVC)
		# The library predates several warnings this build treats as errors and is not ours to fix.
		target_compile_options(imgui_node_editor PRIVATE /W0)
	endif()
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
# Both helpers skip targets that do not exist, so the optional ones can be listed here
# unconditionally.
# ---------------------------------------------------------------------------------------
lime_mark_system_includes(nvrhi nvrhi_d3d12 nvrhi_vk glfw spdlog imgui imgui_test_engine imgui_node_editor tinygltf ShaderMakeBlob)
lime_set_third_party_folder(
	nvrhi nvrhi_d3d12 nvrhi_vk
	glfw spdlog imgui imgui_test_engine imgui_node_editor tinygltf
	ShaderMake ShaderMakeBlob
	DirectX-Headers DirectX-Guids
	Catch2 Catch2WithMain
	update_mappings
)
