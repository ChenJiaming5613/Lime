// Owns the subsystem lifetime and the main loop.

#pragma once

#include "Engine/ApplicationInterface.h"
#include "Engine/ProjectSettings.h"
#include "Platform/PlatformTime.h"
#include "Platform/Window.h"
#include "RHI/DeviceManager.h"
#include "Renderer/Renderer.h"

#include "Asset/AsyncGltfLoader.h"
#include "Camera/FlyCameraController.h"
#include "Camera/PerspectiveCamera.h"
#include "Scene/Scene.h"

#include <filesystem>
#include <memory>

#if LIME_WITH_EDITOR
#include "Editor/EditorLayer.h"
#endif

#if LIME_WITH_AUTOMATION
#include "Automation/AutomationServer.h"
#include "Automation/ScreenshotService.h"
#endif

namespace Lime
{
	class FEngine
	{
	public:
		FEngine() = default;
		~FEngine();

		LIME_NON_COPYABLE(FEngine);
		LIME_NON_MOVABLE(FEngine);

		// Initializes everything, runs the loop and shuts down. Returns a process exit code.
		int32 Run(const FProjectSettings& InSettings);
		void RequestExit() { bExitRequested = true; }

		FWindow& GetWindow() { return Window; }
		FRenderer& GetRenderer() { return Renderer; }
		IDeviceManager& GetDeviceManager() { return *DeviceManager; }
		const FProjectSettings& GetSettings() const { return Settings; }
		const FTimer& GetTimer() const { return Timer; }

		FScene& GetScene() { return Scene; }
		FPerspectiveCamera& GetCamera() { return Camera; }
		FFlyCameraController& GetCameraController() { return CameraController; }

		// State of the scene import running in the background, for a caller that wants to say the scene is
		// still loading rather than draw an empty view with no explanation. Cheap enough to poll per frame.
		FAsyncLoadProgress GetSceneLoadProgress() const { return SceneLoader.GetProgress(); }

#if LIME_WITH_EDITOR
		// Null when the editor is disabled through settings or --no-editor.
		FEditorLayer* GetEditor() { return bEditorEnabled ? &Editor : nullptr; }
#endif

	private:
		bool Initialize();
		void Shutdown();
		void Tick();

		// Loads and compiles the configured render graph, then hands it to the renderer.
		//
		// Returns false when the project has no usable graph, which the caller treats as a warning rather
		// than a failed startup: the engine then draws only the editor UI, and every reason is in the log.
		// That is far more useful than refusing to open a window over a mistyped resource name.
		bool BuildConfiguredRenderGraph();

		// Applies the configured camera and lighting, then starts the glTF import on a worker thread.
		// Never fails the startup: a bad path is reported once here and the engine continues with an empty
		// scene, which is far more useful than refusing to open a window.
		//
		// Returns without waiting, so the window is interactive while a large scene loads. The import is
		// collected later by PollSceneLoad.
		void BeginLoadConfiguredScene();
		// Turns a finished import into entities, then frames the camera on it. Call once per frame.
		//
		// Separate from the import because this part touches the scene's registry and the camera, which
		// belong to this thread; only the parsing was moved off it.
		void PollSceneLoad();
		// Shared by the startup path and the failure paths, so the camera controller ends up consistent
		// with the camera however the load turned out.
		void FinishSceneLoad();
		// Advances the fly camera. Only allows flight to start when the cursor is over the viewport, so a
		// right click on an editor panel does not take over the view.
		void UpdateCamera(float DeltaSeconds);

#if LIME_WITH_AUTOMATION
		// Builds the command context, including the delegates that reach back into the engine.
		bool StartAutomation();
#endif

		// The configuration this session is running with. Consumed during Initialize and never read
		// again, so it stays immutable for the lifetime of the process.
		FProjectSettings Settings;
#if LIME_WITH_AUTOMATION
		// Draft edited by settings.set and written by settings.save. Separate from Settings because no
		// setting can be applied to a running session: mixing the two would make settings.get report
		// values the engine is not actually using.
		FProjectSettings PendingSettings;
#endif
		std::unique_ptr<ILimeApplication> Application;
		FWindow Window;
		std::unique_ptr<IDeviceManager> DeviceManager;
		FRenderer Renderer;
		FTimer Timer;
		// The scene outlives the renderer's use of it, which is what lets the renderer hold a bare pointer.
		FScene Scene;
		// Imports off the main thread. Idle once the configured scene has been collected.
		FAsyncGltfLoader SceneLoader;
		// Kept for the log line and the progress message, since the loader only knows the file name.
		std::filesystem::path LoadingScenePath;
		FPerspectiveCamera Camera;
		FFlyCameraController CameraController;
#if LIME_WITH_EDITOR
		FEditorLayer Editor;
#endif
#if LIME_WITH_AUTOMATION
		FScreenshotService Screenshots;
		FAutomationServer Automation;
#endif
		bool bEditorEnabled = false;
		bool bExitRequested = false;
		// Set by the resize callback and consumed at the start of the next frame.
		bool bResizePending = false;
		uint32 PendingWidth = 0;
		uint32 PendingHeight = 0;
	};
} // namespace Lime
