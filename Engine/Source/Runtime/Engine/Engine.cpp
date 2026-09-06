#include "Engine/Engine.h"

#include "Core/Logging/LogManager.h"
#include "Core/Math/MathUtils.h"
#include "Platform/PlatformPaths.h"
#include "RenderGraph/RenderGraphCompiler.h"
#include "RenderGraph/RenderGraphInjection.h"
#include "RenderGraph/RenderGraphJson.h"
#include "Renderer/Passes/BlinnPhongForwardLitPass.h"
#include "Renderer/Passes/BlitPass.h"
#include "Renderer/Passes/BuiltinPasses.h"
#include "Renderer/Passes/EditorUIPass.h"
#include "Renderer/RenderPassRegistry.h"

#include "Asset/GltfImporter.h"
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

		// After the passes exist, because the graph is compiled against what they declare and executed by
		// the instances themselves. A failure here is not fatal: the engine runs with no scene graph, which
		// renders only the editor UI, and the reason is in the log.
		if (!BuildConfiguredRenderGraph())
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "No render graph is active, so only the editor UI will be drawn");
		}

		// After the passes exist, so the scene pass picks the data up on the frame the import lands.
		BeginLoadConfiguredScene();

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

		Context.SetCameraPose = [this](const FVector3& Position, float YawDegrees, float PitchDegrees)
		{
			Camera.SetPosition(Position);
			Camera.SetRotation(DegreesToRadians(YawDegrees), DegreesToRadians(PitchDegrees));
			// The controller carries the pose between frames, so it has to be told: otherwise the next mouse
			// drag would continue from where the camera used to be and undo this in one frame.
			CameraController.SyncFromCamera(Camera);
		};

		// Lets a script wait for a background import rather than sleeping for a guessed duration, which is
		// the difference between a test that is reliable on a slow machine and one that is flaky.
		Context.QuerySceneLoad = [this]
		{
			const FAsyncLoadProgress Progress = GetSceneLoadProgress();
			FJson Result;
			Result["loading"] = Progress.IsBusy();
			Result["elapsedSeconds"] = Progress.ElapsedSeconds;
			Result["file"] = Progress.FileName;
			Result["phase"] = ToString(Progress.Phase);
			// Both counters always, rather than only the running phase's: a script asserting that textures
			// were all processed needs the final counts to still be there once the phase has moved on.
			Result["texturesDone"] = Progress.TexturesDone;
			Result["textureCount"] = Progress.TextureCount;
			Result["meshesDone"] = Progress.MeshesDone;
			Result["meshCount"] = Progress.MeshCount;
			return Result;
		};

		FAutomationServerDesc Desc;
		Desc.Port = static_cast<uint16>(Settings.AutomationPort);
		return Automation.Initialize(Desc, std::move(Context));
	}
#endif

	bool FEngine::BuildConfiguredRenderGraph()
	{
		Renderer.ClearRenderGraph();

		if (Settings.RenderGraphPath.empty())
		{
			// Not an error worth a stack of diagnostics: a project may deliberately ship without one. It
			// still means nothing draws the scene, which the caller reports.
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_RENDERER, "No render graph is configured");
			return false;
		}

		// Reflected from the passes that were just instantiated, so the graph is validated against what this
		// build can actually run rather than against a list maintained by hand.
		const FRenderGraphPassTypeRegistry PassTypes = FRenderPassRegistry::Get().BuildPassTypes();
		if (PassTypes.IsEmpty())
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "No render passes are registered, so no render graph can be built");
			return false;
		}

		// Resolved through the settings rather than here, so the editor panel opens the same file this runs
		// instead of the two disagreeing about where a relative name points.
		std::vector<std::filesystem::path> Searched;
		const std::filesystem::path Path = Settings.ResolveRenderGraphPath(&Searched);
		if (Path.empty())
		{
			std::string Tried;
			for (const std::filesystem::path& Candidate : Searched)
			{
				Tried += Tried.empty() ? "" : ", ";
				Tried += Candidate.string();
			}

			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Render graph '{}' was not found. Tried: {}", Settings.RenderGraphPath, Tried);
			return false;
		}

		FRenderGraphDesc Graph;
		const FRenderGraphLoadResult LoadResult = FRenderGraphJson::LoadFromFile(Path, PassTypes, Graph);

		// Warnings from loading mean elements were dropped, which usually explains a later compile failure,
		// so they are reported even when the load itself succeeded.
		for (const FRenderGraphIssue& Issue : LoadResult.Issues)
		{
			if (Issue.IsError())
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Render graph '{}': {}", Settings.RenderGraphPath, Issue.Message);
			}
			else
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_RENDERER, "Render graph '{}': {}", Settings.RenderGraphPath, Issue.Message);
			}
		}

		if (!LoadResult.bSucceeded)
		{
			return false;
		}

		// Not const: it is handed to the renderer by move, on the failure path as well as the success one.
		FRenderGraphCompileResult Compiled = CompileRenderGraph(Graph, PassTypes);

		// After the compile and before the renderer sees it, which is the only place the injected passes can
		// go: the description is what the panel draws and what saving writes, so a pass added there would
		// have to be filtered out of both.
		InjectRenderGraphPasses(Compiled, BuildRenderGraphInjections(), PassTypes);

		for (const FRenderGraphIssue& Issue : Compiled.Issues)
		{
			if (Issue.IsError())
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Render graph '{}': {}", Settings.RenderGraphPath, Issue.Message);
			}
			else
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_RENDERER, "Render graph '{}': {}", Settings.RenderGraphPath, Issue.Message);
			}
		}

		if (!Compiled.bSucceeded)
		{
			// Handed over even though it cannot run, so the reason survives past this function. The renderer
			// keeps it for anything asking why nothing is being drawn; returning here without it would leave
			// a failed graph indistinguishable from no graph at all.
			Renderer.SetRenderGraph(std::move(Compiled));
			return false;
		}

		// Read before the move, since the result is about to be handed over.
		const SizeType PassCount = Compiled.ExecutionOrder.size();
		const SizeType ResourceCount = Compiled.Resources.size();

		if (!Renderer.SetRenderGraph(std::move(Compiled)))
		{
			return false;
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_RENDERER, "Render graph '{}' compiled: {} pass(es), {} resource(s)", Settings.RenderGraphPath,
		              PassCount, ResourceCount);
		return true;
	}

	std::vector<FRenderGraphInjection> FEngine::BuildRenderGraphInjections() const
	{
		std::vector<FRenderGraphInjection> Injections;

#if LIME_WITH_EDITOR
		if (bEditorEnabled)
		{
			// The editor UI, drawing the whole window: the scene reaches it through the viewport panel's
			// texture rather than through the back buffer, so it clears its target.
			//
			// Bound to output slot 0 so the graph orders it after whatever produced the scene. That
			// dependency is what the injection is for; the textures the UI actually samples are the ones
			// RegisterTexture handed out.
			FRenderGraphInjection EditorUI;
			EditorUI.PassName = "$EditorUI";
			EditorUI.TypeName = "EditorUI";
			EditorUI.InputFromOutputSlot = 0;
			EditorUI.InputFieldName = FEditorUIPass::SceneField;
			EditorUI.ImportedTarget = FRenderer::BackBufferImport;
			// The UI samples the viewport targets, which the renderer fills with a copy from the graph's
			// outputs. That copy is submitted work the graph did not describe, so nvrhi's within-list
			// tracking does not cover it and the boundary has to be explicit.
			EditorUI.bBeginsNewSubmission = true;
			Injections.push_back(std::move(EditorUI));
			return Injections;
		}
#endif

		// Without the editor nothing would reach the screen at all: every graph pass writes its own
		// textures, and the swap chain is not one of them. A blit is what closes that gap, and it is the
		// same mechanism rather than a second code path.
		FRenderGraphInjection Present;
		Present.PassName = "$Present";
		Present.TypeName = "Blit";
		Present.InputFromOutputSlot = 0;
		Present.InputFieldName = FBlitPass::SourceField;
		Present.ImportedTarget = FRenderer::BackBufferImport;
		Injections.push_back(std::move(Present));

		return Injections;
	}

	void FEngine::BeginLoadConfiguredScene()
	{
		// Applied whether or not a scene loads, so a project drawing through its own passes still gets the
		// configured camera.
		Camera.SetFieldOfView(DegreesToRadians(Settings.CameraFieldOfView));

		FFlyCameraSettings CameraSettings = CameraController.GetSettings();
		CameraSettings.MoveSpeed = Settings.CameraMoveSpeed;
		CameraController.SetSettings(CameraSettings);

		Renderer.SetScene(&Scene);
		Renderer.SetCamera(&Camera);

		if (FBlinnPhongForwardLitPass* Pass = Renderer.FindPass<FBlinnPhongForwardLitPass>())
		{
			Pass->GetSettings().LightIntensity = Settings.LightIntensity;
			Pass->GetSettings().AmbientStrength = Settings.AmbientStrength;
		}

		if (Settings.ScenePath.empty())
		{
			// Not a warning: a project without a scene is a normal configuration.
			FinishSceneLoad();
			return;
		}

		const std::filesystem::path Resolved = FPlatformPaths::ResolveAssetPath(Settings.ScenePath);
		if (Resolved.empty())
		{
			// Logged once, here, rather than from the render loop. The engine continues with an empty scene:
			// a mistyped path should not stop the editor from opening.
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_SCENE, "Scene '{}' was not found in the project or Assets directory", Settings.ScenePath);
			FinishSceneLoad();
			return;
		}

		LoadingScenePath = Resolved;

		// Started rather than awaited, which is the whole point: parsing Bistro is around 15 seconds of CPU
		// work and blocking here made the window unresponsive for all of it. PollSceneLoad picks the result
		// up on a later frame.
		if (!SceneLoader.Start(Resolved))
		{
			LoadingScenePath.clear();
			FinishSceneLoad();
			return;
		}

		// The camera is usable while the scene loads, so flying around an empty view does not snap when the
		// model appears; PollSceneLoad frames it again once the bounds are known.
		FinishSceneLoad();

		LIME_LOG_INFO(LIME_LOG_CATEGORY_SCENE, "Loading '{}' in the background", Resolved.filename().string());
	}

	void FEngine::PollSceneLoad()
	{
		if (!SceneLoader.IsFinished())
		{
			return;
		}

		FGltfImportResult Import;
		if (!SceneLoader.TakeResult(Import))
		{
			return;
		}

		const std::filesystem::path Resolved = LoadingScenePath;
		LoadingScenePath.clear();

		if (!Import.bSucceeded)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_SCENE, "Failed to load '{}': {}", Resolved.string(), Import.Message);
			FinishSceneLoad();
			return;
		}

		if (!Import.Message.empty())
		{
			// tinygltf reports recoverable problems through a warning, worth surfacing but not fatal.
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_SCENE, "While loading '{}': {}", Resolved.string(), Import.Message);
		}

		// On this thread because it populates the registry the renderer and the editor panels read. Measured
		// at under a second even for Bistro, so it does not need splitting across frames; the 15 seconds that
		// did need moving was the parsing, and that already happened on the worker.
		BuildScene(Scene, Import.Scene);

		// Framed from the world bounds so a model of any size and position is visible without per model
		// configuration.
		//
		// The direction is where the camera looks, so this places it on the -Z side, above and to one side,
		// looking back towards +Z. There is no reliable way to know which way a glTF model faces: the
		// specification fixes no facing convention and the sample assets disagree, so this is simply a
		// consistent starting view, and the fly camera is the way to look at the other side.
		const FBoundingBox Bounds = Scene.ComputeWorldBounds();
		if (Bounds.bValid)
		{
			Camera.FrameSphere(Bounds.GetCenter(), Bounds.GetLongestEdge() * 0.5f, FVector3{ -0.3f, -0.25f, -1.0f });
		}

		FinishSceneLoad();

		const FSceneStats Stats = Scene.GetStats();
		LIME_LOG_INFO(LIME_LOG_CATEGORY_SCENE, "Loaded '{}': {} entities, {} meshes, {} triangles", Resolved.filename().string(),
		              Stats.EntityCount, Stats.MeshEntityCount, Stats.TriangleCount);
	}

	void FEngine::FinishSceneLoad()
	{
		// After framing, so the first drag continues from where the camera was placed rather than snapping
		// back to the default pose.
		CameraController.SyncFromCamera(Camera);
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
		if (FViewportTarget* ViewportTarget = Renderer.GetViewportTarget(); ViewportTarget != nullptr)
		{
			const uint32 Width = Renderer.IsOffscreenRenderingEnabled() ? ViewportTarget->GetWidth() : DeviceManager->GetBackBufferWidth();
			const uint32 Height = Renderer.IsOffscreenRenderingEnabled() ? ViewportTarget->GetHeight() : DeviceManager->GetBackBufferHeight();
			if (Height > 0)
			{
				Camera.SetAspectRatio(static_cast<float>(Width) / static_cast<float>(Height));
			}
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

		// Before UpdateCamera, because building the scene frames the camera on the new bounds and
		// UpdateCamera propagates the transforms that framing depends on. Doing it after would show one
		// frame with the model present but the camera still pointing at where nothing is.
		PollSceneLoad();

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

			const FAsyncLoadProgress SceneLoad = GetSceneLoadProgress();
			EditorContext.bSceneLoading = SceneLoad.IsBusy();
			EditorContext.SceneLoadSeconds = SceneLoad.ElapsedSeconds;
			EditorContext.SceneLoadFileName = SceneLoad.FileName;
			EditorContext.SceneLoadPhase = ToString(SceneLoad.Phase);
			// Only the counters of the phase that is running, so the panel needs no knowledge of which
			// phases count what.
			switch (SceneLoad.Phase)
			{
				case EGltfImportPhase::Textures:
					EditorContext.SceneLoadDone = SceneLoad.TexturesDone;
					EditorContext.SceneLoadTotal = SceneLoad.TextureCount;
					break;
				case EGltfImportPhase::Meshes:
					EditorContext.SceneLoadDone = SceneLoad.MeshesDone;
					EditorContext.SceneLoadTotal = SceneLoad.MeshCount;
					break;
				default:
					// Parsing and the bookkeeping phases have nothing to count; zero tells the panel to show
					// elapsed time instead of a bar.
					EditorContext.SceneLoadDone = 0;
					EditorContext.SceneLoadTotal = 0;
					break;
			}

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
			// One graph: the project's passes and the engine's injected ones, ordered together. The editor UI
			// is a pass in it rather than a stage after it.
			Renderer.RenderFrame();
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

		// Before the scene and the renderer go away. Closing the window during a long import is the normal
		// way to abandon one, and the worker writes into members of this engine, so it has to be finished
		// with before any of them are destroyed. The wait is bounded by the import that was already running.
		SceneLoader.Cancel();

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
