// DDS loading.
//
// Only the header is parsed: the block compressed payload is handed to the GPU exactly as stored. Every
// backend this engine targets supports BC1 through BC7 natively, so decompressing on the CPU would throw
// away the memory and bandwidth saving the format exists for. A 4096x4096 BC7 texture is 22 MB on disk
// and stays 22 MB in video memory; decoded to RGBA it would be 89 MB for the top level alone.
//
// Scope is deliberately narrow: the DX10 extension header only. The legacy DDS_PIXELFORMAT bit mask path
// describes D3D9 era formats, and every modern tool (texconv, NVIDIA Texture Tools, Compressonator)
// writes the DX10 header instead. A legacy file is rejected with an explanation rather than silently
// mishandled, since guessing wrong there produces a texture that looks plausible but has its channels
// swapped.
//
// Approach follows Donut's DDSFile.cpp, which the RTXDI samples use through the same NVRHI backend, and
// which is in turn based on DirectXTK's DDSTextureLoader. Two deviations, both to avoid defects observed
// in that implementation:
//
//   - format mapping is an explicit switch rather than an array indexed by the enum value, which in Donut
//     relies on the table staying in exact sync with nvrhi::Format and breaks silently when a format is
//     inserted upstream;
//   - anything that is not a plain 2D texture is refused outright, rather than accepted and then uploaded
//     with a row pitch that ignores the depth slices.

#pragma once

#include "Asset/AssetTypes.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Lime
{
	struct FDdsLoadResult
	{
		bool bSucceeded = false;
		// Why it failed, phrased so the reader knows what to do about it. Empty on success.
		std::string Message;
		FImageData Image;
	};

	class FDdsLoader
	{
	public:
		// True when the path ends in .dds, case insensitively. Used to pick this loader over stb.
		static bool HasDdsExtension(const std::filesystem::path& Path);

		// True when the bytes start with the DDS magic. Cheaper than a full parse and enough to route a
		// buffer whose origin is unknown.
		static bool LooksLikeDds(const uint8* Bytes, size_t Size);

		// Reads a DDS from disk. Never throws; a missing file or an unsupported layout comes back as
		// bSucceeded == false.
		static FDdsLoadResult LoadFromFile(const std::filesystem::path& Path);

		// Parses a DDS already in memory. The bytes are copied into the result, so the caller's buffer does
		// not need to outlive it.
		//
		// DebugName appears in messages; pass the file name or uri so a failure can be traced back to a
		// specific texture in a scene with hundreds of them.
		static FDdsLoadResult LoadFromMemory(const uint8* Bytes, size_t Size, const std::string& DebugName);
	};
} // namespace Lime
