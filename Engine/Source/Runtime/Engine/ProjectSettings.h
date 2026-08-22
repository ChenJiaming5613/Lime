// Project configuration, loaded from ProjectSettings.json.
//
// Precedence: code defaults < ProjectSettings.json < command line. The command line always wins so
// that switching backends for a quick test never requires editing the file.

#pragma once

#include "RHI/RHITypes.h"

#include <filesystem>
#include <string>

namespace Lime
{
	// Debug runtimes cost performance, so the default follows the build configuration.
	enum class EValidationMode : uint8
	{
		Off = 0,
		DebugOnly,
		On
	};

	struct FProjectSettings
	{
		std::string ProjectName = "LimeProject";

		std::string WindowTitle = "LimeEngine";
		uint32 WindowWidth = 1600;
		uint32 WindowHeight = 900;

		ERHIBackend Backend = GetDefaultBackend();
		uint32 BackBufferCount = 3;
		bool bVSync = true;
		EValidationMode Validation = EValidationMode::DebugOnly;

		bool bEnableEditor = LIME_WITH_EDITOR != 0;
		// Writes pass settings to Saved/ on exit and restores them on start. Off by default, because
		// stale saved values silently override changes made to the defaults in code.
		bool bPersistPassSettings = false;

		// <exe>/ProjectSettings.json, falling back to the source tree during development.
		static std::filesystem::path ResolveSettingsPath();

		// Applies the file on top of the current values. Missing or malformed entries are reported
		// and left at their defaults, so a broken file degrades instead of preventing startup.
		bool LoadFromFile(const std::filesystem::path& Path);

		// Supported switches: --rhi=<d3d12|vulkan>, --width=N, --height=N, --no-editor, --no-vsync,
		// --validation=<off|debugOnly|on>, --project=<path to json>.
		void ApplyCommandLine(int ArgumentCount, const char* const* Arguments);

		bool IsValidationEnabled() const;
		void LogSummary() const;
	};
} // namespace Lime
