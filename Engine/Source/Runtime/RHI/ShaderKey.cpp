#include "RHI/ShaderKey.h"

namespace Lime
{
	namespace
	{
		// ShaderMake strips the source extension; it does not care which one was used.
		std::string_view StripExtension(std::string_view Path)
		{
			const SizeType DotIndex = Path.rfind('.');
			const SizeType SlashIndex = Path.find_last_of("/\\");
			if (DotIndex == std::string_view::npos || (SlashIndex != std::string_view::npos && DotIndex < SlashIndex))
			{
				return Path;
			}
			return Path.substr(0, DotIndex);
		}
	} // namespace

	bool FShaderKey::operator==(const FShaderKey& Other) const
	{
		return SourcePath == Other.SourcePath && EntryPoint == Other.EntryPoint && Type == Other.Type && Defines == Other.Defines;
	}

	std::string MakeShaderMakeOutputName(std::string_view SourcePath, std::string_view EntryPoint)
	{
		std::string Result(StripExtension(SourcePath));

		// Normalize separators so the result matches the layout written on disk.
		for (char& Character : Result)
		{
			if (Character == '\\')
			{
				Character = '/';
			}
		}

		if (!EntryPoint.empty() && EntryPoint != "main")
		{
			Result += '_';
			Result.append(EntryPoint);
		}

		return Result;
	}

	const char* GetShaderPlatformDirectory(ERHIBackend Backend)
	{
		switch (Backend)
		{
			case ERHIBackend::D3D12:
				return "DXIL";
			case ERHIBackend::Vulkan:
				return "SPIRV";
			default:
				return "";
		}
	}

	const char* GetShaderFileExtension(ERHIBackend Backend)
	{
		switch (Backend)
		{
			case ERHIBackend::D3D12:
				return ".dxil";
			case ERHIBackend::Vulkan:
				return ".spirv";
			default:
				return "";
		}
	}

	std::string MakeShaderRelativePath(const FShaderKey& Key, ERHIBackend Backend)
	{
		std::string Result = GetShaderPlatformDirectory(Backend);
		Result += '/';
		Result += MakeShaderMakeOutputName(Key.SourcePath, Key.EntryPoint);
		Result += GetShaderFileExtension(Backend);
		return Result;
	}

	std::string MakeShaderCacheKey(const FShaderKey& Key)
	{
		std::string Result = MakeShaderMakeOutputName(Key.SourcePath, Key.EntryPoint);
		Result += '|';
		Result += std::to_string(static_cast<uint32>(Key.Type));

		for (const FShaderDefine& Define : Key.Defines)
		{
			Result += '|';
			Result += Define.first;
			Result += '=';
			Result += Define.second;
		}

		return Result;
	}
} // namespace Lime
