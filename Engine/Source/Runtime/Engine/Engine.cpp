#include "Engine/Engine.h"

#include "Asset/GltfImporter.h"
#include "Core/Logging/LogManager.h"
#include "Core/Math/MathUtils.h"
#include "Platform/PlatformPaths.h"
#include "Renderer/Passes/BlinnPhongForwardPass.h"
#include "Renderer/RenderPassRegistry.h"
#include "Scene/SceneBuilder.h"

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

		// Registered before instantiating, so engine provided passes and project passes end up in one list
		// ordered by priority. An explicit call rather than a static initializer, because the built-in
		// passes live in a static library where the linker may discard a registration-only object file.
		RegisterBuiltinRenderPasses();

		// Passes come from the registry, so ordering is by declared priority rather than by
		// registration site. The editor's ImGui pass is registered the same way and sorts last.
		FRenderPassRegistry::Get().InstantiateAll(Renderer);

		// After the passes exist, so the scene pass picks the data up on its first frame.
		LoadConfiguredScene();

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

	void FEngine::LoadConfiguredScene()
	{
		// Applied whether or not a scene loads, so a project drawing through its own passes still gets the
		// configured camera.
		Camera.SetFieldOfView(DegreesToRadians(Settings.CameraFieldOfView));

		FFlyCameraSettings CameraSettings = CameraController.GetSettings();
		CameraSettings.MoveSpeed = Settings.CameraMoveSpeed;
		CameraController.SetSettings(CameraSettings);

		Renderer.SetScene(&Scene);
		Renderer.SetCamera(&Camera);

		if (FBlinnPhongForwardPass* Pass = Renderer.FindPass<FBlinnPhongForwardPass>())
		{
			Pass->GetSettings().LightIntensity = Settings.LightIntensity;
			Pass->GetSettings().AmbientStrength = Settings.AmbientStrength;
		}

		if (Settings.ScenePath.empty())
		{
			// Not a warning: a project without a scene is a normal configuration.
			CameraController.SyncFromCamera(Camera);
			return;
		}

		const std::filesystem::path Resolved = FPlatformPaths::ResolveAssetPath(Settings.ScenePath);
		if (Resolved.empty())
		{
			// Logged once, here, rather than from the render loop. The engine continues with an empty scene:
			// a mistyped path should not stop the editor from opening.
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_SCENE, "Scene '{}' was not found in the project or Assets directory",
			               Settings.ScenePath);
			CameraController.SyncFromCamera(Camera);
			return;
		}

		const FGltfImportResult Import = FGltfImporter::LoadFromFile(Resolved);
		if (!Import.bSucceeded)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_SCENE, "Failed to load '{}': {}", Resolved.string(), Import.Message);
			CameraController.SyncFromCamera(Camera);
			return;
		}

		if (!Import.Message.empty())
		{
			// tinygltf reports recoverable problems through a warning, worth surfacing but not fatal.
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_SCENE, "While loading '{}': {}", Resolved.string(), Import.Message);
		}

		BuildScene(Scene, Import.Scene);

		// Framed from the world bounds so a model of any size and position is visible without per model
		// configuration.
		//
		// The direction points from the model towards the camera. glTF authors a model facing -Z, so the
		// camera goes on that side to see its front; slightly above and to one side reads better than dead
		// on, which flattens the shape.
		const FBoundingBox Bounds = Scene.ComputeWorldBounds();
		if (Bounds.bValid)
		{
			Camera.FrameSphere(Bounds.GetCenter(), Bounds.GetLongestEdge() * 0.5f, FVector3{ -0.35f, 0.35f, -1.0f });
		}

		// After framing, so the first drag continues from where the camera was placed rather than snapping
		// back to the default pose.
		CameraController.SyncFromCamera(Camera);

		const FSceneStats Stats = Scene.GetStats();
		LIME_LOG_INFO(LIME_LOG_CATEGORY_SCENE, "Loaded '{}': {} entities, {} meshes, {} triangles", Resolved.filename().string(),
		              Stats.EntityCount, Stats.MeshEntityCount, Stats.TriangleCount);
	}

	void FEngine::UpdateCamera(float DeltaSeconds)
	{
		// Flight may only start while the cursor is over the scene, so a right click on an editor panel
		// operates that panel instead of taking over the view. Once flying, the controller keeps control
		// regardless of where the cursor travels.
		bool bCanStartFlying = true;
#if LIME_WITH_EDITOR
		if (bEditorEnabled)
		{
			bCanStartFlying = Editor.IsViewportHovered();
		}
#endif

		CameraController.Update(Camera, Window, DeltaSeconds, bCanStartFlying);

		// Set every frame because the viewport can be resized at any time; a stale ratio shows as a
		// horizontally stretched image.
		const uint32 Width = Renderer.IsOffscreenRenderingEnabled() ? Renderer.GetViewportTarget().GetWidth()
		                                                           : DeviceManager->GetBackBufferWidth();
		const uint32 Height = Renderer.IsOffscreenRenderingEnabled() ? Renderer.GetViewportTarget().GetHeight()
		                                                             : DeviceManager->GetBackBufferHeight();
		if (Height > 0)
		{
			Camera.SetAspectRatio(static_cast<float>(Width) / static_cast<float>(Height));
		}

		// Rebuilt after the camera moves so that the matrices the passes read are for this frame.
		Scene.UpdateTransforms();
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

		// Before the UI is built, so the hierarchy panel and the scene pass see the same transforms this
		// frame rather than the previous one's.
		UpdateCamera(DeltaSeconds);

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

#if LIME_WITH_EDITOR
		// Brackets Present so the UI test engine can measure frame timing. Forwarded through the
		// editor layer, which owns the test engine, so this file needs none of its headers.
		if (bEditorEnabled)
		{
			Editor.PreSwap();
		}
#endif

		DeviceManager->Present();

#if LIME_WITH_EDITOR
		if (bEditorEnabled)
		{
			Editor.PostSwap();
		}
#endif
	}

	void FEngine::Shutdown()
	{
		// Before anything else, so the cursor can never be left hidden if the loop exited while flying.
		CameraController.Release(Window);

		// The renderer must stop reading them before the scene and camera go out of scope.
		Renderer.SetScene(nullptr);
		Renderer.SetCamera(nullptr);

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
