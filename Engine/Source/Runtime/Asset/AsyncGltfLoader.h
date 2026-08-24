// Background glTF import with progress the main thread can read.
//
// Importing Bistro takes about 15 seconds of pure CPU work in a debug build, and doing it inline during
// startup leaves the window unresponsive for that whole time with nothing on screen. There is no way to
// tell that apart from a hang, which is exactly how it was reported.
//
// Splitting the work is possible because the two halves have different constraints, and only one of them
// is expensive. FGltfImporter produces plain data and touches no engine state, so it can run anywhere:
// measured at 15.4 seconds. Turning that data into entities touches the scene's entt registry and has to
// stay on the thread that reads it: measured at 0.9 seconds. Moving the first half off the main thread
// therefore removes 94% of the stall, and the part that remains is short enough to run in one frame
// rather than needing the scene to be mutable from two threads at once.
//
// Deliberately not a general task system. One load at a time is what the engine actually does, and the
// narrow shape is what makes the synchronisation reviewable: a single worker, a mutex around the result,
// and an atomic for progress. A job graph would be more capable and much harder to be sure about.
//
// The design point is that this class never touches FScene, the renderer or the camera. It hands back a
// finished FGltfImportResult and the caller integrates it on its own thread, which keeps the threading
// story confined to this one file.

#pragma once

#include "Asset/GltfImporter.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace Lime
{
	// Coarse stage of a load, for a caller that wants to say what is happening rather than draw a bar.
	enum class EAsyncLoadStage : uint8
	{
		Idle,
		// Reading and parsing the file. This is where nearly all of the time goes.
		Importing,
		// Import finished and the result is waiting to be collected by the main thread.
		Ready,
		Failed
	};

	// Display name of an import phase. Defined next to the enum so a new phase cannot be added without a
	// name, which is how "Unknown" ends up on screen.
	const char* ToString(EGltfImportPhase Phase);

	struct FAsyncLoadProgress
	{
		EAsyncLoadStage Stage = EAsyncLoadStage::Idle;
		// Seconds since the load was started, so a caller can show elapsed time without keeping its own
		// clock. Stops advancing once the import finishes.
		float ElapsedSeconds = 0.0f;
		// What is being loaded, for a message naming the file.
		std::string FileName;

		// Which part of the import is running, and how far through it is.
		//
		// Reported per phase rather than as one overall fraction because the phases are not comparable:
		// parsing cannot report a fraction at all, and the share of the total each one takes varies with
		// the asset. Rolling them into a single percentage would mean inventing weights that are wrong for
		// every scene except the one they were measured on.
		EGltfImportPhase Phase = EGltfImportPhase::Pending;
		uint32 TexturesDone = 0;
		uint32 TextureCount = 0;
		uint32 MeshesDone = 0;
		uint32 MeshCount = 0;

		bool IsBusy() const { return Stage == EAsyncLoadStage::Importing; }

		// Fraction of the current phase, or -1 when the phase cannot report one. Parsing is the case that
		// cannot: it is a single call into tinygltf that returns only when it is finished.
		float GetPhaseFraction() const
		{
			switch (Phase)
			{
				case EGltfImportPhase::Textures:
					return TextureCount > 0 ? static_cast<float>(TexturesDone) / static_cast<float>(TextureCount) : -1.0f;
				case EGltfImportPhase::Meshes:
					return MeshCount > 0 ? static_cast<float>(MeshesDone) / static_cast<float>(MeshCount) : -1.0f;
				default:
					return -1.0f;
			}
		}
	};

	// Runs FGltfImporter on a worker thread. Not copyable or movable: a worker holds a pointer to this
	// object, so moving it would leave that pointer dangling.
	class FAsyncGltfLoader
	{
	public:
		FAsyncGltfLoader() = default;
		~FAsyncGltfLoader();

		LIME_NON_COPYABLE(FAsyncGltfLoader);
		LIME_NON_MOVABLE(FAsyncGltfLoader);

		// Starts importing Path on a worker thread. Returns false when a load is already running, since
		// allowing a second one would leave the first result with no owner.
		bool Start(const std::filesystem::path& Path);

		// Safe to call from any thread and cheap enough for every frame: reads an atomic stage plus a
		// short string under a lock, with no allocation on the busy path.
		FAsyncLoadProgress GetProgress() const;

		// True once the worker has finished, whether it succeeded or not. The result is waiting for
		// TakeResult.
		bool IsFinished() const;

		// Hands over the finished import, leaving this loader idle so it can be reused.
		//
		// Blocks until the worker has actually exited, which is what makes the returned data safe to
		// touch: joining is the only thing that guarantees the worker is done writing it. In practice the
		// caller only reaches here after IsFinished, so there is nothing left to wait for.
		//
		// OutResult is left untouched when no load has finished, so a caller that polls need not check
		// twice.
		bool TakeResult(FGltfImportResult& OutResult);

		// Abandons an in flight load. Blocks until the worker exits, because the alternative is a thread
		// still writing into members that are about to be destroyed.
		void Cancel();

	private:
		void Join();

		std::thread Worker;

		// Guards Result and FileName, both written by the worker and read by the caller. Stage is atomic
		// rather than guarded so a progress poll does not contend with the worker.
		mutable std::mutex ResultMutex;
		FGltfImportResult Result;
		std::string FileName;

		std::atomic<EAsyncLoadStage> Stage{ EAsyncLoadStage::Idle };
		// Counters the importer writes while it runs. Held here rather than passed in, so a caller polling
		// progress needs nothing but this loader.
		FGltfImportProgress ImportProgress;
		// Set by the worker before it stops, read by the main thread to decide when to collect. Separate
		// from Stage because Stage becomes Ready or Failed and both mean "collect me".
		std::atomic<bool> bFinished{ false };

		// Start time as a plain count of nanoseconds so it can be atomic; a steady_clock::time_point is
		// not guaranteed to be lock free.
		std::atomic<int64> StartNanoseconds{ 0 };
		std::atomic<int64> FinishNanoseconds{ 0 };
	};
} // namespace Lime
