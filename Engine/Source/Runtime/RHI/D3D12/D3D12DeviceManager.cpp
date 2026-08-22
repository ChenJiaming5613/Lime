#include "RHI/D3D12/D3D12DeviceManager.h"

#include "Core/Logging/LogManager.h"
#include "Platform/Window.h"

#include <Windows.h>
#include <nvrhi/d3d12.h>

namespace Lime
{
	namespace
	{
		constexpr nvrhi::Format SwapChainFormat = nvrhi::Format::RGBA8_UNORM;
		constexpr DXGI_FORMAT SwapChainDxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

		std::string ToUtf8(const wchar_t* Wide)
		{
			if (Wide == nullptr)
			{
				return {};
			}

			const int Length = WideCharToMultiByte(CP_UTF8, 0, Wide, -1, nullptr, 0, nullptr, nullptr);
			if (Length <= 1)
			{
				return {};
			}

			std::string Result(static_cast<SizeType>(Length - 1), '\0');
			WideCharToMultiByte(CP_UTF8, 0, Wide, -1, Result.data(), Length, nullptr, nullptr);
			return Result;
		}
	} // namespace

	FD3D12DeviceManager::~FD3D12DeviceManager()
	{
		Shutdown();
	}

	bool FD3D12DeviceManager::Initialize(FWindow& Window, const FDeviceCreationDesc& Desc)
	{
		bVSync = Desc.bVSync;

		if (!CreateDeviceAndQueue(Desc.bEnableDebugRuntime))
		{
			return false;
		}

		void* WindowHandle = Window.GetNativeHandle();
		if (WindowHandle == nullptr)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "Failed to obtain the native window handle");
			return false;
		}

		if (!CreateSwapChain(WindowHandle, Window.GetWidth(), Window.GetHeight(), Desc.BackBufferCount))
		{
			return false;
		}

		nvrhi::d3d12::DeviceDesc DeviceDesc;
		DeviceDesc.errorCB = &MessageCallback;
		DeviceDesc.pDevice = D3DDevice.Get();
		DeviceDesc.pGraphicsCommandQueue = GraphicsQueue.Get();

		nvrhi::DeviceHandle NativeDevice = nvrhi::d3d12::createDevice(DeviceDesc);
		if (NativeDevice == nullptr)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "nvrhi::d3d12::createDevice failed");
			return false;
		}

		Device = ApplyValidationLayer(std::move(NativeDevice), Desc.bEnableNvrhiValidation);
		BackBufferFormat = SwapChainFormat;

		if (!WrapBackBuffers())
		{
			return false;
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_RHI, "D3D12 device ready on {} ({}x{}, {} buffers, vsync {})", AdapterName, BackBufferWidth,
		              BackBufferHeight, BackBufferResources.size(), bVSync ? "on" : "off");
		return true;
	}

	bool FD3D12DeviceManager::CreateDeviceAndQueue(bool bEnableDebugRuntime)
	{
		if (bEnableDebugRuntime)
		{
			TComPtr<ID3D12Debug> DebugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&DebugController))))
			{
				DebugController->EnableDebugLayer();
				LIME_LOG_INFO(LIME_LOG_CATEGORY_RHI, "D3D12 debug layer enabled");
			}
			else
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_RHI, "D3D12 debug layer unavailable; install the Graphics Tools feature");
			}
		}

		const UINT FactoryFlags = bEnableDebugRuntime ? DXGI_CREATE_FACTORY_DEBUG : 0;
		if (FAILED(CreateDXGIFactory2(FactoryFlags, IID_PPV_ARGS(&Factory))))
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "CreateDXGIFactory2 failed");
			return false;
		}

		// EnumAdapterByGpuPreference already sorts by capability, so the first hit is the best one.
		for (UINT Index = 0;
		     SUCCEEDED(Factory->EnumAdapterByGpuPreference(Index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&Adapter))); ++Index)
		{
			DXGI_ADAPTER_DESC1 AdapterDesc = {};
			Adapter->GetDesc1(&AdapterDesc);
			if ((AdapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
			{
				Adapter.Reset();
				continue;
			}

			if (SUCCEEDED(D3D12CreateDevice(Adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&D3DDevice))))
			{
				AdapterName = ToUtf8(AdapterDesc.Description);
				break;
			}

			Adapter.Reset();
			D3DDevice.Reset();
		}

		if (D3DDevice == nullptr)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "No Direct3D 12 capable adapter found");
			return false;
		}

		if (bEnableDebugRuntime)
		{
			TComPtr<ID3D12InfoQueue> InfoQueue;
			if (SUCCEEDED(D3DDevice.As(&InfoQueue)))
			{
				InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
				InfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
			}
		}

		D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
		QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		QueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		if (FAILED(D3DDevice->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&GraphicsQueue))))
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "CreateCommandQueue failed");
			return false;
		}
		GraphicsQueue->SetName(L"LimeGraphicsQueue");

		if (FAILED(D3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence))))
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "CreateFence failed");
			return false;
		}

		FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (FenceEvent == nullptr)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "CreateEvent failed");
			return false;
		}

		return true;
	}

	bool FD3D12DeviceManager::CreateSwapChain(void* WindowHandle, uint32 Width, uint32 Height, uint32 BufferCount)
	{
		BackBufferWidth = std::max<uint32>(Width, 1);
		BackBufferHeight = std::max<uint32>(Height, 1);
		SwapChainFlags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

		DXGI_SWAP_CHAIN_DESC1 SwapChainDesc = {};
		SwapChainDesc.Width = BackBufferWidth;
		SwapChainDesc.Height = BackBufferHeight;
		SwapChainDesc.Format = SwapChainDxgiFormat;
		SwapChainDesc.SampleDesc.Count = 1;
		SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		SwapChainDesc.BufferCount = std::max<uint32>(BufferCount, 2);
		SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		SwapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		SwapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		SwapChainDesc.Flags = SwapChainFlags;

		TComPtr<IDXGISwapChain1> SwapChain1;
		HRESULT Result = Factory->CreateSwapChainForHwnd(GraphicsQueue.Get(), static_cast<HWND>(WindowHandle), &SwapChainDesc, nullptr,
		                                                 nullptr, &SwapChain1);
		if (FAILED(Result))
		{
			// Tearing needs DXGI 1.5 plus driver support; fall back without it.
			SwapChainFlags = 0;
			SwapChainDesc.Flags = 0;
			Result = Factory->CreateSwapChainForHwnd(GraphicsQueue.Get(), static_cast<HWND>(WindowHandle), &SwapChainDesc, nullptr, nullptr,
			                                         &SwapChain1);
		}

		if (FAILED(Result))
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "CreateSwapChainForHwnd failed (0x{:08X})", static_cast<uint32>(Result));
			return false;
		}

		if (FAILED(SwapChain1.As(&SwapChain)))
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "IDXGISwapChain3 is not available");
			return false;
		}

		// The engine handles fullscreen through the window, not DXGI.
		Factory->MakeWindowAssociation(static_cast<HWND>(WindowHandle), DXGI_MWA_NO_ALT_ENTER);

		FrameFenceValues.assign(SwapChainDesc.BufferCount, 0);
		return true;
	}

	bool FD3D12DeviceManager::WrapBackBuffers()
	{
		DXGI_SWAP_CHAIN_DESC1 SwapChainDesc = {};
		if (FAILED(SwapChain->GetDesc1(&SwapChainDesc)))
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "IDXGISwapChain3::GetDesc1 failed");
			return false;
		}

		BackBufferResources.assign(SwapChainDesc.BufferCount, nullptr);
		BackBuffers.assign(SwapChainDesc.BufferCount, nullptr);

		for (uint32 Index = 0; Index < SwapChainDesc.BufferCount; ++Index)
		{
			if (FAILED(SwapChain->GetBuffer(Index, IID_PPV_ARGS(&BackBufferResources[Index]))))
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "IDXGISwapChain3::GetBuffer({}) failed", Index);
				return false;
			}

			const nvrhi::TextureDesc TextureDesc = nvrhi::TextureDesc()
			                                           .setDimension(nvrhi::TextureDimension::Texture2D)
			                                           .setFormat(BackBufferFormat)
			                                           .setWidth(BackBufferWidth)
			                                           .setHeight(BackBufferHeight)
			                                           .setIsRenderTarget(true)
			                                           .setInitialState(nvrhi::ResourceStates::Present)
			                                           .setKeepInitialState(true)
			                                           .setDebugName("BackBuffer");

			BackBuffers[Index] = Device->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource,
			                                                          nvrhi::Object(BackBufferResources[Index].Get()), TextureDesc);
			if (BackBuffers[Index] == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "createHandleForNativeTexture failed for back buffer {}", Index);
				return false;
			}
		}

		FrameFenceValues.assign(SwapChainDesc.BufferCount, 0);
		CurrentBackBufferIndex = SwapChain->GetCurrentBackBufferIndex();
		return RebuildFramebuffers();
	}

	void FD3D12DeviceManager::Shutdown()
	{
		if (D3DDevice == nullptr)
		{
			return;
		}

		WaitForIdle();

		ReleaseFramebuffers();
		BackBufferResources.clear();
		Device = nullptr;

		SwapChain.Reset();
		Fence.Reset();
		if (FenceEvent != nullptr)
		{
			CloseHandle(static_cast<HANDLE>(FenceEvent));
			FenceEvent = nullptr;
		}

		GraphicsQueue.Reset();
		D3DDevice.Reset();
		Adapter.Reset();
		Factory.Reset();

		LIME_LOG_INFO(LIME_LOG_CATEGORY_RHI, "D3D12 device destroyed");
	}

	bool FD3D12DeviceManager::BeginFrame()
	{
		if (SwapChain == nullptr || BackBufferWidth == 0 || BackBufferHeight == 0)
		{
			return false;
		}

		CurrentBackBufferIndex = SwapChain->GetCurrentBackBufferIndex();
		WaitForFrame(CurrentBackBufferIndex);
		bFrameAcquired = true;
		return true;
	}

	void FD3D12DeviceManager::Present()
	{
		if (!bFrameAcquired || SwapChain == nullptr)
		{
			return;
		}
		bFrameAcquired = false;

		// NVRHI leaves the back buffer in Present state thanks to keepInitialState.
		const UINT SyncInterval = bVSync ? 1 : 0;
		const UINT PresentFlags = (!bVSync && (SwapChainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0) ? DXGI_PRESENT_ALLOW_TEARING : 0;

		const HRESULT Result = SwapChain->Present(SyncInterval, PresentFlags);
		if (Result == DXGI_ERROR_DEVICE_REMOVED || Result == DXGI_ERROR_DEVICE_RESET)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RHI, "Device removed during Present (0x{:08X})", static_cast<uint32>(Result));
			return;
		}
		if (FAILED(Result))
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Present failed (0x{:08X})", static_cast<uint32>(Result));
			return;
		}

		const uint64 SignalValue = NextFenceValue++;
		GraphicsQueue->Signal(Fence.Get(), SignalValue);
		if (CurrentBackBufferIndex < FrameFenceValues.size())
		{
			FrameFenceValues[CurrentBackBufferIndex] = SignalValue;
		}

		Device->runGarbageCollection();
	}

	void FD3D12DeviceManager::WaitForFrame(uint32 FrameIndex)
	{
		if (FrameIndex >= FrameFenceValues.size())
		{
			return;
		}

		const uint64 TargetValue = FrameFenceValues[FrameIndex];
		if (TargetValue == 0 || Fence->GetCompletedValue() >= TargetValue)
		{
			return;
		}

		Fence->SetEventOnCompletion(TargetValue, static_cast<HANDLE>(FenceEvent));
		WaitForSingleObject(static_cast<HANDLE>(FenceEvent), INFINITE);
	}

	void FD3D12DeviceManager::ResizeSwapChain(uint32 Width, uint32 Height)
	{
		if (SwapChain == nullptr || (Width == BackBufferWidth && Height == BackBufferHeight))
		{
			return;
		}

		if (Width == 0 || Height == 0)
		{
			// Minimized: keep the old swap chain and let BeginFrame skip the frame.
			BackBufferWidth = 0;
			BackBufferHeight = 0;
			return;
		}

		WaitForIdle();

		// Every NVRHI handle referencing a back buffer must be released before ResizeBuffers, and
		// NVRHI defers destruction, so the garbage collector has to run before DXGI sees zero refs.
		ReleaseFramebuffers();
		BackBufferResources.clear();
		Device->runGarbageCollection();

		BackBufferWidth = Width;
		BackBufferHeight = Height;

		const HRESULT Result = SwapChain->ResizeBuffers(0, Width, Height, DXGI_FORMAT_UNKNOWN, SwapChainFlags);
		if (FAILED(Result))
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "ResizeBuffers failed (0x{:08X})", static_cast<uint32>(Result));
			return;
		}

		if (!WrapBackBuffers())
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Failed to rebuild back buffers after resize");
			return;
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_RHI, "Swap chain resized to {}x{}", Width, Height);
	}

	void FD3D12DeviceManager::WaitForIdle()
	{
		if (GraphicsQueue == nullptr || Fence == nullptr || FenceEvent == nullptr)
		{
			return;
		}

		if (Device != nullptr)
		{
			Device->waitForIdle();
		}

		const uint64 SignalValue = NextFenceValue++;
		GraphicsQueue->Signal(Fence.Get(), SignalValue);
		if (Fence->GetCompletedValue() < SignalValue)
		{
			Fence->SetEventOnCompletion(SignalValue, static_cast<HANDLE>(FenceEvent));
			WaitForSingleObject(static_cast<HANDLE>(FenceEvent), INFINITE);
		}

		std::fill(FrameFenceValues.begin(), FrameFenceValues.end(), 0);
	}
} // namespace Lime
