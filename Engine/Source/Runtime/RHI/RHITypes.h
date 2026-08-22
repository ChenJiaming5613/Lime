// RHI level types shared by the device managers and the renderer.

#pragma once

#include "Core/CoreTypes.h"

#include <nvrhi/nvrhi.h>

#include <string_view>

namespace Lime
{
	enum class ERHIBackend : uint8
	{
		D3D12 = 0,
		Vulkan
	};

	const char* ToString(ERHIBackend Backend);
	// Accepts "d3d12", "dx12", "vulkan", "vk"; returns false when unrecognized.
	bool TryParseBackend(std::string_view Text, ERHIBackend& OutBackend);
	bool IsBackendEnabled(ERHIBackend Backend);
	// First compiled-in backend, preferring D3D12 on Windows.
	ERHIBackend GetDefaultBackend();

	struct FDeviceCreationDesc
	{
		ERHIBackend Backend = GetDefaultBackend();
		uint32 BackBufferCount = 3;
		bool bEnableDebugRuntime = LIME_DEBUG != 0;
		bool bEnableNvrhiValidation = LIME_DEBUG != 0;
		bool bVSync = true;
	};

	// HLSL register shifts for Vulkan. These mirror LIME_VK_SHIFT_* in CMake, which are also passed
	// to DXC, and must match nvrhi::VulkanBindingOffsets or descriptor bindings will be wrong.
	struct FVulkanBindingShifts
	{
		static constexpr uint32 ShaderResource = 0;
		static constexpr uint32 Sampler = 128;
		static constexpr uint32 ConstantBuffer = 256;
		static constexpr uint32 UnorderedAccess = 384;

		static nvrhi::VulkanBindingOffsets ToNvrhi();
	};

	// Applies the Vulkan binding offsets so one BindingLayoutDesc works on both backends.
	nvrhi::BindingLayoutDesc MakeBindingLayoutDesc(nvrhi::ShaderType Visibility);
} // namespace Lime
