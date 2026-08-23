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

		// The D3D12 path calls this from CreateSwapChain, which runs before the NVRHI device exists so
		// that the swap chain format is known when the device is created. There is nothing to build yet
		// in that case, and WrapBackBuffers calls again once the device is ready.
		if (Device == nullptr)
		{
			return true;
		}

		Framebuffers.reserve(BackBuffers.size());

		// One depth buffer shared by every back buffer. Unlike the colour targets, which the presentation
		// engine may still be reading from, depth is written and consumed within a single frame and never
		// outlives it, so there is nothing to double buffer.
		if (!RebuildDepthBuffer())
		{
			Framebuffers.clear();
			return false;
		}

		for (const nvrhi::TextureHandle& BackBuffer : BackBuffers)
		{
			if (BackBuffer == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Back buffer texture is null while building framebuffers");
				Framebuffers.clear();
				return false;
			}

			nvrhi::FramebufferHandle Framebuffer =
			    Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(BackBuffer).setDepthAttachment(DepthBuffer));
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

	bool FDeviceManagerBase::RebuildDepthBuffer()
	{
		DepthBuffer = nullptr;

		if (BackBufferWidth == 0 || BackBufferHeight == 0)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Back buffer size is unknown while creating the depth buffer");
			return false;
		}

		// isTypeless matters and is not optional. A depth resource is viewed through two incompatible
		// view types, a depth stencil view for writing and a shader view for reading, and only a
		// typeless resource can carry both. Creating it as a plain depth format makes the D3D12 backend
		// fault while building descriptors. This follows Donut's GBuffer, which is written against the
		// same NVRHI.
		const nvrhi::TextureDesc DepthDesc = nvrhi::TextureDesc()
		                                         .setDimension(nvrhi::TextureDimension::Texture2D)
		                                         .setWidth(BackBufferWidth)
		                                         .setHeight(BackBufferHeight)
		                                         .setFormat(DepthFormat)
		                                         .setIsRenderTarget(true)
		                                         .setIsTypeless(true)
		                                         .setInitialState(nvrhi::ResourceStates::DepthWrite)
		                                         .setKeepInitialState(true)
		                                         // Must match the value the renderer clears with, or D3D12
		                                         // loses the fast clear path and warns about it.
		                                         .setClearValue(nvrhi::Color(1.0f))
		                                         .setDebugName("SwapChainDepth");

		DepthBuffer = Device->createTexture(DepthDesc);
		if (DepthBuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "createTexture failed for the swap chain depth buffer ({}x{})", BackBufferWidth,
			               BackBufferHeight);
			return false;
		}

		return true;
	}

	void FDeviceManagerBase::ReleaseFramebuffers()
	{
		// Framebuffers first: they reference the depth texture, so releasing it earlier would keep the
		// handle alive anyway and obscure the ownership.
		Framebuffers.clear();
		DepthBuffer = nullptr;
		BackBuffers.clear();
		CurrentBackBufferIndex = 0;
	}
} // namespace Lime
