// Owns the subsystem lifetime and the main loop.

#pragma once

#include "Engine/ApplicationInterface.h"
#include "Engine/ProjectSettings.h"
#include "Platform/PlatformTime.h"
#include "Platform/Window.h"
#include "RHI/DeviceManager.h"
#include "Renderer/Renderer.h"

#include <memory>

#if LIME_WITH_EDITOR
#include "Editor/EditorLayer.h"
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

#if LIME_WITH_EDITOR
		// Null when the editor is disabled through settings or --no-editor.
		FEditorLayer* GetEditor() { return bEditorEnabled ? &Editor : nullptr; }
#endif

	private:
		bool Initialize();
		void Shutdown();
		void Tick();

		FProjectSettings Settings;
		std::unique_ptr<ILimeApplication> Application;
		FWindow Window;
		std::unique_ptr<IDeviceManager> DeviceManager;
		FRenderer Renderer;
		FTimer Timer;
#if LIME_WITH_EDITOR
		FEditorLayer Editor;
#endif
		bool bEditorEnabled = false;
		bool bExitRequested = false;
		// Set by the resize callback and consumed at the start of the next frame.
		bool bResizePending = false;
		uint32 PendingWidth = 0;
		uint32 PendingHeight = 0;
	};
} // namespace Lime
