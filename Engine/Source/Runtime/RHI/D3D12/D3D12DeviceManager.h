// Direct3D 12 device, command queue and DXGI swap chain.

#pragma once

#include "RHI/DeviceManager.h"

#include <d3d12.h>
#include <dxgi1_6.h>

#include <wrl/client.h>

namespace Lime
{
	class FD3D12DeviceManager final : public FDeviceManagerBase
	{
	public:
		FD3D12DeviceManager() = default;
		~FD3D12DeviceManager() override;

		bool Initialize(FWindow& Window, const FDeviceCreationDesc& Desc) override;
		void Shutdown() override;

		bool BeginFrame() override;
		void Present() override;
		void ResizeSwapChain(uint32 Width, uint32 Height) override;
		void WaitForIdle() override;

		nvrhi::IDevice* GetDevice() const override { return Device; }
		ERHIBackend GetBackend() const override { return ERHIBackend::D3D12; }

	private:
		template<typename T>
		using TComPtr = Microsoft::WRL::ComPtr<T>;

		bool CreateDeviceAndQueue(bool bEnableDebugRuntime);
		bool CreateSwapChain(void* WindowHandle, uint32 Width, uint32 Height, uint32 BufferCount);
		bool WrapBackBuffers();
		// Waits until the GPU has finished the frame that previously used this back buffer slot.
		void WaitForFrame(uint32 FrameIndex);

		TComPtr<IDXGIFactory6> Factory;
		TComPtr<IDXGIAdapter1> Adapter;
		TComPtr<ID3D12Device> D3DDevice;
		TComPtr<ID3D12CommandQueue> GraphicsQueue;
		TComPtr<IDXGISwapChain3> SwapChain;
		TComPtr<ID3D12Fence> Fence;
		void* FenceEvent = nullptr;

		std::vector<TComPtr<ID3D12Resource>> BackBufferResources;
		std::vector<uint64> FrameFenceValues;
		uint64 NextFenceValue = 1;
		uint32 SwapChainFlags = 0;
		bool bFrameAcquired = false;
	};
} // namespace Lime
