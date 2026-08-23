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

		// <exe>/Shaders, falling back to Engine/Shaders in the source tree. This is the root that
		// holds one subdirectory per shader owner, not the built-in shaders themselves.
		static const std::filesystem::path& GetShaderDirectory();
		// <exe>/Shaders/Engine, the built-in shaders. A sibling of the per project directories rather
		// than their parent, so both are resolved the same way.
		static const std::filesystem::path& GetEngineShaderDirectory();
		// <exe>/Content, falling back to Engine/Content in the source tree.
		static const std::filesystem::path& GetContentDirectory();
		// Writable location for logs, layout and saved settings.
		static const std::filesystem::path& GetSavedDirectory();

		// Source directory of the project being run. Injected at startup rather than compiled in,
		// because the define only exists on the executable target and this code lives in a library.
		// Empty in installed builds, where everything sits next to the executable.
		static const std::filesystem::path& GetProjectSourceDirectory();
		static void SetProjectSourceDirectory(std::filesystem::path Directory);

		static std::string ToUtf8(const std::filesystem::path& Path);

		// Identifies this process. Lets an external tool confirm the engine it discovered is still
		// alive rather than trusting a stale file left behind by a crash.
		static uint32 GetProcessId();
	};
} // namespace Lime
