// Device and swap chain management. NVRHI does not provide this, so each backend implements the
// platform specific parts behind one interface.

#pragma once

#include "RHI/RHITypes.h"

#include <nvrhi/nvrhi.h>

#include <memory>
#include <vector>

namespace Lime
{
	class FWindow;

	// Routes NVRHI diagnostics into the engine log.
	class FRHIMessageCallback final : public nvrhi::IMessageCallback
	{
	public:
		void message(nvrhi::MessageSeverity Severity, const char* MessageText) override;
	};

	class IDeviceManager
	{
	public:
		virtual ~IDeviceManager() = default;

		virtual bool Initialize(FWindow& Window, const FDeviceCreationDesc& Desc) = 0;
		virtual void Shutdown() = 0;

		// Acquires the next back buffer. Returns false when the frame must be skipped, for example
		// while the window is minimized or the swap chain is being recreated.
		virtual bool BeginFrame() = 0;
		virtual void Present() = 0;
		virtual void ResizeSwapChain(uint32 Width, uint32 Height) = 0;
		// Blocks until the GPU is idle; required before destroying resources.
		virtual void WaitForIdle() = 0;

		virtual nvrhi::IDevice* GetDevice() const = 0;
		virtual nvrhi::IFramebuffer* GetCurrentFramebuffer() const = 0;
		virtual nvrhi::Format GetBackBufferFormat() const = 0;
		virtual ERHIBackend GetBackend() const = 0;
		virtual uint32 GetBackBufferWidth() const = 0;
		virtual uint32 GetBackBufferHeight() const = 0;
		// Human readable adapter name for the editor UI.
		virtual const std::string& GetAdapterName() const = 0;
	};

	// Returns nullptr when the backend is not compiled in.
	std::unique_ptr<IDeviceManager> CreateDeviceManager(ERHIBackend Backend);

	// Shared framebuffer caching so per-frame createFramebuffer calls are avoided.
	class FDeviceManagerBase : public IDeviceManager
	{
	public:
		nvrhi::IFramebuffer* GetCurrentFramebuffer() const override;
		nvrhi::Format GetBackBufferFormat() const override { return BackBufferFormat; }
		uint32 GetBackBufferWidth() const override { return BackBufferWidth; }
		uint32 GetBackBufferHeight() const override { return BackBufferHeight; }
		const std::string& GetAdapterName() const override { return AdapterName; }

	protected:
		// Wraps the device in the NVRHI validation layer when requested.
		nvrhi::DeviceHandle ApplyValidationLayer(nvrhi::DeviceHandle InDevice, bool bEnableValidation);
		// Rebuilds the framebuffer cache from the current back buffer textures.
		bool RebuildFramebuffers();
		void ReleaseFramebuffers();

		FRHIMessageCallback MessageCallback;
		nvrhi::DeviceHandle Device;
		std::vector<nvrhi::TextureHandle> BackBuffers;
		std::vector<nvrhi::FramebufferHandle> Framebuffers;
		nvrhi::Format BackBufferFormat = nvrhi::Format::RGBA8_UNORM;
		std::string AdapterName = "Unknown";
		uint32 BackBufferWidth = 0;
		uint32 BackBufferHeight = 0;
		uint32 CurrentBackBufferIndex = 0;
		bool bVSync = true;
	};
} // namespace Lime
