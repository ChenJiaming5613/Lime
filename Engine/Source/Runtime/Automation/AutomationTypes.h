// Types shared by the automation server and its command handlers.
//
// Commands are executed on the main thread between frames, so a handler may touch engine state
// directly without synchronization. The network threads only parse requests and write replies.

#pragma once

#include "Core/Json/JsonUtils.h"
#include "Core/Math/Vector.h"

#include <functional>
#include <string>

namespace Lime
{
	class FEditorLayer;
	class FLogRingBuffer;
	class FRenderer;
	class IDeviceManager;
	class FScreenshotService;

	// Everything a command is allowed to reach. Built once by FEngine; the numeric fields are
	// refreshed every frame. Delegates cover state that lives in layers above this module, which
	// keeps LimeAutomation below LimeRuntime in the dependency graph.
	struct FAutomationContext
	{
		FRenderer* Renderer = nullptr;
		IDeviceManager* DeviceManager = nullptr;
		FLogRingBuffer* LogBuffer = nullptr;
		FScreenshotService* Screenshots = nullptr;
		// Null when the editor is disabled or not compiled in.
		FEditorLayer* Editor = nullptr;

		std::string ProjectName;
		float DeltaSeconds = 0.0f;
		float FramesPerSecond = 0.0f;
		double TotalSeconds = 0.0;
		uint64 FrameCount = 0;

		// Asks the engine to leave the main loop.
		std::function<void()> RequestExit;
		// The configuration this session is running with, matching the ProjectSettings.json schema.
		// Immutable: settings are consumed at startup and cannot be changed afterwards.
		std::function<FJson()> QuerySettings;
		// Edits the draft that will be written to disk. No setting takes effect before a restart, so
		// this deliberately does not touch the running configuration.
		std::function<bool(const FJson&, std::string&)> ApplySettings;
		// The draft as it currently stands, which differs from QuerySettings once something is edited.
		std::function<FJson()> QueryPendingSettings;
		// Writes the draft to the authored ProjectSettings.json.
		std::function<bool()> SaveSettings;

		// Places the camera. A delegate rather than a camera pointer because positioning is specific to a
		// concrete camera type, and routing it through the engine keeps this module free of that dependency.
		// Angles are in degrees, matching what scene.camera.get reports.
		std::function<void(const FVector3& Position, float YawDegrees, float PitchDegrees)> SetCameraPose;

		// Reports a scene import still running on a worker thread, so a script can wait for a large scene
		// instead of guessing how long it takes. Empty JSON when nothing is loading.
		//
		// A delegate for the same reason as the others: the loader lives in the asset module and this one
		// stays free of it.
		std::function<FJson()> QuerySceneLoad;
	};

	// A single command in flight. The handler fills Result or Error; returning without touching
	// either yields an empty success.
	class FAutomationInvocation
	{
	public:
		// Runs at the end of the frame, once rendering for it has been submitted.
		using FCompletion = std::function<void(FAutomationContext&, FJson& Result, std::string& Error)>;

		FAutomationInvocation(const FJson& InParams, FAutomationContext& InContext)
		    : Params(InParams),
		      Context(InContext)
		{
		}

		const FJson& GetParams() const { return Params; }
		FAutomationContext& GetContext() const { return Context; }

		FJson& GetResult() { return Result; }
		const FJson& GetResult() const { return Result; }

		void Fail(std::string Message) { Error = std::move(Message); }
		bool HasFailed() const { return !Error.empty(); }
		const std::string& GetError() const { return Error; }

		// Defers the reply until the frame has been rendered, which is what a screenshot needs.
		void Defer(FCompletion InCompletion)
		{
			Completion = std::move(InCompletion);
			bDeferred = true;
		}
		bool IsDeferred() const { return bDeferred; }
		FCompletion TakeCompletion() { return std::move(Completion); }

		// Typed parameter access. Each reports a readable error through OutError when the key is
		// present but of the wrong type; a missing key falls back to the default.
		bool TryGetString(const char* Key, std::string& OutValue, std::string& OutError) const;
		bool TryGetBool(const char* Key, bool& OutValue, std::string& OutError) const;
		bool TryGetUInt(const char* Key, uint32& OutValue, std::string& OutError) const;

		// Same, but missing keys are an error. Returns false and calls Fail on any problem.
		bool RequireString(const char* Key, std::string& OutValue);

	private:
		const FJson& Params;
		FAutomationContext& Context;
		FJson Result = FJson::object();
		std::string Error;
		FCompletion Completion;
		bool bDeferred = false;
	};

	using FAutomationHandler = std::function<void(FAutomationInvocation&)>;
} // namespace Lime
