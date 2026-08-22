// Local TCP server exposing engine state to external automation scripts.
//
// Protocol: newline delimited JSON over TCP, one request object per line, one reply object per line.
//   -> {"id": 1, "command": "engine.info", "params": {}}
//   <- {"id": 1, "ok": true, "result": {...}}
//   <- {"id": 1, "ok": false, "error": "..."}
//
// Threading: each connection gets a thread that only parses and writes. Commands are queued and run
// by Tick() on the main thread between frames, so handlers see a consistent engine state and need no
// locking of their own. The calling thread blocks on a future until its command has been executed.
//
// The listener binds to the loopback interface only. This is a debug facility with full access to
// engine state, so it must never be reachable from outside the machine.

#pragma once

#include "Automation/AutomationTypes.h"

#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Lime
{
	struct FAutomationServerDesc
	{
		uint16 Port = 8787;
		// Written next to the executable so a script can discover the port without being told.
		bool bWritePortFile = true;
	};

	class FAutomationServer
	{
	public:
		FAutomationServer() = default;
		~FAutomationServer();

		LIME_NON_COPYABLE(FAutomationServer);
		LIME_NON_MOVABLE(FAutomationServer);

		// Starts listening. Returns false when the socket layer or the bind failed, which is not
		// fatal for the engine: it simply runs without automation.
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
			// Fulfilled by the main thread; the connection thread waits on the matching future.
			std::promise<FJson> Reply;
		};

		struct FDeferredCommand
		{
			FAutomationInvocation::FCompletion Completion;
			FJson Result;
			std::promise<FJson> Reply;
		};

		void ListenerLoop();
		void ConnectionLoop(uintptr_t ClientSocket);
		// Parses one request line and returns the reply object.
		FJson HandleRequestLine(const std::string& Line);
		// Queues a command and waits for the main thread to run it.
		FJson DispatchCommand(std::string Name, FJson Params, const FJson& Id);

		static FJson MakeError(const FJson& Id, std::string Message);
		static FJson MakeSuccess(const FJson& Id, FJson Result);

		void WritePortFile() const;
		void RemovePortFile() const;

		FAutomationServerDesc Desc;
		FAutomationContext Context;

		// Stored as an integer so <winsock2.h> stays out of this header.
		std::atomic<uintptr_t> ListenSocket{ ~uintptr_t{ 0 } };
		std::thread ListenerThread;
		std::vector<std::thread> ConnectionThreads;
		std::mutex ConnectionMutex;

		std::mutex QueueMutex;
		std::vector<std::unique_ptr<FPendingCommand>> Queue;
		// Deferred commands wait here between Tick and FlushDeferred, always on the main thread.
		std::vector<FDeferredCommand> Deferred;

		std::atomic<bool> bRunning{ false };
		std::atomic<bool> bStopping{ false };
		uint16 BoundPort = 0;
	};
} // namespace Lime
