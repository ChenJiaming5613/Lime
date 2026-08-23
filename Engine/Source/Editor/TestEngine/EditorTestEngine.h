// Owns the Dear ImGui test engine, which drives editor widgets by injecting real input events.
//
// This lives in the editor rather than the runtime because the test engine binds to an ImGui
// context, and FEditorLayer is the only thing that creates one.
//
// Why input injection matters: a test clicks the Window menu the way a user would, instead of
// flipping a panel's visibility flag. That covers the interaction path itself, so a menu item wired
// to the wrong panel fails the test rather than passing silently.
//
// Threading: the test function runs on a coroutine, which the engine drives from inside
// ImGui::NewFrame(). The coroutine and the main thread hand control back and forth under a mutex and
// never run in parallel, so a test may touch engine state without synchronization.
//
// The header deliberately exposes no test engine types, so translation units that only need to
// query state do not inherit its headers.

#pragma once

#include "Core/CoreTypes.h"

#include <string>
#include <vector>

struct ImGuiContext;
struct ImGuiTestEngine;

namespace Lime
{
	// Mirrors ImGuiTestStatus, so a client does not have to depend on the test engine's numbering.
	enum class EUITestStatus : uint8
	{
		Unknown = 0,
		Success,
		Queued,
		Running,
		Error,
		Suspended
	};

	const char* ToString(EUITestStatus Status);

	struct FUITestResult
	{
		std::string Category;
		std::string Name;
		EUITestStatus Status = EUITestStatus::Unknown;
		// Wall clock duration of the last run, or zero when it has not run yet.
		double DurationSeconds = 0.0;
	};

	// Snapshot of a run, which maps directly onto the uitest.status reply.
	struct FUITestRunStatus
	{
		// Incremented every time tests are queued. Lets a polling client tell "the previous run has
		// finished" apart from "the run I asked for has not started yet", which is otherwise a race:
		// the queue is briefly empty between the request and the first frame that picks it up.
		uint32 Run = 0;
		bool bRunning = false;
		int32 Tested = 0;
		int32 Succeeded = 0;
		int32 Remaining = 0;
	};

	class FEditorTestEngine
	{
	public:
		FEditorTestEngine() = default;
		~FEditorTestEngine();

		LIME_NON_COPYABLE(FEditorTestEngine);
		LIME_NON_MOVABLE(FEditorTestEngine);

		// Binds to the ImGui context and registers everything in FUITestRegistry. Call after the
		// context and the platform backend exist.
		bool Initialize(ImGuiContext* Context);

		// Stops the coroutine and joins it. Must run before ImGui::DestroyContext(), because a
		// suspended test would otherwise resume against a destroyed context. Destroying the engine
		// itself has to happen after, which is why it is a separate step.
		void Shutdown();

		// Releases the engine. Only valid once ImGui::DestroyContext() has run.
		void DestroyAfterImGuiContext();

		bool IsInitialized() const { return Engine != nullptr; }

		// Bracket the swap chain present. Only timing is affected here, since capture is compiled out.
		void PreSwap();
		void PostSwap();

		// True while the engine wants frames as fast as possible, which is the cue to skip waiting
		// for vsync. Honoured only during a run so an idle editor does not spin the GPU.
		bool IsRequestingMaxAppSpeed() const;

		// Draws the test engine's own windows, for picking and running tests by hand.
		void DrawUI(bool* bOpen);

		// Queues tests whose "Category/Name" contains Filter, or all of them when it is empty.
		// Returns false and explains why when nothing matched, so a typo in a filter is not
		// mistaken for a run that passed with zero tests.
		bool QueueTests(const std::string& Filter, std::string& OutError);

		// Stops the current run. Queued tests are dropped.
		void Abort();

		FUITestRunStatus GetRunStatus() const;
		std::vector<FUITestResult> GetResults() const;

	private:
		void RegisterTests();

		ImGuiTestEngine* Engine = nullptr;
		uint32 RunCounter = 0;
		// Set while a queued run has not finished. Needed because the engine's queue is empty both
		// before a run starts and after it ends. Mutable because GetRunStatus() is the only place
		// that observes the run ending, and it has no reason to be non const otherwise.
		mutable bool bRunPending = false;
	};
} // namespace Lime
