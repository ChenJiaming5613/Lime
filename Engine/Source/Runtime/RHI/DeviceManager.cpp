#include "RHI/DeviceManager.h"

#include "Core/Logging/LogManager.h"

#include <nvrhi/validation.h>

#if LIME_RHI_D3D12
#include "RHI/D3D12/D3D12DeviceManager.h"
#endif
#if LIME_RHI_VULKAN
#include "RHI/Vulkan/VulkanDeviceManager.h"
#endif

namespace Lime
{
	void FRHIMessageCallback::message(nvrhi::MessageSeverity Severity, const char* MessageText)
	{
		switch (Severity)
		{
			case nvrhi::MessageSeverity::Info:
				LIME_LOG_INFO(LIME_LOG_CATEGORY_RHI, "{}", MessageText);
				break;
			case nvrhi::MessageSeverity::Warning:
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_RHI, "{}", MessageText);
				break;
			case nvrhi::MessageSeverity::Error:
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "{}", MessageText);
				break;
			case nvrhi::MessageSeverity::Fatal:
				LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "{}", MessageText);
				break;
			default:
				LIME_LOG_INFO(LIME_LOG_CATEGORY_RHI, "{}", MessageText);
				break;
		}
	}

	std::unique_ptr<IDeviceManager> CreateDeviceManager(ERHIBackend Backend)
	{
		switch (Backend)
		{
#if LIME_RHI_D3D12
			case ERHIBackend::D3D12:
				return std::make_unique<FD3D12DeviceManager>();
#endif
#if LIME_RHI_VULKAN
			case ERHIBackend::Vulkan:
				return std::make_unique<FVulkanDeviceManager>();
#endif
			default:
				break;
		}

		LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Backend {} is not compiled into this build", ToString(Backend));
		return nullptr;
	}

	nvrhi::DeviceHandle FDeviceManagerBase::ApplyValidationLayer(nvrhi::DeviceHandle InDevice, bool bEnableValidation)
	{
		if (InDevice == nullptr || !bEnableValidation)
		{
			return InDevice;
		}

		// Catches binding set and layout mismatches that would otherwise fail deep inside the driver.
		LIME_LOG_INFO(LIME_LOG_CATEGORY_RHI, "NVRHI validation layer enabled");
		return nvrhi::validation::createValidationLayer(InDevice);
	}

	nvrhi::IFramebuffer* FDeviceManagerBase::GetCurrentFramebuffer() const
	{
		if (CurrentBackBufferIndex >= Framebuffers.size())
		{
			return nullptr;
		}
		return Framebuffers[CurrentBackBufferIndex];
	}

	bool FDeviceManagerBase::RebuildFramebuffers()
	{
		Framebuffers.clear();
		Framebuffers.reserve(BackBuffers.size());

		for (const nvrhi::TextureHandle& BackBuffer : BackBuffers)
		{
			if (BackBuffer == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Back buffer texture is null while building framebuffers");
				Framebuffers.clear();
				return false;
			}

			nvrhi::FramebufferHandle Framebuffer = Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(BackBuffer));
			if (Framebuffer == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "createFramebuffer failed");
				Framebuffers.clear();
				return false;
			}

			Framebuffers.push_back(std::move(Framebuffer));
		}

		return true;
	}

	void FDeviceManagerBase::ReleaseFramebuffers()
	{
		Framebuffers.clear();
		BackBuffers.clear();
		CurrentBackBufferIndex = 0;
	}
} // namespace Lime
