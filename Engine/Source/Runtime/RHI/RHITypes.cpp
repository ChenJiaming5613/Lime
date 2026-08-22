#include "RHI/RHITypes.h"

#include <algorithm>
#include <string>

namespace Lime
{
	const char* ToString(ERHIBackend Backend)
	{
		switch (Backend)
		{
			case ERHIBackend::D3D12:
				return "D3D12";
			case ERHIBackend::Vulkan:
				return "Vulkan";
			default:
				return "Unknown";
		}
	}

	bool TryParseBackend(std::string_view Text, ERHIBackend& OutBackend)
	{
		std::string Lowered(Text);
		std::transform(Lowered.begin(), Lowered.end(), Lowered.begin(),
		               [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });

		if (Lowered == "d3d12" || Lowered == "dx12" || Lowered == "directx12")
		{
			OutBackend = ERHIBackend::D3D12;
			return true;
		}
		if (Lowered == "vulkan" || Lowered == "vk")
		{
			OutBackend = ERHIBackend::Vulkan;
			return true;
		}
		return false;
	}

	bool IsBackendEnabled(ERHIBackend Backend)
	{
		switch (Backend)
		{
			case ERHIBackend::D3D12:
				return LIME_RHI_D3D12 != 0;
			case ERHIBackend::Vulkan:
				return LIME_RHI_VULKAN != 0;
			default:
				return false;
		}
	}

	ERHIBackend GetDefaultBackend()
	{
#if LIME_RHI_D3D12
		return ERHIBackend::D3D12;
#else
		return ERHIBackend::Vulkan;
#endif
	}

	nvrhi::VulkanBindingOffsets FVulkanBindingShifts::ToNvrhi()
	{
		return nvrhi::VulkanBindingOffsets()
		    .setShaderResourceOffset(ShaderResource)
		    .setSamplerOffset(Sampler)
		    .setConstantBufferOffset(ConstantBuffer)
		    .setUnorderedAccessViewOffset(UnorderedAccess);
	}

	nvrhi::BindingLayoutDesc MakeBindingLayoutDesc(nvrhi::ShaderType Visibility)
	{
		return nvrhi::BindingLayoutDesc().setVisibility(Visibility).setBindingOffsets(FVulkanBindingShifts::ToNvrhi());
	}
} // namespace Lime
