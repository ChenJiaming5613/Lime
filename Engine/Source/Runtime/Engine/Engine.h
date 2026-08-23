// Owns the subsystem lifetime and the main loop.

#pragma once

#include "Camera/FlyCameraController.h"
#include "Camera/PerspectiveCamera.h"
#include "Engine/ApplicationInterface.h"
#include "Engine/ProjectSettings.h"
#include "Platform/PlatformTime.h"
#include "Platform/Window.h"
#include "RHI/DeviceManager.h"
#include "Renderer/Renderer.h"
#include "Scene/Scene.h"

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

#if LIME_WITH_EDITOR
		// Null when the editor is disabled through settings or --no-editor.
		FEditorLayer* GetEditor() { return bEditorEnabled ? &Editor : nullptr; }
#endif

	private:
		bool Initialize();
		void Shutdown();
		void Tick();

		// Loads the configured glTF, or leaves the scene empty when none is set. Never fails the startup:
		// a bad path is reported once here and the engine continues with an empty scene, which is far more
		// useful than refusing to open a window.
		void LoadConfiguredScene();
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
