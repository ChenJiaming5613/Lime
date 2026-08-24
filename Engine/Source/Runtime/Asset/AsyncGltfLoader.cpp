#include "Asset/AsyncGltfLoader.h"

#include "Core/Logging/LogManager.h"

#include <chrono>
#include <utility>

namespace Lime
{
	namespace
	{
		int64 NowNanoseconds()
		{
			return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}
	} // namespace

	FAsyncGltfLoader::~FAsyncGltfLoader()
	{
		// A running worker writes into members of this object, so letting the destructor finish while it
		// is still going would corrupt memory that no longer belongs to anyone.
		Join();
	}

	bool FAsyncGltfLoader::Start(const std::filesystem::path& Path)
	{
		if (Stage.load() == EAsyncLoadStage::Importing)
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_SCENE, "A glTF load is already running; ignoring the request for '{}'", Path.string());
			return false;
		}

		// Reaps the thread of a previous load whose result was never collected. Without this, starting a
		// second load would assign over a joinable std::thread, which terminates the process.
		Join();

		{
			const std::lock_guard<std::mutex> Lock(ResultMutex);
			Result = {};
			FileName = Path.filename().string();
		}

		StartNanoseconds.store(NowNanoseconds());
		FinishNanoseconds.store(0);
		bFinished.store(false);
		Stage.store(EAsyncLoadStage::Importing);

		// The path is captured by value: the caller's object may well be a temporary, and the worker
		// outlives this function by design.
		Worker = std::thread(
		    [this, Path]()
		    {
			    FGltfImportResult Imported = FGltfImporter::LoadFromFile(Path);

			    {
				    const std::lock_guard<std::mutex> Lock(ResultMutex);
				    Result = std::move(Imported);
			    }

			    FinishNanoseconds.store(NowNanoseconds());
			    // Stage before bFinished, so a reader that sees the finished flag never observes a stale stage.
			    Stage.store(Result.bSucceeded ? EAsyncLoadStage::Ready : EAsyncLoadStage::Failed);
			    bFinished.store(true);
		    });

		return true;
	}

	FAsyncLoadProgress FAsyncGltfLoader::GetProgress() const
	{
		FAsyncLoadProgress Progress;
		Progress.Stage = Stage.load();

		const int64 Started = StartNanoseconds.load();
		if (Started != 0)
		{
			// Frozen at the finish time once the import is done, so a completed load does not keep
			// counting up while its result waits to be collected.
			const int64 Finished = FinishNanoseconds.load();
			const int64 End = Finished != 0 ? Finished : NowNanoseconds();
			Progress.ElapsedSeconds = static_cast<float>(static_cast<double>(End - Started) / 1'000'000'000.0);
		}

		{
			const std::lock_guard<std::mutex> Lock(ResultMutex);
			Progress.FileName = FileName;
		}

		return Progress;
	}

	bool FAsyncGltfLoader::IsFinished() const
	{
		return bFinished.load();
	}

	bool FAsyncGltfLoader::TakeResult(FGltfImportResult& OutResult)
	{
		if (!bFinished.load())
		{
			return false;
		}

		// Before reading Result: joining is what establishes that the worker's writes are visible here.
		Join();

		{
			const std::lock_guard<std::mutex> Lock(ResultMutex);
			OutResult = std::move(Result);
			Result = {};
		}

		bFinished.store(false);
		Stage.store(EAsyncLoadStage::Idle);
		StartNanoseconds.store(0);
		FinishNanoseconds.store(0);
		return true;
	}

	void FAsyncGltfLoader::Cancel()
	{
		// The import itself is not interruptible: tinygltf has no way to be asked to stop partway. So this
		// waits for it rather than pretending to abort, and discards the result. Called during shutdown,
		// where a few seconds of waiting is preferable to a worker writing into freed memory.
		Join();

		{
			const std::lock_guard<std::mutex> Lock(ResultMutex);
			Result = {};
			FileName.clear();
		}

		bFinished.store(false);
		Stage.store(EAsyncLoadStage::Idle);
		StartNanoseconds.store(0);
		FinishNanoseconds.store(0);
	}

	void FAsyncGltfLoader::Join()
	{
		if (Worker.joinable())
		{
			Worker.join();
		}
	}
} // namespace Lime
