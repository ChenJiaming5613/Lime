// GLFW window wrapper. Created with GLFW_NO_API since NVRHI owns the graphics device.

#pragma once

#include "Core/CoreTypes.h"

#include <functional>
#include <string>

struct GLFWwindow;

namespace Lime
{
	struct FWindowDesc
	{
		std::string Title = "LimeEngine";
		uint32 Width = 1600;
		uint32 Height = 900;
		bool bResizable = true;
		bool bCentered = true;
	};

	class FWindow
	{
	public:
		using FResizeCallback = std::function<void(uint32 Width, uint32 Height)>;

		FWindow() = default;
		~FWindow();

		LIME_NON_COPYABLE(FWindow);
		LIME_NON_MOVABLE(FWindow);

		bool Initialize(const FWindowDesc& Desc);
		void Shutdown();

		void PollEvents();
		void RequestClose();
		bool ShouldClose() const;

		uint32 GetWidth() const { return Width; }
		uint32 GetHeight() const { return Height; }
		// True while the window is minimized; callers should skip rendering in that case.
		bool IsMinimized() const { return Width == 0 || Height == 0; }

		GLFWwindow* GetHandle() const { return Handle; }
		// HWND on Windows, needed to create the D3D12 swap chain.
		void* GetNativeHandle() const;

		void SetTitle(const std::string& Title);
		void SetResizeCallback(FResizeCallback Callback) { OnResize = std::move(Callback); }

		// Initializes GLFW once per process; safe to call repeatedly.
		static bool InitializeSubsystem();
		static void ShutdownSubsystem();
		// Vulkan instance extensions GLFW requires for surface creation.
		static const char** GetRequiredVulkanExtensions(uint32& OutCount);

	private:
		static void FramebufferSizeCallback(GLFWwindow* Window, int NewWidth, int NewHeight);

		GLFWwindow* Handle = nullptr;
		uint32 Width = 0;
		uint32 Height = 0;
		FResizeCallback OnResize;
	};
} // namespace Lime
