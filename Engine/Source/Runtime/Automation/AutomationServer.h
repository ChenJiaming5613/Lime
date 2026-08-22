// Local HTTP server exposing engine state to external automation scripts.
//
// Protocol: JSON over HTTP on the loopback interface.
//   POST /command            {"command": "engine.info", "params": {}}   -> {"ok": true, "result": {...}}
//   POST /command/<name>     {...params...}                             -> same, name from the path
//   POST /batch              [{"command": ...}, ...]                    -> array of replies
//   GET  /                                                             -> server identity and metadata
//   GET  /commands                                                     -> the command catalogue
//   GET  /screenshot?source=backBuffer                                 -> image/png bytes
//
// Built on cpp-httplib, so this file contains no platform specific socket code and the module
// compiles anywhere the rest of the engine does.
//
// Threading: httplib serves each request from its own thread pool. Commands are queued and run by
// Tick() on the main thread between frames, so handlers see a consistent engine state and need no
// locking of their own. The serving thread blocks on a future until its command has been executed.
//
// The listener binds to the loopback interface only. This is a debug facility with full access to
// engine state, so it must never be reachable from outside the machine.

#pragma once

#include "Automation/AutomationTypes.h"

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Forward declared so <httplib.h> stays out of this header; it is a heavy include that also pulls in
// <windows.h>. The lower case name is the library's own and cannot follow the engine convention.
namespace httplib // NOLINT(readability-identifier-naming)
{
	class Server;
} // namespace httplib

namespace Lime
{
	struct FAutomationServerDesc
	{
		uint16 Port = 8787;
		// Written next to the executable so a script can discover the port without being told.
		bool bWriteEndpointFile = true;
		// How long a request waits for the main thread before giving up. A hung main loop must not
		// leave the client blocked forever.
		uint32 CommandTimeoutSeconds = 30;
	};

	class FAutomationServer
	{
	public:
		FAutomationServer();
		~FAutomationServer();

		LIME_NON_COPYABLE(FAutomationServer);
		LIME_NON_MOVABLE(FAutomationServer);

		// Starts listening. Returns false when the bind failed, which is not fatal for the engine: it
		// simply runs without automation.
		bool Initialize(const FAutomationServerDesc& Desc, FAutomationContext Context);
		void Shutdown();

		bool IsRunning() const { return bRunning; }
		uint16 GetPort() const { return BoundPort; }

		// Refreshes the per frame fields of the context. Call once per frame before Tick.
		void UpdateContext(float DeltaSeconds, float FramesPerSecond, double TotalSeconds, uint64 FrameCount);

		// Executes the commands queued since the last call. Must run on the main thread.
		void Tick();
		// Runs the completions of commands deferred during Tick. Call after the frame was submitted,
		// which is what lets a screenshot capture the frame it asked for.
		void FlushDeferred();

	private:
		struct FPendingCommand
		{
			std::string Name;
			FJson Params;
			// Fulfilled by the main thread; the serving thread waits on the matching future.
			std::promise<FJson> Reply;
		};

		struct FDeferredCommand
		{
			FAutomationInvocation::FCompletion Completion;
			FJson Result;
			std::promise<FJson> Reply;
		};

		void InstallRoutes();
		// Runs one command through the queue and returns the reply object.
		FJson Execute(std::string Name, FJson Params);
		// Parses a request body into a command name and params. Returns false and fills OutError when
		// the body is not a usable request.
		static bool ParseCommandBody(const std::string& Body, std::string& OutName, FJson& OutParams, std::string& OutError);

		static FJson MakeError(std::string Message);
		static FJson MakeSuccess(FJson Result);

		void WriteEndpointFile() const;
		void RemoveEndpointFile() const;

		FAutomationServerDesc Desc;
		FAutomationContext Context;

		// Held by pointer so <httplib.h> stays out of this header; it is a heavy include that also
		// pulls in <windows.h>.
		std::unique_ptr<httplib::Server> Server;
		std::thread ListenerThread;

		std::mutex QueueMutex;
		std::vector<std::unique_ptr<FPendingCommand>> Queue;
		// Deferred commands wait here between Tick and FlushDeferred, always on the main thread.
		std::vector<FDeferredCommand> Deferred;

		std::atomic<bool> bRunning{ false };
		std::atomic<bool> bStopping{ false };
		uint16 BoundPort = 0;
	};
} // namespace Lime
