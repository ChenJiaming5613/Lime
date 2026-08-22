# Shared compile settings applied to every LimeEngine target.

add_library(LimeCompilerOptions INTERFACE)

target_compile_features(LimeCompilerOptions INTERFACE cxx_std_20)

target_compile_definitions(LimeCompilerOptions INTERFACE
	NOMINMAX
	WIN32_LEAN_AND_MEAN
	UNICODE
	_UNICODE
	_CRT_SECURE_NO_WARNINGS
	$<$<CONFIG:Debug>:LIME_DEBUG=1>
	$<$<NOT:$<CONFIG:Debug>>:LIME_DEBUG=0>
)

if(LIME_BUILD_EDITOR)
	target_compile_definitions(LimeCompilerOptions INTERFACE LIME_WITH_EDITOR=1)
else()
	target_compile_definitions(LimeCompilerOptions INTERFACE LIME_WITH_EDITOR=0)
endif()

if(LIME_ENABLE_RHI_D3D12)
	target_compile_definitions(LimeCompilerOptions INTERFACE LIME_RHI_D3D12=1)
else()
	target_compile_definitions(LimeCompilerOptions INTERFACE LIME_RHI_D3D12=0)
endif()

if(LIME_ENABLE_RHI_VULKAN)
	target_compile_definitions(LimeCompilerOptions INTERFACE LIME_RHI_VULKAN=1)
else()
	target_compile_definitions(LimeCompilerOptions INTERFACE LIME_RHI_VULKAN=0)
endif()

if(MSVC)
	target_compile_options(LimeCompilerOptions INTERFACE
		/W4
		/permissive-
		/Zc:preprocessor
		/Zc:__cplusplus
		/Zc:inline
		/utf-8
		/EHsc
		/MP
		/bigobj
		# 4324: structure padded due to alignment specifier, triggered by aligned constant buffers.
		/wd4324
	)
endif()
