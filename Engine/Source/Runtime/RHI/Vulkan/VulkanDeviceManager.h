// Vulkan instance, device and swap chain.
//
// The Vulkan loader is resolved at runtime through vulkan-hpp's dynamic dispatcher, so the engine
// links no import library and a machine without a Vulkan runtime can still use the D3D12 backend.

#pragma once

#include "RHI/DeviceManager.h"

#define VK_NO_PROTOTYPES
#include <nvrhi/vulkan.h>
#include <vulkan/vulkan.h>

namespace Lime
{
	class FVulkanDeviceManager final : public FDeviceManagerBase
	{
	public:
		FVulkanDeviceManager() = default;
		~FVulkanDeviceManager() override;

		bool Initialize(FWindow& Window, const FDeviceCreationDesc& Desc) override;
		void Shutdown() override;

		bool BeginFrame() override;
		void Present() override;
		void ResizeSwapChain(uint32 Width, uint32 Height) override;
		void WaitForIdle() override;

		nvrhi::IDevice* GetDevice() const override { return Device; }
		ERHIBackend GetBackend() const override { return ERHIBackend::Vulkan; }

	private:
		bool LoadLoader();
		bool CreateInstance(bool bEnableValidationLayers);
		bool SelectPhysicalDevice();
		bool CreateLogicalDevice();
		bool CreateSwapChain(uint32 Width, uint32 Height, uint32 ImageCount);
		bool WrapSwapChainImages();
		bool CreateSyncObjects();
		// Sized to the swap chain image count, so recreated with the swap chain.
		bool CreateReleaseSemaphores();
		void DestroySwapChain();
		bool RecreateSwapChain(uint32 Width, uint32 Height);

		void* LoaderModule = nullptr;
		VkInstance Instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;
		VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
		VkDevice LogicalDevice = VK_NULL_HANDLE;
		VkSurfaceKHR Surface = VK_NULL_HANDLE;
		VkSwapchainKHR SwapChain = VK_NULL_HANDLE;
		VkQueue GraphicsQueue = VK_NULL_HANDLE;
		VkQueue PresentQueue = VK_NULL_HANDLE;

		// Typed view of the device, needed for the Vulkan specific semaphore methods.
		nvrhi::vulkan::IDevice* VulkanDevice = nullptr;

		uint32 GraphicsQueueFamily = UINT32_MAX;
		uint32 PresentQueueFamily = UINT32_MAX;
		VkFormat SurfaceFormat = VK_FORMAT_UNDEFINED;
		VkPresentModeKHR PresentMode = VK_PRESENT_MODE_FIFO_KHR;
		uint32 RequestedImageCount = 3;

		std::vector<VkImage> SwapChainImages;
		// Acquire semaphores rotate per in-flight frame. Release semaphores are indexed by swap chain
		// image instead, because a presented image keeps its semaphore busy until it is re-acquired.
		std::vector<VkSemaphore> ImageAvailableSemaphores;
		std::vector<VkSemaphore> RenderFinishedSemaphores;
		// NVRHI event queries throttle the CPU; NVRHI owns every queue submission.
		std::vector<nvrhi::EventQueryHandle> FrameQueries;
		uint32 FrameIndex = 0;
		bool bFrameAcquired = false;
		bool bSwapChainOutOfDate = false;
	};
} // namespace Lime
