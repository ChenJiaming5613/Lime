#include "Engine/Engine.h"

#include "Core/Logging/LogManager.h"
#include "Platform/PlatformPaths.h"

namespace Lime
{
	FEngine::~FEngine() = default;

	int32 FEngine::Run(const FEngineConfig& InConfig, ILimeApplication& Application)
	{
		Config = InConfig;

		if (!Initialize(Application))
		{
			Shutdown(Application);
			return 1;
		}

		Timer.Reset();
		while (!bExitRequested && !Window.ShouldClose())
		{
			Tick(Application);
		}

		Shutdown(Application);
		return 0;
	}

	bool FEngine::Initialize(ILimeApplication& Application)
	{
		FLogConfig LogConfig;
		LogConfig.FileName = FPlatformPaths::GetSavedDirectory() / "Logs" / "LimeEngine.log";
		FLogManager::Get().Initialize(LogConfig);

		LIME_LOG_INFO(LIME_LOG_CATEGORY_CORE, "LimeEngine starting ({}, {}x{}, editor {})", ToString(Config.Backend), Config.WindowWidth,
		              Config.WindowHeight, Config.bEnableEditor ? "on" : "off");

		FWindowDesc WindowDesc;
		WindowDesc.Title = Config.WindowTitle;
		WindowDesc.Width = Config.WindowWidth;
		WindowDesc.Height = Config.WindowHeight;
		if (!Window.Initialize(WindowDesc))
		{
			return false;
		}

		// The swap chain is only touched at frame boundaries, so resizes are deferred.
		Window.SetResizeCallback(
		    [this](uint32 NewWidth, uint32 NewHeight)
		    {
			    bResizePending = true;
			    PendingWidth = NewWidth;
			    PendingHeight = NewHeight;
		    });

		DeviceManager = CreateDeviceManager(Config.Backend);
		if (DeviceManager == nullptr)
		{
			return false;
		}

		FDeviceCreationDesc DeviceDesc;
		DeviceDesc.Backend = Config.Backend;
		DeviceDesc.BackBufferCount = Config.BackBufferCount;
		DeviceDesc.bVSync = Config.bVSync;
		DeviceDesc.bEnableDebugRuntime = Config.bEnableDebugRuntime;
		DeviceDesc.bEnableNvrhiValidation = Config.bEnableNvrhiValidation;
		if (!DeviceManager->Initialize(Window, DeviceDesc))
		{
			return false;
		}

		if (!Renderer.Initialize(*DeviceManager))
		{
			return false;
		}

#if LIME_WITH_EDITOR
		// The editor registers the ImGui render pass, so it must come before the application adds
		// passes that should draw underneath the UI.
		bEditorEnabled = Config.bEnableEditor;
		if (bEditorEnabled && !Editor.Initialize(Window, Renderer))
		{
			bEditorEnabled = false;
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_CORE, "Editor initialization failed; continuing without it");
		}
#else
		bEditorEnabled = false;
#endif

		if (!Application.OnInitialize(*this))
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_CORE, "Application initialization failed");
			return false;
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_CORE, "Startup complete on {}", DeviceManager->GetAdapterName());
		return true;
	}

	void FEngine::Tick(ILimeApplication& Application)
	{
		Window.PollEvents();

		if (bResizePending)
		{
			bResizePending = false;
			// Pass pipelines pin the back buffer textures, so they are dropped first.
			Renderer.ReleaseFramebufferDependentResources();
			DeviceManager->ResizeSwapChain(PendingWidth, PendingHeight);
		}

		const float DeltaSeconds = Timer.Tick();
		Application.OnUpdate(DeltaSeconds);

		if (Window.IsMinimized())
		{
			return;
		}

#if LIME_WITH_EDITOR
		// The UI is built before rendering so the ImGui pass has draw data ready this frame.
		if (bEditorEnabled)
		{
			Editor.BeginFrame();

			FEditorContext EditorContext;
			EditorContext.DeltaSeconds = DeltaSeconds;
			EditorContext.FramesPerSecond = Timer.GetFramesPerSecond();
			EditorContext.ViewportWidth = DeviceManager->GetBackBufferWidth();
			EditorContext.ViewportHeight = DeviceManager->GetBackBufferHeight();
			EditorContext.BackendName = ToString(DeviceManager->GetBackend());
			EditorContext.AdapterName = DeviceManager->GetAdapterName();
			EditorContext.LogBuffer = &FLogManager::Get().GetRingBuffer();

			Editor.DrawUI(EditorContext);
			Application.OnDrawEditorUI();
			Editor.EndFrame();
		}
#endif

		if (!DeviceManager->BeginFrame())
		{
			return;
		}

		if (Renderer.BeginFrame(DeltaSeconds, Timer.GetTotalSeconds()))
		{
			Renderer.RenderPasses();
			Application.OnRender(Renderer);
			Renderer.EndFrame();
		}

		DeviceManager->Present();
	}

	void FEngine::Shutdown(ILimeApplication& Application)
	{
		Application.OnShutdown();

		// Reverse initialization order, and the GPU must be idle before releasing resources.
		if (DeviceManager != nullptr)
		{
			DeviceManager->WaitForIdle();
		}

#if LIME_WITH_EDITOR
		Editor.Shutdown();
		bEditorEnabled = false;
#endif

		Renderer.Shutdown();

		if (DeviceManager != nullptr)
		{
			DeviceManager->Shutdown();
			DeviceManager.reset();
		}

		Window.Shutdown();

		LIME_LOG_INFO(LIME_LOG_CATEGORY_CORE, "LimeEngine shut down after {} frame(s)", Timer.GetFrameCount());
		FLogManager::Get().Shutdown();
	}
} // namespace Lime
