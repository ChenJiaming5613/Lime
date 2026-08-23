#include "Platform/Window.h"

#include "Core/Logging/LogManager.h"
#include "Platform/Input.h"

#include <GLFW/glfw3.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace Lime
{
	namespace
	{
		int32 GlfwRefCount = 0;

		void GlfwErrorCallback(int ErrorCode, const char* Description)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_PLATFORM, "GLFW error {}: {}", ErrorCode, Description);
		}
	} // namespace

	bool FWindow::InitializeSubsystem()
	{
		if (GlfwRefCount > 0)
		{
			++GlfwRefCount;
			return true;
		}

		glfwSetErrorCallback(&GlfwErrorCallback);
		if (glfwInit() != GLFW_TRUE)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_PLATFORM, "glfwInit failed");
			return false;
		}

		++GlfwRefCount;
		LIME_LOG_INFO(LIME_LOG_CATEGORY_PLATFORM, "GLFW {} initialized", glfwGetVersionString());
		return true;
	}

	void FWindow::ShutdownSubsystem()
	{
		if (GlfwRefCount == 0)
		{
			return;
		}

		if (--GlfwRefCount == 0)
		{
			glfwTerminate();
		}
	}

	const char** FWindow::GetRequiredVulkanExtensions(uint32& OutCount)
	{
		OutCount = 0;
		uint32_t GlfwCount = 0;
		const char** Extensions = glfwGetRequiredInstanceExtensions(&GlfwCount);
		OutCount = static_cast<uint32>(GlfwCount);
		return Extensions;
	}

	FWindow::~FWindow()
	{
		Shutdown();
	}

	bool FWindow::Initialize(const FWindowDesc& Desc)
	{
		if (Handle != nullptr)
		{
			return true;
		}

		if (!InitializeSubsystem())
		{
			return false;
		}

		// NVRHI creates and owns the device, so GLFW must not create any graphics context.
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, Desc.bResizable ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

		Handle = glfwCreateWindow(static_cast<int>(Desc.Width), static_cast<int>(Desc.Height), Desc.Title.c_str(), nullptr, nullptr);
		if (Handle == nullptr)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_PLATFORM, "Failed to create a {}x{} window", Desc.Width, Desc.Height);
			ShutdownSubsystem();
			return false;
		}

		if (Desc.bCentered)
		{
			if (GLFWmonitor* Monitor = glfwGetPrimaryMonitor())
			{
				if (const GLFWvidmode* Mode = glfwGetVideoMode(Monitor))
				{
					int MonitorX = 0;
					int MonitorY = 0;
					glfwGetMonitorPos(Monitor, &MonitorX, &MonitorY);
					glfwSetWindowPos(Handle, MonitorX + (Mode->width - static_cast<int>(Desc.Width)) / 2,
					                 MonitorY + (Mode->height - static_cast<int>(Desc.Height)) / 2);
				}
			}
		}

		int FramebufferWidth = 0;
		int FramebufferHeight = 0;
		glfwGetFramebufferSize(Handle, &FramebufferWidth, &FramebufferHeight);
		Width = static_cast<uint32>(FramebufferWidth);
		Height = static_cast<uint32>(FramebufferHeight);

		glfwSetWindowUserPointer(Handle, this);
		glfwSetFramebufferSizeCallback(Handle, &FWindow::FramebufferSizeCallback);
		// Installed here, which must stay earlier than the editor's ImGui initialization. ImGui's GLFW
		// backend chains: it saves whatever callbacks already exist and forwards to them after handling an
		// event. Attaching after ImGui would instead overwrite its callbacks, and the UI would stop
		// receiving keyboard and mouse input.
		FInput::Get().AttachToWindow(Handle);

		glfwShowWindow(Handle);

		LIME_LOG_INFO(LIME_LOG_CATEGORY_PLATFORM, "Window created: {}x{} '{}'", Width, Height, Desc.Title);
		return true;
	}

	void FWindow::Shutdown()
	{
		if (Handle == nullptr)
		{
			return;
		}

		FInput::Get().DetachFromWindow(Handle);
		glfwDestroyWindow(Handle);
		Handle = nullptr;
		Width = 0;
		Height = 0;
		ShutdownSubsystem();
	}

	void FWindow::SetCursorMode(ECursorMode Mode)
	{
		if (Handle == nullptr || CursorMode == Mode)
		{
			return;
		}

		CursorMode = Mode;
		glfwSetInputMode(Handle, GLFW_CURSOR, Mode == ECursorMode::Captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

		// Capturing warps the cursor, which GLFW reports as one large jump. Reseeding the cached position
		// makes that frame's delta zero, so the view does not snap when a fly camera starts or stops.
		double CursorX = 0.0;
		double CursorY = 0.0;
		glfwGetCursorPos(Handle, &CursorX, &CursorY);
		FInput::Get().ResetMousePosition({ static_cast<float>(CursorX), static_cast<float>(CursorY) });
	}

	void FWindow::PollEvents()
	{
		FInput::Get().BeginFrame();
		glfwPollEvents();
	}

	void FWindow::RequestClose()
	{
		if (Handle != nullptr)
		{
			glfwSetWindowShouldClose(Handle, GLFW_TRUE);
		}
	}

	bool FWindow::ShouldClose() const
	{
		return Handle == nullptr || glfwWindowShouldClose(Handle) == GLFW_TRUE;
	}

	void* FWindow::GetNativeHandle() const
	{
#if defined(_WIN32)
		return Handle != nullptr ? static_cast<void*>(glfwGetWin32Window(Handle)) : nullptr;
#else
		return nullptr;
#endif
	}

	void FWindow::SetTitle(const std::string& Title)
	{
		if (Handle != nullptr)
		{
			glfwSetWindowTitle(Handle, Title.c_str());
		}
	}

	void FWindow::FramebufferSizeCallback(GLFWwindow* Window, int NewWidth, int NewHeight)
	{
		auto* Self = static_cast<FWindow*>(glfwGetWindowUserPointer(Window));
		if (Self == nullptr)
		{
			return;
		}

		Self->Width = static_cast<uint32>(NewWidth > 0 ? NewWidth : 0);
		Self->Height = static_cast<uint32>(NewHeight > 0 ? NewHeight : 0);

		if (Self->OnResize)
		{
			Self->OnResize(Self->Width, Self->Height);
		}
	}
} // namespace Lime
