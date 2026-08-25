// Reading and writing a frame graph as JSON.
//
// The file holds structure only: passes, edges, graph outputs. No positions, no zoom, nothing about how
// it was last displayed. Layout is recomputed on load, so storing it would create a second source of
// truth that drifts the moment either side changes.
//
// Loading is lenient by element and strict about the whole. A malformed edge is dropped with a warning
// and the rest of the graph still opens, because these files are meant to be hand written and generated
// by scripts, and losing the entire graph over one typo would make that unusable. A file that is not
// JSON at all fails outright, since there is nothing to salvage.

#pragma once

#include "FrameGraph/FrameGraphDesc.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Lime
{
	struct FFrameGraphLoadResult
	{
		bool bSucceeded = false;
		// Everything noticed while reading: dropped elements as warnings, a failed load as an error. The
		// panel lists these, so they are phrased for someone looking at the file rather than the code.
		std::vector<FFrameGraphIssue> Issues;

		bool HasIssues() const { return !Issues.empty(); }
		SizeType CountErrors() const;
	};

	class FFrameGraphJson
	{
	public:
		static FFrameGraphLoadResult LoadFromFile(const std::filesystem::path& Path, const FFramePassTypeRegistry& Types,
		                                          FFrameGraphDesc& OutGraph);

		// Exists so the tests can exercise malformed documents without writing fixture files, matching how
		// the glTF importer is tested.
		static FFrameGraphLoadResult LoadFromString(const std::string& Json, const FFramePassTypeRegistry& Types,
		                                            FFrameGraphDesc& OutGraph);

		// Writes through FJsonUtils, which replaces the file atomically: a crash mid-write cannot leave a
		// truncated graph where a working one used to be.
		static bool SaveToFile(const std::filesystem::path& Path, const FFrameGraphDesc& Graph);

		// The document as text, for tests and for showing the user what would be written.
		static std::string SaveToString(const FFrameGraphDesc& Graph);
	};
} // namespace Lime
