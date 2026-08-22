// Maps a shader request onto the file names ShaderMake produces.
//
// ShaderMake composes an output name as:
//   <relative path without extension>[_<Entry> when Entry != "main"][<OutputSuffix>]
// A shader declaring defines additionally gets an 8 digit hash suffix per permutation and all of
// its permutations are packed into a blob stored under the unhashed name. A shader with no defines
// is written as raw bytecode under that same unhashed name. This header mirrors only the unhashed
// part; permutation lookup inside a blob is done through ShaderMake's blob reader at runtime.

#pragma once

#include "Core/CoreTypes.h"
#include "RHI/RHITypes.h"

#include <string>
#include <utility>
#include <vector>

namespace Lime
{
	using FShaderDefine = std::pair<std::string, std::string>;

	struct FShaderKey
	{
		// Path relative to the shader config, e.g. "Triangle/Triangle.hlsl".
		std::string SourcePath;
		std::string EntryPoint = "main";
		nvrhi::ShaderType Type = nvrhi::ShaderType::None;
		std::vector<FShaderDefine> Defines;

		bool operator==(const FShaderKey& Other) const;
	};

	// Returns "Triangle/Triangle_MainVS" for the key above, without extension.
	std::string MakeShaderMakeOutputName(std::string_view SourcePath, std::string_view EntryPoint);
	// "DXIL" or "SPIRV"; the subdirectory the build writes to.
	const char* GetShaderPlatformDirectory(ERHIBackend Backend);
	// ".dxil" or ".spirv".
	const char* GetShaderFileExtension(ERHIBackend Backend);

	// Relative path of the file to load, e.g. "DXIL/Triangle/Triangle_MainVS.dxil".
	std::string MakeShaderRelativePath(const FShaderKey& Key, ERHIBackend Backend);

	// Stable identity for the shader cache, including defines.
	std::string MakeShaderCacheKey(const FShaderKey& Key);
} // namespace Lime
