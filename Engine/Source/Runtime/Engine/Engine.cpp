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
		// project can override a built-in shader by shipping one with the same relative path.
		Renderer.GetShaderLibrary().AddSearchRoot(FPlatformPaths::GetShaderDirectory() / Settings.ProjectName);

#if LIME_WITH_EDITOR
		bEditorEnabled = Settings.bEnableEditor;
		if (bEditorEnabled && !Editor.Initialize(Window))
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

		LIME_LOG_INFO(LIME_LOG_CATEGORY_CORE, "Startup complete on {}", DeviceManager->GetAdapterName());
		return true;
	}

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
			EditorContext.Renderer = &Renderer;

			Editor.DrawUI(EditorContext);
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
			Renderer.EndFrame();
		}

		DeviceManager->Present();
	}

	void FEngine::Shutdown()
	{
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
