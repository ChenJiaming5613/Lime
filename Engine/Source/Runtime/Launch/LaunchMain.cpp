// Engine owned entry point. Projects never define main.
//
// Compiled as an OBJECT library so main and every static registrar end up in the executable
// unconditionally; a static library would let the linker discard object files whose only content is
// a self registration.

#include "Core/Logging/LogManager.h"
#include "Engine/Engine.h"
#include "Engine/ProjectSettings.h"
#include "Platform/PlatformPaths.h"

#include <string_view>

namespace
{
	// --project is handled before the settings file is read, unlike the rest of the command line.
	std::filesystem::path ResolveSettingsPath(int ArgumentCount, char** Arguments)
	{
		constexpr std::string_view Option = "--project=";
		for (int Index = 1; Index < ArgumentCount; ++Index)
		{
			const std::string_view Argument(Arguments[Index]);
			if (Argument.starts_with(Option))
			{
				return std::filesystem::path(Argument.substr(Option.size()));
			}
		}
		return Lime::FProjectSettings::ResolveSettingsPath();
	}
} // namespace

int main(int ArgumentCount, char** Arguments)
{
	// Logging comes first so settings parsing diagnostics are captured.
	Lime::FLogConfig LogConfig;
	LogConfig.FileName = Lime::FPlatformPaths::GetSavedDirectory() / "Logs" / "LimeEngine.log";
	Lime::FLogManager::Get().Initialize(LogConfig);

	// Precedence: code defaults, then ProjectSettings.json, then the command line.
	Lime::FProjectSettings Settings;
	Settings.LoadFromFile(ResolveSettingsPath(ArgumentCount, Arguments));
	Settings.ApplyCommandLine(ArgumentCount, Arguments);
	Settings.LogSummary();

	Lime::FEngine Engine;
	const Lime::int32 ExitCode = Engine.Run(Settings);

	Lime::FLogManager::Get().Shutdown();
	return ExitCode;
}
