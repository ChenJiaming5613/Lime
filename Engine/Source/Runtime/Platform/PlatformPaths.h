// Resolves engine and project directories. Everything is relative to the executable, with a
// fallback to the source tree so shaders and content can be edited without reinstalling.

#pragma once

#include "Core/CoreTypes.h"

#include <filesystem>

namespace Lime
{
	class FPlatformPaths
	{
	public:
		static const std::filesystem::path& GetExecutableDirectory();

		// <exe>/Shaders, falling back to Engine/Shaders in the source tree.
		static const std::filesystem::path& GetShaderDirectory();
		// <exe>/Content, falling back to Engine/Content in the source tree.
		static const std::filesystem::path& GetContentDirectory();
		// Writable location for logs, layout and saved settings.
		static const std::filesystem::path& GetSavedDirectory();

		// Source directory of the project being run, injected by lime_add_project. Empty in
		// installed builds, where everything is expected to sit next to the executable.
		static const std::filesystem::path& GetProjectSourceDirectory();

		static std::string ToUtf8(const std::filesystem::path& Path);
	};
} // namespace Lime
