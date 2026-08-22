# LimeRHI: NVRHI wrapper, device managers and shader loading.

set(RhiSources
	DeviceManager.h
	DeviceManager.cpp
	RHITypes.h
	RHITypes.cpp
	ShaderKey.h
	ShaderKey.cpp
	ShaderLibrary.h
	ShaderLibrary.cpp
)

set(RhiPublicDeps LimePlatform nvrhi)
set(RhiPrivateDeps ShaderMakeBlob)

if(LIME_ENABLE_RHI_D3D12)
	list(APPEND RhiSources
		D3D12/D3D12DeviceManager.h
		D3D12/D3D12DeviceManager.cpp
	)
	list(APPEND RhiPublicDeps nvrhi_d3d12)
	list(APPEND RhiPrivateDeps d3d12 dxgi dxguid)
endif()

if(LIME_ENABLE_RHI_VULKAN)
	list(APPEND RhiSources
		Vulkan/VulkanDeviceManager.h
		Vulkan/VulkanDeviceManager.cpp
	)
	# The loader is resolved at runtime, so only the headers are needed at build time.
	list(APPEND RhiPublicDeps nvrhi_vk Vulkan::Headers)
	list(APPEND RhiPrivateDeps glfw)
endif()

lime_add_module(
	NAME LimeRHI
	SOURCES ${RhiSources}
	PUBLIC_DEPS ${RhiPublicDeps}
	PRIVATE_DEPS ${RhiPrivateDeps}
	FOLDER "Engine/Runtime"
)

target_include_directories(LimeRHI PRIVATE "${LIME_THIRD_PARTY_DIR}/ShaderMake")

# The shader blobs must exist before anything that loads them runs.
add_dependencies(LimeRHI LimeShaders)
