// glTF import, wrapping tinygltf.
//
// The importer is deliberately strict about what it produces and lenient about what it accepts: the
// Khronos sample assets contain models with missing normals, 8/16/32 bit indices, non triangle
// primitives and images with any channel count. All of that is normalised here so that everything
// downstream sees one uniform representation and needs no special cases.
//
// Failures are reported through a return value rather than exceptions, because a bad scene path must
// degrade to an empty scene instead of preventing the engine from starting.

#pragma once

#include "Asset/AssetTypes.h"

#include <atomic>
#include <filesystem>
#include <string>

namespace Lime
{
	// Which part of an import is running.
	//
	// Named after what the importer actually does in sequence, because the three parts are measured
	// differently: parsing is one opaque call into tinygltf with no way to report a fraction, while
	// textures and meshes are loops this code owns and can count.
	enum class EGltfImportPhase : uint8
	{
		// Nothing started yet.
		Pending,
		// Inside tinygltf. No sub-progress is available: it is a single call that returns when done.
		Parsing,
		// Reading and decoding images. The bulk of the time on a textured scene.
		Textures,
		// Converting mesh attributes and indices.
		Meshes,
		// Hierarchy fixups and the remaining bookkeeping.
		Finalizing,
		Done
	};

	// Counters an import publishes as it runs, for a caller on another thread to read.
	//
	// Atomics rather than a lock because this is written continuously by the importer and read once per
	// frame by whoever is drawing the progress: a mutex here would put the reader in the way of the work
	// it is reporting on. Each field is independent, so a reader that catches a half updated set sees a
	// count that is at worst one item stale, which is not worth synchronising against.
	//
	// Totals are published before the matching loop begins, so a reader never divides by a total that has
	// not been established yet.
	struct FGltfImportProgress
	{
		std::atomic<EGltfImportPhase> Phase{ EGltfImportPhase::Pending };

		std::atomic<uint32> TexturesDone{ 0 };
		std::atomic<uint32> TextureCount{ 0 };

		std::atomic<uint32> MeshesDone{ 0 };
		std::atomic<uint32> MeshCount{ 0 };

		void Reset()
		{
			Phase.store(EGltfImportPhase::Pending);
			TexturesDone.store(0);
			TextureCount.store(0);
			MeshesDone.store(0);
			MeshCount.store(0);
		}
	};

	struct FGltfImportResult
	{
		bool bSucceeded = false;
		// Reason for the failure, or the warning tinygltf produced on success. Worth logging either way.
		std::string Message;
		FGltfSceneData Scene;
	};

	class FGltfImporter
	{
	public:
		// Loads a .gltf or .glb. The container is chosen by extension, falling back to the other one when
		// the first attempt fails, so a mislabelled file still loads.
		//
		// Never throws. A missing file, malformed JSON or an unsupported feature all come back as
		// bSucceeded == false with a message.
		//
		// OutProgress is optional and may be read from another thread while this runs. Passing null skips
		// the reporting entirely, which is what the tests and any caller without a UI do.
		static FGltfImportResult LoadFromFile(const std::filesystem::path& Path, FGltfImportProgress* OutProgress = nullptr);

		// Parses a glTF document held in memory. Exists so the unit tests can exercise the importer with
		// embedded documents and no fixture files on disk.
		//
		// BaseDirectory is used to resolve external buffers and images; tests pass an empty path because
		// their documents embed everything.
		static FGltfImportResult LoadFromString(const std::string& Json, const std::filesystem::path& BaseDirectory);
	};
} // namespace Lime
