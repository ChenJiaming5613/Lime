#include "RHI/Vulkan/VulkanDeviceManager.h"

#include "Core/Logging/LogManager.h"
#include "Platform/Window.h"

#include <GLFW/glfw3.h>
#include <nvrhi/vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>

#if defined(_WIN32)
#include <Windows.h>
#endif

// NVRHI is built statically here, so the application owns the vulkan-hpp dynamic dispatcher storage.
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Lime
{
	namespace
	{
		constexpr uint32 MaxFramesInFlight = 2;

		nvrhi::Format ToNvrhiFormat(VkFormat Format)
		{
			switch (Format)
			{
				case VK_FORMAT_R8G8B8A8_UNORM:
					return nvrhi::Format::RGBA8_UNORM;
				case VK_FORMAT_B8G8R8A8_UNORM:
					return nvrhi::Format::BGRA8_UNORM;
				case VK_FORMAT_R8G8B8A8_SRGB:
					return nvrhi::Format::SRGBA8_UNORM;
				case VK_FORMAT_B8G8R8A8_SRGB:
					return nvrhi::Format::SBGRA8_UNORM;
				default:
					return nvrhi::Format::UNKNOWN;
			}
		}

		VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT Severity,
		                                                      VkDebugUtilsMessageTypeFlagsEXT /*Types*/,
		                                                      const VkDebugUtilsMessengerCallbackDataEXT* CallbackData, void* /*UserData*/)
		{
			if (CallbackData == nullptr || CallbackData->pMessage == nullptr)
			{
				return VK_FALSE;
			}

			if ((Severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Vulkan: {}", CallbackData->pMessage);
			}
			else if ((Severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_RHI, "Vulkan: {}", CallbackData->pMessage);
			}
			return VK_FALSE;
		}
	} // namespace

	FVulkanDeviceManager::~FVulkanDeviceManager()
	{
		Shutdown();
	}

	bool FVulkanDeviceManager::LoadLoader()
	{
#if defined(_WIN32)
		LoaderModule = LoadLibraryA("vulkan-1.dll");
		if (LoaderModule == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "vulkan-1.dll not found; install a Vulkan capable graphics driver");
			return false;
		}

		auto GetInstanceProcAddr =
		    reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(static_cast<HMODULE>(LoaderModule), "vkGetInstanceProcAddr"));
#else
		PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
#endif
		if (GetInstanceProcAddr == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "vkGetInstanceProcAddr could not be resolved");
			return false;
		}

		VULKAN_HPP_DEFAULT_DISPATCHER.init(GetInstanceProcAddr);
		return true;
	}

	bool FVulkanDeviceManager::Initialize(FWindow& Window, const FDeviceCreationDesc& Desc)
	{
		bVSync = Desc.bVSync;
		RequestedImageCount = std::max<uint32>(Desc.BackBufferCount, 2);

		if (!LoadLoader() || !CreateInstance(Desc.bEnableDebugRuntime))
		{
			return false;
		}

		VkSurfaceKHR RawSurface = VK_NULL_HANDLE;
		if (glfwCreateWindowSurface(Instance, Window.GetHandle(), nullptr, &RawSurface) != VK_SUCCESS)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "glfwCreateWindowSurface failed");
			return false;
		}
		Surface = RawSurface;

		if (!SelectPhysicalDevice() || !CreateLogicalDevice())
		{
			return false;
		}

		nvrhi::vulkan::DeviceDesc DeviceDesc;
		DeviceDesc.errorCB = &MessageCallback;
		DeviceDesc.instance = Instance;
		DeviceDesc.physicalDevice = PhysicalDevice;
		DeviceDesc.device = LogicalDevice;
		DeviceDesc.graphicsQueue = GraphicsQueue;
		DeviceDesc.graphicsQueueIndex = static_cast<int>(GraphicsQueueFamily);
		DeviceDesc.bufferDeviceAddressSupported = true;

		// Extension names NVRHI needs to know were enabled; it toggles feature flags from this list.
		const char* EnabledDeviceExtensions[] = {
			VK_KHR_SWAPCHAIN_EXTENSION_NAME,
			VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
		};
		DeviceDesc.deviceExtensions = EnabledDeviceExtensions;
		DeviceDesc.numDeviceExtensions = std::size(EnabledDeviceExtensions);

		nvrhi::vulkan::DeviceHandle NativeDevice = nvrhi::vulkan::createDevice(DeviceDesc);
		if (NativeDevice == nullptr)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "nvrhi::vulkan::createDevice failed");
			return false;
		}

		// Keep the unwrapped device: the Vulkan specific semaphore methods are not exposed through
		// the validation layer wrapper.
		VulkanDevice = NativeDevice.Get();
		Device = ApplyValidationLayer(nvrhi::DeviceHandle(NativeDevice.Get()), Desc.bEnableNvrhiValidation);

		if (!CreateSwapChain(Window.GetWidth(), Window.GetHeight(), RequestedImageCount) || !CreateSyncObjects())
		{
			return false;
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_RHI, "Vulkan device ready on {} ({}x{}, {} images, vsync {})", AdapterName, BackBufferWidth,
		              BackBufferHeight, SwapChainImages.size(), bVSync ? "on" : "off");
		return true;
	}

	bool FVulkanDeviceManager::CreateInstance(bool bEnableValidationLayers)
	{
		uint32 GlfwExtensionCount = 0;
		const char** GlfwExtensions = FWindow::GetRequiredVulkanExtensions(GlfwExtensionCount);
		if (GlfwExtensions == nullptr || GlfwExtensionCount == 0)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "GLFW reported no required Vulkan instance extensions");
			return false;
		}

		std::vector<const char*> Extensions(GlfwExtensions, GlfwExtensions + GlfwExtensionCount);
		std::vector<const char*> Layers;

		if (bEnableValidationLayers)
		{
			uint32_t LayerCount = 0;
			LIME_UNUSED(vk::enumerateInstanceLayerProperties(&LayerCount, nullptr));
			std::vector<vk::LayerProperties> AvailableLayers(LayerCount);
			LIME_UNUSED(vk::enumerateInstanceLayerProperties(&LayerCount, AvailableLayers.data()));

			const char* ValidationLayerName = "VK_LAYER_KHRONOS_validation";
			const bool bHasValidation =
			    std::any_of(AvailableLayers.begin(), AvailableLayers.end(), [ValidationLayerName](const vk::LayerProperties& Layer)
				            { return std::strcmp(Layer.layerName, ValidationLayerName) == 0; });

			if (bHasValidation)
			{
				Layers.push_back(ValidationLayerName);
				Extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
				LIME_LOG_INFO(LIME_LOG_CATEGORY_RHI, "Vulkan validation layer enabled");
			}
			else
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_RHI, "VK_LAYER_KHRONOS_validation not available");
			}
		}

		const vk::ApplicationInfo ApplicationInfo("LimeEngine", 1, "LimeEngine", 1, VK_API_VERSION_1_3);
		const vk::InstanceCreateInfo CreateInfo({}, &ApplicationInfo, static_cast<uint32_t>(Layers.size()), Layers.data(),
		                                        static_cast<uint32_t>(Extensions.size()), Extensions.data());

		vk::Instance NewInstance;
		if (vk::createInstance(&CreateInfo, nullptr, &NewInstance) != vk::Result::eSuccess)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "vkCreateInstance failed");
			return false;
		}

		Instance = NewInstance;
		VULKAN_HPP_DEFAULT_DISPATCHER.init(NewInstance);

		if (!Layers.empty())
		{
			VkDebugUtilsMessengerCreateInfoEXT MessengerInfo = {};
			MessengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			MessengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			MessengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			MessengerInfo.pfnUserCallback = &DebugMessengerCallback;

			if (VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateDebugUtilsMessengerEXT != nullptr)
			{
				VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateDebugUtilsMessengerEXT(Instance, &MessengerInfo, nullptr, &DebugMessenger);
			}
		}

		return true;
	}

	bool FVulkanDeviceManager::SelectPhysicalDevice()
	{
		const vk::Instance CppInstance(Instance);

		uint32_t DeviceCount = 0;
		LIME_UNUSED(CppInstance.enumeratePhysicalDevices(&DeviceCount, nullptr));
		if (DeviceCount == 0)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "No Vulkan capable physical device found");
			return false;
		}

		std::vector<vk::PhysicalDevice> Devices(DeviceCount);
		LIME_UNUSED(CppInstance.enumeratePhysicalDevices(&DeviceCount, Devices.data()));

		vk::PhysicalDevice BestDevice;
		uint32 BestGraphicsFamily = UINT32_MAX;
		uint32 BestPresentFamily = UINT32_MAX;
		int32 BestScore = -1;

		for (const vk::PhysicalDevice& Candidate : Devices)
		{
			const vk::PhysicalDeviceProperties Properties = Candidate.getProperties();
			if (Properties.apiVersion < VK_API_VERSION_1_3)
			{
				continue;
			}

			const std::vector<vk::QueueFamilyProperties> QueueFamilies = Candidate.getQueueFamilyProperties();
			uint32 GraphicsFamily = UINT32_MAX;
			uint32 PresentFamily = UINT32_MAX;

			for (uint32 Index = 0; Index < static_cast<uint32>(QueueFamilies.size()); ++Index)
			{
				if ((QueueFamilies[Index].queueFlags & vk::QueueFlagBits::eGraphics) != vk::QueueFlagBits{} && GraphicsFamily == UINT32_MAX)
				{
					GraphicsFamily = Index;
				}

				VkBool32 bSupportsPresent = VK_FALSE;
				if (Candidate.getSurfaceSupportKHR(Index, vk::SurfaceKHR(Surface), &bSupportsPresent) == vk::Result::eSuccess &&
				    bSupportsPresent == VK_TRUE && PresentFamily == UINT32_MAX)
				{
					PresentFamily = Index;
				}
			}

			if (GraphicsFamily == UINT32_MAX || PresentFamily == UINT32_MAX)
			{
				continue;
			}

			// Prefer discrete hardware, matching the D3D12 high performance adapter preference.
			const int32 Score = Properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu ? 100 : 10;
			if (Score > BestScore)
			{
				BestScore = Score;
				BestDevice = Candidate;
				BestGraphicsFamily = GraphicsFamily;
				BestPresentFamily = PresentFamily;
				AdapterName = Properties.deviceName.data();
			}
		}

		if (BestScore < 0)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "No Vulkan 1.3 device with graphics and present support was found");
			return false;
		}

		PhysicalDevice = BestDevice;
		GraphicsQueueFamily = BestGraphicsFamily;
		PresentQueueFamily = BestPresentFamily;
		return true;
	}

	bool FVulkanDeviceManager::CreateLogicalDevice()
	{
		const vk::PhysicalDevice CppPhysicalDevice(PhysicalDevice);

		const float QueuePriority = 1.0f;
		std::vector<vk::DeviceQueueCreateInfo> QueueInfos;
		QueueInfos.push_back(vk::DeviceQueueCreateInfo({}, GraphicsQueueFamily, 1, &QueuePriority));
		if (PresentQueueFamily != GraphicsQueueFamily)
		{
			QueueInfos.push_back(vk::DeviceQueueCreateInfo({}, PresentQueueFamily, 1, &QueuePriority));
		}

		const std::array<const char*, 1> DeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

		vk::PhysicalDeviceFeatures Features;
		Features.samplerAnisotropy = VK_TRUE;
		Features.fillModeNonSolid = VK_TRUE;
		Features.independentBlend = VK_TRUE;

		// NVRHI relies on descriptor indexing, timeline semaphores and buffer device address.
		vk::PhysicalDeviceVulkan12Features Features12;
		Features12.descriptorIndexing = VK_TRUE;
		Features12.timelineSemaphore = VK_TRUE;
		Features12.bufferDeviceAddress = VK_TRUE;
		Features12.runtimeDescriptorArray = VK_TRUE;
		Features12.descriptorBindingPartiallyBound = VK_TRUE;
		Features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

		vk::PhysicalDeviceVulkan13Features Features13;
		Features13.synchronization2 = VK_TRUE;
		Features13.dynamicRendering = VK_TRUE;
		Features13.maintenance4 = VK_TRUE;
		Features13.pNext = &Features12;

		vk::DeviceCreateInfo CreateInfo({}, static_cast<uint32_t>(QueueInfos.size()), QueueInfos.data(), 0, nullptr,
		                                static_cast<uint32_t>(DeviceExtensions.size()), DeviceExtensions.data(), &Features);
		CreateInfo.pNext = &Features13;

		vk::Device NewDevice;
		if (CppPhysicalDevice.createDevice(&CreateInfo, nullptr, &NewDevice) != vk::Result::eSuccess)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "vkCreateDevice failed");
			return false;
		}

		LogicalDevice = NewDevice;
		VULKAN_HPP_DEFAULT_DISPATCHER.init(NewDevice);

		GraphicsQueue = NewDevice.getQueue(GraphicsQueueFamily, 0);
		PresentQueue = NewDevice.getQueue(PresentQueueFamily, 0);
		return true;
	}

	bool FVulkanDeviceManager::CreateSwapChain(uint32 Width, uint32 Height, uint32 ImageCount)
	{
		const vk::PhysicalDevice CppPhysicalDevice(PhysicalDevice);
		const vk::SurfaceKHR CppSurface(Surface);

		const vk::SurfaceCapabilitiesKHR Capabilities = CppPhysicalDevice.getSurfaceCapabilitiesKHR(CppSurface);
		const std::vector<vk::SurfaceFormatKHR> Formats = CppPhysicalDevice.getSurfaceFormatsKHR(CppSurface);
		const std::vector<vk::PresentModeKHR> PresentModes = CppPhysicalDevice.getSurfacePresentModesKHR(CppSurface);

		if (Formats.empty() || PresentModes.empty())
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "Surface reports no usable formats or present modes");
			return false;
		}

		// Non-sRGB so shader output is written verbatim, matching the D3D12 path.
		vk::SurfaceFormatKHR ChosenFormat = Formats[0];
		for (const vk::SurfaceFormatKHR& Candidate : Formats)
		{
			if ((Candidate.format == vk::Format::eB8G8R8A8Unorm || Candidate.format == vk::Format::eR8G8B8A8Unorm) &&
			    Candidate.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
			{
				ChosenFormat = Candidate;
				break;
			}
		}

		vk::PresentModeKHR ChosenPresentMode = vk::PresentModeKHR::eFifo;
		if (!bVSync)
		{
			const bool bHasMailbox =
			    std::find(PresentModes.begin(), PresentModes.end(), vk::PresentModeKHR::eMailbox) != PresentModes.end();
			const bool bHasImmediate =
			    std::find(PresentModes.begin(), PresentModes.end(), vk::PresentModeKHR::eImmediate) != PresentModes.end();
			ChosenPresentMode =
			    bHasMailbox ? vk::PresentModeKHR::eMailbox : (bHasImmediate ? vk::PresentModeKHR::eImmediate : vk::PresentModeKHR::eFifo);
		}

		vk::Extent2D Extent;
		if (Capabilities.currentExtent.width != UINT32_MAX)
		{
			Extent = Capabilities.currentExtent;
		}
		else
		{
			Extent.width = std::clamp(Width, Capabilities.minImageExtent.width, Capabilities.maxImageExtent.width);
			Extent.height = std::clamp(Height, Capabilities.minImageExtent.height, Capabilities.maxImageExtent.height);
		}

		if (Extent.width == 0 || Extent.height == 0)
		{
			BackBufferWidth = 0;
			BackBufferHeight = 0;
			return false;
		}

		uint32 DesiredImageCount = std::max(ImageCount, Capabilities.minImageCount);
		if (Capabilities.maxImageCount > 0)
		{
			DesiredImageCount = std::min(DesiredImageCount, Capabilities.maxImageCount);
		}

		vk::SwapchainCreateInfoKHR CreateInfo;
		CreateInfo.surface = CppSurface;
		CreateInfo.minImageCount = DesiredImageCount;
		CreateInfo.imageFormat = ChosenFormat.format;
		CreateInfo.imageColorSpace = ChosenFormat.colorSpace;
		CreateInfo.imageExtent = Extent;
		CreateInfo.imageArrayLayers = 1;
		// eTransferSrc is what lets a screenshot copy out of the back buffer. It is requested only
		// when the surface supports it, since it is not guaranteed by the specification.
		CreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
		if ((Capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc) == vk::ImageUsageFlagBits::eTransferSrc)
		{
			CreateInfo.imageUsage |= vk::ImageUsageFlagBits::eTransferSrc;
		}
		else
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_RHI, "Surface does not support TRANSFER_SRC; back buffer capture is unavailable");
		}
		CreateInfo.preTransform = Capabilities.currentTransform;
		CreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
		CreateInfo.presentMode = ChosenPresentMode;
		CreateInfo.clipped = VK_TRUE;

		const std::array<uint32_t, 2> QueueFamilies = { GraphicsQueueFamily, PresentQueueFamily };
		if (GraphicsQueueFamily != PresentQueueFamily)
		{
			CreateInfo.imageSharingMode = vk::SharingMode::eConcurrent;
			CreateInfo.queueFamilyIndexCount = static_cast<uint32_t>(QueueFamilies.size());
			CreateInfo.pQueueFamilyIndices = QueueFamilies.data();
		}
		else
		{
			CreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
		}

		const vk::Device CppDevice(LogicalDevice);
		vk::SwapchainKHR NewSwapChain;
		if (CppDevice.createSwapchainKHR(&CreateInfo, nullptr, &NewSwapChain) != vk::Result::eSuccess)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "vkCreateSwapchainKHR failed");
			return false;
		}

		SwapChain = NewSwapChain;
		SurfaceFormat = static_cast<VkFormat>(ChosenFormat.format);
		PresentMode = static_cast<VkPresentModeKHR>(ChosenPresentMode);
		BackBufferWidth = Extent.width;
		BackBufferHeight = Extent.height;
		BackBufferFormat = ToNvrhiFormat(SurfaceFormat);

		if (BackBufferFormat == nvrhi::Format::UNKNOWN)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "Unsupported surface format {}", static_cast<int32>(SurfaceFormat));
			return false;
		}

		return WrapSwapChainImages();
	}

	bool FVulkanDeviceManager::WrapSwapChainImages()
	{
		const vk::Device CppDevice(LogicalDevice);
		const std::vector<vk::Image> Images = CppDevice.getSwapchainImagesKHR(vk::SwapchainKHR(SwapChain));

		SwapChainImages.clear();
		SwapChainImages.reserve(Images.size());
		BackBuffers.assign(Images.size(), nullptr);

		for (SizeType Index = 0; Index < Images.size(); ++Index)
		{
			SwapChainImages.push_back(static_cast<VkImage>(Images[Index]));

			const nvrhi::TextureDesc TextureDesc = nvrhi::TextureDesc()
			                                           .setDimension(nvrhi::TextureDimension::Texture2D)
			                                           .setFormat(BackBufferFormat)
			                                           .setWidth(BackBufferWidth)
			                                           .setHeight(BackBufferHeight)
			                                           .setIsRenderTarget(true)
			                                           .setInitialState(nvrhi::ResourceStates::Present)
			                                           .setKeepInitialState(true)
			                                           .setDebugName("BackBuffer");

			BackBuffers[Index] =
			    Device->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(SwapChainImages[Index]), TextureDesc);
			if (BackBuffers[Index] == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "createHandleForNativeTexture failed for swap chain image {}", Index);
				return false;
			}
		}

		CurrentBackBufferIndex = 0;
		return RebuildFramebuffers();
	}

	bool FVulkanDeviceManager::CreateSyncObjects()
	{
		const vk::Device CppDevice(LogicalDevice);
		const vk::SemaphoreCreateInfo SemaphoreInfo;

		ImageAvailableSemaphores.resize(MaxFramesInFlight);
		FrameQueries.resize(MaxFramesInFlight);

		for (uint32 Index = 0; Index < MaxFramesInFlight; ++Index)
		{
			vk::Semaphore Acquire;
			if (CppDevice.createSemaphore(&SemaphoreInfo, nullptr, &Acquire) != vk::Result::eSuccess)
			{
				LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "vkCreateSemaphore failed for the acquire semaphore");
				return false;
			}
			ImageAvailableSemaphores[Index] = Acquire;

			FrameQueries[Index] = Device->createEventQuery();
			if (FrameQueries[Index] == nullptr)
			{
				LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "createEventQuery failed");
				return false;
			}
		}

		// One release semaphore per swap chain image: a presented image keeps its semaphore in use
		// until that image is acquired again, so per-frame semaphores would be reused too early.
		return CreateReleaseSemaphores();
	}

	bool FVulkanDeviceManager::CreateReleaseSemaphores()
	{
		const vk::Device CppDevice(LogicalDevice);
		const vk::SemaphoreCreateInfo SemaphoreInfo;

		RenderFinishedSemaphores.resize(SwapChainImages.size());
		for (VkSemaphore& Semaphore : RenderFinishedSemaphores)
		{
			vk::Semaphore Release;
			if (CppDevice.createSemaphore(&SemaphoreInfo, nullptr, &Release) != vk::Result::eSuccess)
			{
				LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "vkCreateSemaphore failed for a release semaphore");
				return false;
			}
			Semaphore = Release;
		}

		return true;
	}

	void FVulkanDeviceManager::DestroySwapChain()
	{
		ReleaseFramebuffers();
		SwapChainImages.clear();

		if (LogicalDevice != VK_NULL_HANDLE)
		{
			// Release semaphores are sized to the image count, so they go with the swap chain.
			for (VkSemaphore Semaphore : RenderFinishedSemaphores)
			{
				vk::Device(LogicalDevice).destroySemaphore(vk::Semaphore(Semaphore));
			}
			RenderFinishedSemaphores.clear();

			if (SwapChain != VK_NULL_HANDLE)
			{
				vk::Device(LogicalDevice).destroySwapchainKHR(vk::SwapchainKHR(SwapChain));
				SwapChain = VK_NULL_HANDLE;
			}
		}
	}

	void FVulkanDeviceManager::Shutdown()
	{
		if (Instance == VK_NULL_HANDLE)
		{
			return;
		}

		WaitForIdle();

		if (LogicalDevice != VK_NULL_HANDLE)
		{
			for (VkSemaphore Semaphore : ImageAvailableSemaphores)
			{
				vk::Device(LogicalDevice).destroySemaphore(vk::Semaphore(Semaphore));
			}
		}
		ImageAvailableSemaphores.clear();
		FrameQueries.clear();

		// Also releases the per-image release semaphores.
		DestroySwapChain();
		VulkanDevice = nullptr;
		Device = nullptr;

		const vk::Device CppDevice(LogicalDevice);

		if (LogicalDevice != VK_NULL_HANDLE)
		{
			CppDevice.destroy();
			LogicalDevice = VK_NULL_HANDLE;
		}

		const vk::Instance CppInstance(Instance);
		if (Surface != VK_NULL_HANDLE)
		{
			CppInstance.destroySurfaceKHR(vk::SurfaceKHR(Surface));
			Surface = VK_NULL_HANDLE;
		}

		if (DebugMessenger != VK_NULL_HANDLE && VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyDebugUtilsMessengerEXT != nullptr)
		{
			VULKAN_HPP_DEFAULT_DISPATCHER.vkDestroyDebugUtilsMessengerEXT(Instance, DebugMessenger, nullptr);
			DebugMessenger = VK_NULL_HANDLE;
		}

		CppInstance.destroy();
		Instance = VK_NULL_HANDLE;
		PhysicalDevice = VK_NULL_HANDLE;

#if defined(_WIN32)
		if (LoaderModule != nullptr)
		{
			FreeLibrary(static_cast<HMODULE>(LoaderModule));
			LoaderModule = nullptr;
		}
#endif

		LIME_LOG_INFO(LIME_LOG_CATEGORY_RHI, "Vulkan device destroyed");
	}

	bool FVulkanDeviceManager::BeginFrame()
	{
		if (SwapChain == VK_NULL_HANDLE || BackBufferWidth == 0 || BackBufferHeight == 0)
		{
			return false;
		}

		// Throttle the CPU to MaxFramesInFlight before reusing this slot's semaphores.
		if (FrameQueries[FrameIndex] != nullptr)
		{
			Device->waitEventQuery(FrameQueries[FrameIndex]);
			Device->resetEventQuery(FrameQueries[FrameIndex]);
		}

		const vk::Device CppDevice(LogicalDevice);
		uint32_t ImageIndex = 0;
		const vk::Result AcquireResult = CppDevice.acquireNextImageKHR(
		    vk::SwapchainKHR(SwapChain), UINT64_MAX, vk::Semaphore(ImageAvailableSemaphores[FrameIndex]), vk::Fence(), &ImageIndex);

		if (AcquireResult == vk::Result::eErrorOutOfDateKHR)
		{
			RecreateSwapChain(BackBufferWidth, BackBufferHeight);
			return false;
		}
		if (AcquireResult != vk::Result::eSuccess && AcquireResult != vk::Result::eSuboptimalKHR)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "vkAcquireNextImageKHR failed ({})", static_cast<int32>(AcquireResult));
			return false;
		}

		CurrentBackBufferIndex = ImageIndex;

		// NVRHI owns queue submission, so both semaphores are registered up front and ride along with
		// the renderer's real submission later this frame.
		if (VulkanDevice != nullptr)
		{
			VulkanDevice->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, ImageAvailableSemaphores[FrameIndex], 0);
			VulkanDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, RenderFinishedSemaphores[CurrentBackBufferIndex], 0);
		}

		bFrameAcquired = true;
		return true;
	}

	void FVulkanDeviceManager::Present()
	{
		if (!bFrameAcquired || SwapChain == VK_NULL_HANDLE)
		{
			return;
		}
		bFrameAcquired = false;

		// Guarantees the pending semaphore operations reach the queue even when nothing was drawn.
		Device->executeCommandLists(nullptr, 0, nvrhi::CommandQueue::Graphics);
		if (FrameQueries[FrameIndex] != nullptr)
		{
			Device->setEventQuery(FrameQueries[FrameIndex], nvrhi::CommandQueue::Graphics);
		}

		const vk::Semaphore WaitSemaphore(RenderFinishedSemaphores[CurrentBackBufferIndex]);
		const vk::SwapchainKHR CppSwapChain(SwapChain);
		const uint32_t ImageIndex = CurrentBackBufferIndex;

		vk::PresentInfoKHR PresentInfo;
		PresentInfo.waitSemaphoreCount = 1;
		PresentInfo.pWaitSemaphores = &WaitSemaphore;
		PresentInfo.swapchainCount = 1;
		PresentInfo.pSwapchains = &CppSwapChain;
		PresentInfo.pImageIndices = &ImageIndex;

		const vk::Result PresentResult = vk::Queue(PresentQueue).presentKHR(&PresentInfo);
		if (PresentResult == vk::Result::eErrorOutOfDateKHR || PresentResult == vk::Result::eSuboptimalKHR)
		{
			bSwapChainOutOfDate = true;
		}
		else if (PresentResult != vk::Result::eSuccess)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "vkQueuePresentKHR failed ({})", static_cast<int32>(PresentResult));
		}

		FrameIndex = (FrameIndex + 1) % MaxFramesInFlight;
		Device->runGarbageCollection();

		if (bSwapChainOutOfDate)
		{
			RecreateSwapChain(BackBufferWidth, BackBufferHeight);
		}
	}

	bool FVulkanDeviceManager::RecreateSwapChain(uint32 Width, uint32 Height)
	{
		if (LogicalDevice == VK_NULL_HANDLE)
		{
			return false;
		}

		WaitForIdle();

		// NVRHI defers destruction, so the garbage collector must run before the images are freed.
		if (Device != nullptr)
		{
			Device->runGarbageCollection();
		}

		DestroySwapChain();
		bSwapChainOutOfDate = false;

		if (Width == 0 || Height == 0)
		{
			BackBufferWidth = 0;
			BackBufferHeight = 0;
			return false;
		}

		if (!CreateSwapChain(Width, Height, RequestedImageCount) || !CreateReleaseSemaphores())
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Failed to recreate the Vulkan swap chain");
			return false;
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_RHI, "Swap chain recreated at {}x{}", BackBufferWidth, BackBufferHeight);
		return true;
	}

	void FVulkanDeviceManager::ResizeSwapChain(uint32 Width, uint32 Height)
	{
		if (Width == BackBufferWidth && Height == BackBufferHeight)
		{
			return;
		}
		RecreateSwapChain(Width, Height);
	}

	void FVulkanDeviceManager::WaitForIdle()
	{
		if (Device != nullptr)
		{
			Device->waitForIdle();
		}

		if (LogicalDevice != VK_NULL_HANDLE)
		{
			vk::Device(LogicalDevice).waitIdle();
		}
	}
} // namespace Lime
