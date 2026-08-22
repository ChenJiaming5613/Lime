#include "RHI/ShaderLibrary.h"

#include "Core/Logging/LogManager.h"
#include "Platform/PlatformPaths.h"

#include <ShaderMake/ShaderBlob.h>
#include <fstream>

namespace Lime
{
	bool FShaderLibrary::Initialize(nvrhi::IDevice* InDevice, ERHIBackend InBackend, std::filesystem::path InRootDirectory)
	{
		if (InDevice == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "FShaderLibrary requires a valid device");
			return false;
		}

		Device = InDevice;
		Backend = InBackend;
		RootDirectory = std::move(InRootDirectory);
		Cache.clear();

		const std::filesystem::path PlatformDirectory = RootDirectory / GetShaderPlatformDirectory(Backend);
		if (!std::filesystem::exists(PlatformDirectory))
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Shader directory not found: {}. Build the LimeShaders target first.",
			               FPlatformPaths::ToUtf8(PlatformDirectory));
			return false;
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_RHI, "Shader library ready: {} ({})", FPlatformPaths::ToUtf8(PlatformDirectory), ToString(Backend));
		return true;
	}

	void FShaderLibrary::Shutdown()
	{
		Cache.clear();
		Device = nullptr;
	}

	nvrhi::ShaderHandle FShaderLibrary::GetShader(std::string_view SourcePath, std::string_view EntryPoint, nvrhi::ShaderType Type)
	{
		FShaderKey Key;
		Key.SourcePath.assign(SourcePath);
		Key.EntryPoint.assign(EntryPoint);
		Key.Type = Type;
		return GetShader(Key);
	}

	nvrhi::ShaderHandle FShaderLibrary::GetShader(const FShaderKey& Key)
	{
		if (Device == nullptr)
		{
			return nullptr;
		}

		const std::string CacheKey = MakeShaderCacheKey(Key);
		if (const auto Existing = Cache.find(CacheKey); Existing != Cache.end())
		{
			return Existing->second;
		}

		std::vector<uint8> FileData;
		if (!LoadBytecode(Key, FileData))
		{
			return nullptr;
		}

		const void* Bytecode = FileData.data();
		SizeType BytecodeSize = FileData.size();

		// ShaderMake only produces a blob when the shader declares defines; otherwise the file is
		// raw bytecode and must be passed through untouched.
		if (!Key.Defines.empty())
		{
			std::vector<ShaderMake::ShaderConstant> Constants;
			Constants.reserve(Key.Defines.size());
			for (const FShaderDefine& Define : Key.Defines)
			{
				Constants.push_back(ShaderMake::ShaderConstant{ Define.first.c_str(), Define.second.c_str() });
			}

			const void* PermutationData = nullptr;
			size_t PermutationSize = 0;
			if (!ShaderMake::FindPermutationInBlob(FileData.data(), FileData.size(), Constants.data(),
			                                       static_cast<uint32_t>(Constants.size()), &PermutationData, &PermutationSize))
			{
				const std::string Diagnostic = ShaderMake::FormatShaderNotFoundMessage(FileData.data(), FileData.size(), Constants.data(),
				                                                                       static_cast<uint32_t>(Constants.size()));
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Shader permutation not found for {}: {}", Key.SourcePath, Diagnostic);
				return nullptr;
			}

			Bytecode = PermutationData;
			BytecodeSize = PermutationSize;
		}

		const nvrhi::ShaderDesc Desc = nvrhi::ShaderDesc().setShaderType(Key.Type).setEntryName(Key.EntryPoint.c_str());
		nvrhi::ShaderHandle Shader = Device->createShader(Desc, Bytecode, BytecodeSize);
		if (Shader == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "createShader failed for {} entry {}", Key.SourcePath, Key.EntryPoint);
			return nullptr;
		}

		Cache.emplace(CacheKey, Shader);
		LIME_LOG_TRACE(LIME_LOG_CATEGORY_RHI, "Loaded shader {} entry {} ({} bytes)", Key.SourcePath, Key.EntryPoint, BytecodeSize);
		return Shader;
	}

	bool FShaderLibrary::LoadBytecode(const FShaderKey& Key, std::vector<uint8>& OutBytecode) const
	{
		const std::filesystem::path FullPath = RootDirectory / MakeShaderRelativePath(Key, Backend);

		std::ifstream Stream(FullPath, std::ios::binary | std::ios::ate);
		if (!Stream)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Shader file not found: {} (backend {})", FPlatformPaths::ToUtf8(FullPath),
			               ToString(Backend));
			return false;
		}

		const std::streamsize Size = Stream.tellg();
		if (Size <= 0)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Shader file is empty: {}", FPlatformPaths::ToUtf8(FullPath));
			return false;
		}

		OutBytecode.resize(static_cast<SizeType>(Size));
		Stream.seekg(0, std::ios::beg);
		if (!Stream.read(reinterpret_cast<char*>(OutBytecode.data()), Size))
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Failed to read shader file: {}", FPlatformPaths::ToUtf8(FullPath));
			return false;
		}

		return true;
	}
} // namespace Lime
