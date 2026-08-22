// Loads ShaderMake output and turns it into nvrhi shader objects.

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

		// RootDirectory holds the DXIL/ and SPIRV/ subdirectories.
		bool Initialize(nvrhi::IDevice* InDevice, ERHIBackend InBackend, std::filesystem::path RootDirectory);
		void Shutdown();

		// Returns nullptr and logs a diagnostic on failure. Results are cached per key.
		nvrhi::ShaderHandle GetShader(const FShaderKey& Key);
		nvrhi::ShaderHandle GetShader(std::string_view SourcePath, std::string_view EntryPoint, nvrhi::ShaderType Type);

		const std::filesystem::path& GetRootDirectory() const { return RootDirectory; }

	private:
		bool LoadBytecode(const FShaderKey& Key, std::vector<uint8>& OutBytecode) const;

		nvrhi::IDevice* Device = nullptr;
		ERHIBackend Backend = ERHIBackend::D3D12;
		std::filesystem::path RootDirectory;
		std::unordered_map<std::string, nvrhi::ShaderHandle> Cache;
	};
} // namespace Lime
