// Loads ShaderMake output and turns it into nvrhi shader objects.
//
// Multiple search roots are supported so a project can ship its own shaders, and override a built-in
// one by using the same relative path. Roots are searched most recently added first.

#pragma once

#include "RHI/ShaderKey.h"

#include <nvrhi/nvrhi.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Lime
{
	class FShaderLibrary
	{
	public:
		FShaderLibrary() = default;

		LIME_NON_COPYABLE(FShaderLibrary);
		LIME_NON_MOVABLE(FShaderLibrary);

		// EngineRootDirectory holds the DXIL/ and SPIRV/ subdirectories of the built-in shaders.
		bool Initialize(nvrhi::IDevice* InDevice, ERHIBackend InBackend, std::filesystem::path EngineRootDirectory);
		void Shutdown();

		// Adds a root that takes precedence over the ones already registered. Ignored when the
		// directory does not contain output for the active backend.
		bool AddSearchRoot(std::filesystem::path Root);

		// Returns nullptr and logs a diagnostic on failure. Results are cached per key.
		nvrhi::ShaderHandle GetShader(const FShaderKey& Key);
		nvrhi::ShaderHandle GetShader(std::string_view SourcePath, std::string_view EntryPoint, nvrhi::ShaderType Type);

		const std::vector<std::filesystem::path>& GetSearchRoots() const { return SearchRoots; }

	private:
		// Resolves against each root in precedence order; empty when the file exists nowhere.
		std::filesystem::path ResolveShaderPath(const FShaderKey& Key) const;
		bool LoadBytecode(const FShaderKey& Key, std::vector<uint8>& OutBytecode) const;

		nvrhi::IDevice* Device = nullptr;
		ERHIBackend Backend = ERHIBackend::D3D12;
		// Ordered by precedence: the highest priority root is first.
		std::vector<std::filesystem::path> SearchRoots;
		std::unordered_map<std::string, nvrhi::ShaderHandle> Cache;
	};
} // namespace Lime
