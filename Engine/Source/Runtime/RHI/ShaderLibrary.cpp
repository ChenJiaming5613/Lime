#include "RHI/ShaderLibrary.h"

#include "Core/Logging/LogManager.h"
#include "Platform/PlatformPaths.h"

#include <ShaderMake/ShaderBlob.h>
#include <fstream>

namespace Lime
{
	bool FShaderLibrary::Initialize(nvrhi::IDevice* InDevice, ERHIBackend InBackend, std::filesystem::path EngineRootDirectory)
	{
		if (InDevice == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "FShaderLibrary requires a valid device");
			return false;
		}

		Device = InDevice;
		Backend = InBackend;
		SearchRoots.clear();
		Cache.clear();

		if (!AddSearchRoot(std::move(EngineRootDirectory)))
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "No engine shaders for {}. Build the LimeShaders target first.", ToString(Backend));
			return false;
		}

		return true;
	}

	bool FShaderLibrary::AddSearchRoot(std::filesystem::path Root)
	{
		// A root that does not exist at all is not a problem: a project with no shaders of its own is a
		// valid project, and the engine's own root is still registered. Warning here would mean every
		// configuration-only project logs a diagnostic about something it never asked for.
		if (!std::filesystem::exists(Root))
		{
			return false;
		}

		const std::filesystem::path PlatformDirectory = Root / GetShaderPlatformDirectory(Backend);
		if (!std::filesystem::exists(PlatformDirectory))
		{
			// The directory exists but holds nothing for this backend, which does deserve a warning: shaders
			// were built for one backend and the engine is running on the other.
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_RHI, "Skipping shader root without {} output: {}", GetShaderPlatformDirectory(Backend),
			                 FPlatformPaths::ToUtf8(Root));
			return false;
		}

		// Inserted at the front so later roots win, letting a project override a built-in shader.
		SearchRoots.insert(SearchRoots.begin(), std::move(Root));
		LIME_LOG_INFO(LIME_LOG_CATEGORY_RHI, "Shader root added: {} ({})", FPlatformPaths::ToUtf8(PlatformDirectory), ToString(Backend));
		return true;
	}

	void FShaderLibrary::Shutdown()
	{
		Cache.clear();
		SearchRoots.clear();
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

	std::filesystem::path FShaderLibrary::ResolveShaderPath(const FShaderKey& Key) const
	{
		const std::filesystem::path RelativePath = MakeShaderRelativePath(Key, Backend);
		for (const std::filesystem::path& Root : SearchRoots)
		{
			const std::filesystem::path Candidate = Root / RelativePath;
			if (std::filesystem::exists(Candidate))
			{
				return Candidate;
			}
		}
		return {};
	}

	bool FShaderLibrary::LoadBytecode(const FShaderKey& Key, std::vector<uint8>& OutBytecode) const
	{
		const std::filesystem::path FullPath = ResolveShaderPath(Key);
		if (FullPath.empty())
		{
			// Listing the roots that were searched makes a missing build step obvious.
			std::string SearchedRoots;
			for (const std::filesystem::path& Root : SearchRoots)
			{
				if (!SearchedRoots.empty())
				{
					SearchedRoots += ", ";
				}
				SearchedRoots += FPlatformPaths::ToUtf8(Root);
			}
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Shader '{}' not found for backend {} under: {}",
			               FPlatformPaths::ToUtf8(MakeShaderRelativePath(Key, Backend)), ToString(Backend), SearchedRoots);
			return false;
		}

		std::ifstream Stream(FullPath, std::ios::binary | std::ios::ate);
		if (!Stream)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RHI, "Cannot open shader file: {}", FPlatformPaths::ToUtf8(FullPath));
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
