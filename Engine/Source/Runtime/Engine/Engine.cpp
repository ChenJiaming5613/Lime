#include "Engine/Engine.h"

#include "Core/Logging/LogManager.h"
#include "Platform/PlatformPaths.h"
#include "Renderer/RenderPassRegistry.h"

namespace Lime
{
	FEngine::~FEngine() = default;

	int32 FEngine::Run(const FProjectSettings& InSettings)
	{
		Settings = InSettings;

		if (!Initialize())
		{
			Shutdown();
			return 1;
		}

		Timer.Reset();
		while (!bExitRequested && !Window.ShouldClose())
		{
			Tick();
		}

		Shutdown();
		return 0;
	}

	bool FEngine::Initialize()
	{
		Application = FApplicationFactory::Create();

		FWindowDesc WindowDesc;
		WindowDesc.Title = Settings.WindowTitle;
		WindowDesc.Width = Settings.WindowWidth;
		WindowDesc.Height = Settings.WindowHeight;
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

		DeviceManager = CreateDeviceManager(Settings.Backend);
		if (DeviceManager == nullptr)
		{
			return false;
		}

		FDeviceCreationDesc DeviceDesc;
		DeviceDesc.Backend = Settings.Backend;
		DeviceDesc.BackBufferCount = Settings.BackBufferCount;
		DeviceDesc.bVSync = Settings.bVSync;
		DeviceDesc.bEnableDebugRuntime = Settings.IsValidationEnabled();
		DeviceDesc.bEnableNvrhiValidation = Settings.IsValidationEnabled();
		if (!DeviceManager->Initialize(Window, DeviceDesc))
		{
			return false;
		}

		if (!Renderer.Initialize(*DeviceManager))
		{
			return false;
		}

		// Project shaders live in a subdirectory named after the project and take precedence, so a
		// project can override a built-in shader by shipping one with the same relative path. It is a
		// sibling of Shaders/Engine, which the renderer already registered.
		Renderer.GetShaderLibrary().AddSearchRoot(FPlatformPaths::GetShaderDirectory() / Settings.ProjectName);

#if LIME_WITH_EDITOR
		bEditorEnabled = Settings.bEnableEditor;
		// The editor also switches the renderer to offscreen scene rendering.
		if (bEditorEnabled && !Editor.Initialize(Window, Renderer, Settings))
		{
			bEditorEnabled = false;
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_CORE, "Editor initialization failed; continuing without it");
		}
#else
		bEditorEnabled = false;
#endif

		// Passes come from the registry, so ordering is by declared priority rather than by
		// registration site. The editor's ImGui pass is registered the same way and sorts last.
		FRenderPassRegistry::Get().InstantiateAll(Renderer);

		if (!Application->OnStartup(*this))
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_CORE, "Application startup failed");
			return false;
		}

#if LIME_WITH_AUTOMATION
		// Last, so the first command already sees a fully constructed engine. A failure here is not
		// fatal: the engine simply runs without remote control.
		if (Settings.bEnableAutomation && !StartAutomation())
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "Continuing without the automation server");
		}
#endif

		LIME_LOG_INFO(LIME_LOG_CATEGORY_CORE, "Startup complete on {}", DeviceManager->GetAdapterName());
		return true;
	}

#if LIME_WITH_AUTOMATION
	bool FEngine::StartAutomation()
	{
		Screenshots.Initialize(*DeviceManager, Renderer);

		// Starts as a copy of what is running, so a client that saves without editing anything writes
		// the current configuration back rather than the code defaults.
		PendingSettings = Settings;

		FAutomationContext Context;
		Context.Renderer = &Renderer;
		Context.DeviceManager = DeviceManager.get();
		Context.LogBuffer = &FLogManager::Get().GetRingBuffer();
		Context.Screenshots = &Screenshots;
		Context.ProjectName = Settings.ProjectName;
#if LIME_WITH_EDITOR
		Context.Editor = GetEditor();
#endif

		// Delegates rather than direct access, so LimeAutomation stays below LimeRuntime and does not
		// need to know about FEngine or FProjectSettings.
		Context.RequestExit = [this] { RequestExit(); };
		// Reports the configuration this session is running with, which never changes after startup.
		Context.QuerySettings = [this] { return Settings.ToJson(); };
		// Edits the pending file rather than the live configuration: every setting is consumed once at
		// startup, so there is nothing to apply mid-session. Kept separate from Settings so a query
		// still describes what is actually running.
		Context.ApplySettings = [this](const FJson& Json, std::string& OutError) { return PendingSettings.ApplyJson(Json, OutError); };
		Context.QueryPendingSettings = [this] { return PendingSettings.ToJson(); };
		Context.SaveSettings = [this] { return PendingSettings.SaveToFile(); };

		FAutomationServerDesc Desc;
		Desc.Port = static_cast<uint16>(Settings.AutomationPort);
		return Automation.Initialize(Desc, std::move(Context));
	}
#endif

	void FEngine::Tick()
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
		Application->OnUpdate(DeltaSeconds);

#if LIME_WITH_AUTOMATION
		// Before the UI is built, so a command that changes a panel or a pass setting is reflected in
		// the frame this same tick produces. That is what makes "set then capture" observable.
		if (Automation.IsRunning())
		{
			Automation.UpdateContext(DeltaSeconds, Timer.GetFramesPerSecond(), Timer.GetTotalSeconds(), Timer.GetFrameCount());
			Automation.Tick();
		}
#endif

		if (Window.IsMinimized())
		{
#if LIME_WITH_AUTOMATION
			// No frame is coming, so a deferred capture would otherwise wait until the window is
			// restored. Failing it keeps the client from blocking on a frame that never renders.
			Automation.FlushDeferred();
#endif
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
			EditorContext.Renderer = &Renderer;

			Editor.DrawUI(EditorContext);
			Editor.EndFrame();
		}
#endif

		if (!DeviceManager->BeginFrame())
		{
#if LIME_WITH_AUTOMATION
			// The frame was skipped, so a pending capture has nothing to read and must not keep its
			// caller waiting for the next one.
			Automation.FlushDeferred();
#endif
			return;
		}

		if (Renderer.BeginFrame(DeltaSeconds, Timer.GetTotalSeconds()))
		{
			// Two stages: the scene goes to the viewport target in editor mode and straight to the
			// back buffer otherwise, while editor UI passes always target the back buffer.
			Renderer.RenderScene();
			Renderer.RenderEditorUI();
			Renderer.EndFrame();
		}

#if LIME_WITH_EDITOR
		// The panel size is only known once the UI has been laid out, so the request is queued here
		// and applied by the next BeginFrame.
		if (bEditorEnabled)
		{
			Editor.SubmitViewportSize();
		}
#endif

#if LIME_WITH_AUTOMATION
		// After submission but before Present: the back buffer still holds this frame, so a capture
		// returns what was just drawn rather than the previous image.
		Automation.FlushDeferred();
#endif

		DeviceManager->Present();
	}

	void FEngine::Shutdown()
	{
#if LIME_WITH_AUTOMATION
		// First: its handlers hold raw pointers into everything below, and a connection thread could
		// otherwise still be dispatching while those are being destroyed.
		Automation.Shutdown();
		Screenshots.Shutdown();
#endif

		if (Application != nullptr)
		{
			Application->OnShutdown();
		}

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
		Application.reset();

		LIME_LOG_INFO(LIME_LOG_CATEGORY_CORE, "LimeEngine shut down after {} frame(s)", Timer.GetFrameCount());
	}
} // namespace Lime
