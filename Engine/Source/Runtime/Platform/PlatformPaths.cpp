#include "Platform/PlatformPaths.h"

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace Lime
{
	namespace
	{
		std::filesystem::path ResolveExecutableDirectory()
		{
#if defined(_WIN32)
			wchar_t Buffer[MAX_PATH] = {};
			const DWORD Length = GetModuleFileNameW(nullptr, Buffer, MAX_PATH);
			if (Length > 0 && Length < MAX_PATH)
			{
				return std::filesystem::path(Buffer).parent_path();
			}
#endif
			return std::filesystem::current_path();
		}

		// LIME_SOURCE_DIR is injected by CMake so a development build finds assets even when the
		// post-build copy has not run yet.
		std::filesystem::path ResolveEngineDirectory(const char* LeafName)
		{
			const std::filesystem::path Deployed = FPlatformPaths::GetExecutableDirectory() / LeafName;
			if (std::filesystem::exists(Deployed))
			{
				return Deployed;
			}

#if defined(LIME_SOURCE_DIR)
			const std::filesystem::path Source = std::filesystem::path(LIME_SOURCE_DIR) / "Engine" / LeafName;
			if (std::filesystem::exists(Source))
			{
				return Source;
			}
#endif

			return Deployed;
		}
	} // namespace

	const std::filesystem::path& FPlatformPaths::GetExecutableDirectory()
	{
		static const std::filesystem::path Directory = ResolveExecutableDirectory();
		return Directory;
	}

	const std::filesystem::path& FPlatformPaths::GetShaderDirectory()
	{
		static const std::filesystem::path Directory = ResolveEngineDirectory("Shaders");
		return Directory;
	}

	const std::filesystem::path& FPlatformPaths::GetContentDirectory()
	{
		static const std::filesystem::path Directory = ResolveEngineDirectory("Content");
		return Directory;
	}

	const std::filesystem::path& FPlatformPaths::GetSavedDirectory()
	{
		static const std::filesystem::path Directory = []
		{
			const std::filesystem::path Result = GetExecutableDirectory() / "Saved";
			std::error_code ErrorCode;
			std::filesystem::create_directories(Result, ErrorCode);
			return Result;
		}();
		return Directory;
	}

	const std::filesystem::path& FPlatformPaths::GetProjectSourceDirectory()
	{
		// LIME_PROJECT_SOURCE_DIR is injected per project by lime_add_project.
		static const std::filesystem::path Directory = []
		{
#if defined(LIME_PROJECT_SOURCE_DIR)
			return std::filesystem::path(LIME_PROJECT_SOURCE_DIR);
#else
			return std::filesystem::path();
#endif
		}();
		return Directory;
	}

	std::string FPlatformPaths::ToUtf8(const std::filesystem::path& Path)
	{
		const std::u8string Utf8 = Path.u8string();
		return std::string(Utf8.begin(), Utf8.end());
	}
} // namespace Lime
