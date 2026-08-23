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

#include <filesystem>
#include <string>

namespace Lime
{
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
		static FGltfImportResult LoadFromFile(const std::filesystem::path& Path);

		// Parses a glTF document held in memory. Exists so the unit tests can exercise the importer with
		// embedded documents and no fixture files on disk.
		//
		// BaseDirectory is used to resolve external buffers and images; tests pass an empty path because
		// their documents embed everything.
		static FGltfImportResult LoadFromString(const std::string& Json, const std::filesystem::path& BaseDirectory);
	};
} // namespace Lime
