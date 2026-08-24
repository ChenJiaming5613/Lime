// Async glTF loading tests.
//
// Threading defects are the kind that survive review and then fail once on someone else's machine, so
// the properties asserted here are the ones that make the loader safe to use rather than merely working:
// that a result is delivered exactly once, that the object can be destroyed while a load is in flight,
// and that progress is readable at any moment without waiting for the worker.
//
// Documents are built in memory so the suite needs no fixture files and no graphics device, matching the
// rest of the asset tests.

#include "Asset/AsyncGltfLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace Lime;

namespace
{
	// A minimal valid glTF: one triangle, everything embedded, so it parses quickly and needs no
	// sibling files.
	const char* const MinimalGltf = R"({
	  "asset": {"version": "2.0"},
	  "scene": 0,
	  "scenes": [{"nodes": [0]}],
	  "nodes": [{"mesh": 0}],
	  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
	  "accessors": [
	    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
	     "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
	    {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
	  ],
	  "bufferViews": [
	    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
	    {"buffer": 0, "byteOffset": 36, "byteLength": 6}
	  ],
	  "buffers": [{"byteLength": 42, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA"}]
	})";

	// Writes the document to a real file, because the loader's entry point takes a path: the point of the
	// test is the threading around the import, not a way to bypass it.
	class FTemporaryGltf
	{
	public:
		FTemporaryGltf()
		{
			Path = std::filesystem::temp_directory_path() / "LimeAsyncGltfLoaderTest.gltf";
			std::ofstream Out(Path, std::ios::binary | std::ios::trunc);
			Out << MinimalGltf;
		}

		~FTemporaryGltf()
		{
			std::error_code Error;
			std::filesystem::remove(Path, Error);
		}

		const std::filesystem::path& Get() const { return Path; }

	private:
		std::filesystem::path Path;
	};

	// Spins until the loader reports it is done. Bounded so a defect fails the test instead of hanging
	// the whole suite.
	bool WaitForFinish(const FAsyncGltfLoader& Loader, std::chrono::seconds Timeout = std::chrono::seconds(30))
	{
		const auto Deadline = std::chrono::steady_clock::now() + Timeout;
		while (std::chrono::steady_clock::now() < Deadline)
		{
			if (Loader.IsFinished())
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return false;
	}
} // namespace

TEST_CASE("An async load delivers its result", "[Asset][AsyncGltf]")
{
	const FTemporaryGltf Document;
	FAsyncGltfLoader Loader;

	SECTION("A fresh loader is idle and has nothing to hand over")
	{
		REQUIRE(Loader.GetProgress().Stage == EAsyncLoadStage::Idle);
		REQUIRE_FALSE(Loader.IsFinished());

		// The out parameter must be left alone when there is nothing to collect, so a caller that polls
		// cannot be handed a cleared result by mistake.
		FGltfImportResult Result;
		Result.Message = "untouched";
		REQUIRE_FALSE(Loader.TakeResult(Result));
		REQUIRE(Result.Message == "untouched");
	}

	SECTION("A started load finishes and produces the scene")
	{
		REQUIRE(Loader.Start(Document.Get()));
		REQUIRE(WaitForFinish(Loader));

		FGltfImportResult Result;
		REQUIRE(Loader.TakeResult(Result));
		INFO("importer message: " << Result.Message);
		REQUIRE(Result.bSucceeded);
		REQUIRE(Result.Scene.Meshes.size() == 1);
		REQUIRE(Result.Scene.GetTotalTriangleCount() == 1);
	}

	SECTION("The result is handed over exactly once")
	{
		REQUIRE(Loader.Start(Document.Get()));
		REQUIRE(WaitForFinish(Loader));

		FGltfImportResult First;
		REQUIRE(Loader.TakeResult(First));
		REQUIRE(First.Scene.Meshes.size() == 1);

		// A second collection would mean the caller could integrate the same scene twice, which in the
		// engine shows as duplicated entities.
		FGltfImportResult Second;
		REQUIRE_FALSE(Loader.TakeResult(Second));
		REQUIRE(Second.Scene.Meshes.empty());
	}

	SECTION("Collecting the result returns the loader to idle so it can be reused")
	{
		REQUIRE(Loader.Start(Document.Get()));
		REQUIRE(WaitForFinish(Loader));

		FGltfImportResult Result;
		REQUIRE(Loader.TakeResult(Result));
		REQUIRE(Loader.GetProgress().Stage == EAsyncLoadStage::Idle);

		// Reuse matters because the engine has one loader for the session: a second scene has to be able
		// to use it without leaking the first worker.
		REQUIRE(Loader.Start(Document.Get()));
		REQUIRE(WaitForFinish(Loader));
		FGltfImportResult Again;
		REQUIRE(Loader.TakeResult(Again));
		REQUIRE(Again.bSucceeded);
	}
}

TEST_CASE("A failed async load reports rather than throws", "[Asset][AsyncGltf]")
{
	FAsyncGltfLoader Loader;

	// A path that cannot exist. The engine must keep running with an empty scene, so this has to come
	// back as a state rather than an exception on a thread with no handler.
	REQUIRE(Loader.Start(std::filesystem::temp_directory_path() / "LimeNoSuchScene.gltf"));
	REQUIRE(WaitForFinish(Loader));
	REQUIRE(Loader.GetProgress().Stage == EAsyncLoadStage::Failed);

	FGltfImportResult Result;
	REQUIRE(Loader.TakeResult(Result));
	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE_FALSE(Result.Message.empty());
}

TEST_CASE("Async load progress is readable while the worker runs", "[Asset][AsyncGltf]")
{
	const FTemporaryGltf Document;
	FAsyncGltfLoader Loader;

	REQUIRE(Loader.Start(Document.Get()));

	// Polling must never block, or the caller reading progress every frame would stall on the very work
	// it is reporting on. Asserted as a bound rather than assumed: the document is tiny, so a poll that
	// waited for the import would still return quickly and hide the defect. A generous limit keeps this
	// from failing on a loaded machine while still catching a poll that joins the worker.
	const auto Start = std::chrono::steady_clock::now();
	for (int Poll = 0; Poll < 50; ++Poll)
	{
		const FAsyncLoadProgress Progress = Loader.GetProgress();
		REQUIRE(Progress.FileName == Document.Get().filename().string());
		REQUIRE(Progress.ElapsedSeconds >= 0.0f);
	}
	const auto Elapsed = std::chrono::steady_clock::now() - Start;
	REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(Elapsed).count() < 2000);

	REQUIRE(WaitForFinish(Loader));
	FGltfImportResult Result;
	REQUIRE(Loader.TakeResult(Result));
	REQUIRE(Result.bSucceeded);
}

TEST_CASE("Import phases are reported with their counts", "[Asset][AsyncGltf]")
{
	const FTemporaryGltf Document;
	FAsyncGltfLoader Loader;

	SECTION("A finished load ends on the done phase with every item accounted for")
	{
		REQUIRE(Loader.Start(Document.Get()));
		REQUIRE(WaitForFinish(Loader));

		const FAsyncLoadProgress Progress = Loader.GetProgress();
		REQUIRE(Progress.Phase == EGltfImportPhase::Done);
		// The counters are what a bar divides by, so a finished phase must not leave them short of the
		// total: that would show as a bar stuck at 90% on a load that is over.
		REQUIRE(Progress.MeshCount == 1);
		REQUIRE(Progress.MeshesDone == Progress.MeshCount);
	}

	SECTION("Collecting the result clears the phase")
	{
		REQUIRE(Loader.Start(Document.Get()));
		REQUIRE(WaitForFinish(Loader));

		FGltfImportResult Result;
		REQUIRE(Loader.TakeResult(Result));

		// An idle loader reporting the phase of the load that just ended would draw a stale overlay.
		const FAsyncLoadProgress Progress = Loader.GetProgress();
		REQUIRE(Progress.Phase == EGltfImportPhase::Pending);
		REQUIRE(Progress.MeshCount == 0);
		REQUIRE(Progress.TextureCount == 0);
	}

	SECTION("A phase that cannot be counted reports no fraction")
	{
		// Parsing is one call into tinygltf, so there is nothing to divide. Returning -1 rather than 0 is
		// what lets the caller tell "no progress available" apart from "no progress yet", which decide
		// between showing elapsed time and showing an empty bar.
		FAsyncLoadProgress Parsing;
		Parsing.Phase = EGltfImportPhase::Parsing;
		REQUIRE(Parsing.GetPhaseFraction() < 0.0f);

		FAsyncLoadProgress Textures;
		Textures.Phase = EGltfImportPhase::Textures;
		Textures.TextureCount = 4;
		Textures.TexturesDone = 1;
		REQUIRE(Textures.GetPhaseFraction() == 0.25f);

		// A scene with no textures at all must not divide by zero.
		FAsyncLoadProgress Empty;
		Empty.Phase = EGltfImportPhase::Textures;
		REQUIRE(Empty.GetPhaseFraction() < 0.0f);
	}

	SECTION("Every phase has a display name")
	{
		// A missing name shows up as "Unknown" in the viewport, which is worse than useless: it says the
		// engine does not know what it is doing.
		const EGltfImportPhase Phases[] = { EGltfImportPhase::Pending, EGltfImportPhase::Parsing,    EGltfImportPhase::Textures,
		                                    EGltfImportPhase::Meshes,  EGltfImportPhase::Finalizing, EGltfImportPhase::Done };
		for (const EGltfImportPhase Phase : Phases)
		{
			const char* const Name = ToString(Phase);
			REQUIRE(Name != nullptr);
			REQUIRE(std::string(Name) != "Unknown");
		}
	}
}

TEST_CASE("An async load can be abandoned safely", "[Asset][AsyncGltf]")
{
	const FTemporaryGltf Document;

	SECTION("Cancel leaves the loader idle")
	{
		FAsyncGltfLoader Loader;
		REQUIRE(Loader.Start(Document.Get()));
		Loader.Cancel();

		REQUIRE(Loader.GetProgress().Stage == EAsyncLoadStage::Idle);
		REQUIRE_FALSE(Loader.IsFinished());

		// The result was discarded with the cancellation, so there must be nothing left to collect.
		FGltfImportResult Result;
		REQUIRE_FALSE(Loader.TakeResult(Result));
	}

	SECTION("Destroying a loader mid load does not leave the worker running")
	{
		// The case that would otherwise corrupt memory: the worker writes into members of the loader, so
		// the destructor has to wait for it. Closing the window during a long import does exactly this.
		{
			FAsyncGltfLoader Loader;
			REQUIRE(Loader.Start(Document.Get()));
		}

		// Reaching here without a crash or a terminate is the assertion; a joinable thread left behind
		// would have aborted the process on destruction.
		SUCCEED("the loader joined its worker on destruction");
	}
}
