#include "Engine/ProjectSettings.h"

#include "Core/Json/JsonUtils.h"
#include "Core/Logging/LogManager.h"
#include "Platform/PlatformPaths.h"

#include <spdlog/fmt/fmt.h>

#include <charconv>
#include <string_view>

namespace Lime
{
	namespace
	{
		constexpr const char* SettingsFileName = "ProjectSettings.json";
		constexpr const char* LogContext = "ProjectSettings.json";
		// Matches FViewportTarget::MaxSize; a window larger than that could not be rendered into.
		constexpr uint32 MaxWindowDimension = 16384;

		bool TryParseUInt(std::string_view Text, uint32& OutValue)
		{
			uint32 Parsed = 0;
			const auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Parsed);
			if (Result.ec != std::errc{} || Parsed == 0)
			{
				return false;
			}
			OutValue = Parsed;
			return true;
		}

		// Separate from TryParseUInt, which rejects zero: port 0 is a valid request for an OS assigned
		// port rather than a missing value.
		bool TryParsePort(std::string_view Text, uint32& OutValue)
		{
			uint32 Parsed = 0;
			const auto Result = std::from_chars(Text.data(), Text.data() + Text.size(), Parsed);
			if (Result.ec != std::errc{} || Parsed > 65535)
			{
				return false;
			}
			OutValue = Parsed;
			return true;
		}

		bool MatchOption(std::string_view Argument, std::string_view Name, std::string_view& OutValue)
		{
			if (!Argument.starts_with(Name) || Argument.size() <= Name.size() || Argument[Name.size()] != '=')
			{
				return false;
			}
			OutValue = Argument.substr(Name.size() + 1);
			return true;
		}
	} // namespace

	const char* ToString(EValidationMode Mode)
	{
		switch (Mode)
		{
			case EValidationMode::Off:
				return "off";
			case EValidationMode::On:
				return "on";
			case EValidationMode::DebugOnly:
				return "debugOnly";
		}
		return "debugOnly";
	}

	bool TryParseValidation(std::string_view Text, EValidationMode& OutMode)
	{
		if (Text == "off")
		{
			OutMode = EValidationMode::Off;
			return true;
		}
		if (Text == "debugOnly" || Text == "debugonly")
		{
			OutMode = EValidationMode::DebugOnly;
			return true;
		}
		if (Text == "on")
		{
			OutMode = EValidationMode::On;
			return true;
		}
		return false;
	}

	std::filesystem::path FProjectSettings::ResolveAuthoringPath()
	{
		// Only the source tree copy is worth writing: the one beside the executable is overwritten by
		// the next build's post build step.
		const std::filesystem::path& SourceRoot = FPlatformPaths::GetProjectSourceDirectory();
		if (SourceRoot.empty())
		{
			return {};
		}
		return SourceRoot / SettingsFileName;
	}

	bool FProjectSettings::SaveToFile() const
	{
		const std::filesystem::path AuthoringPath = ResolveAuthoringPath();
		if (AuthoringPath.empty())
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_CORE, "No project source directory is known, so settings cannot be saved");
			return false;
		}

		// Load and modify rather than serialize from scratch: comments and any keys a newer version of
		// the engine might add have to survive a save from an older one.
		FJson Root;
		if (!FJsonUtils::LoadFromFile(AuthoringPath, Root) || !Root.is_object())
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "'{}' could not be read; writing a fresh file", AuthoringPath.string());
			Root = FJson::object();
			Root["version"] = 1;
		}

		// ProjectName is deliberately not written: CMake reads it at configure time to derive the
		// target and the shader output directory, so changing it here would desync the build.
		bool bOk = true;
		bOk &= FJsonUtils::Set(Root, "window.title", WindowTitle);
		bOk &= FJsonUtils::Set(Root, "window.width", WindowWidth);
		bOk &= FJsonUtils::Set(Root, "window.height", WindowHeight);
		bOk &= FJsonUtils::Set(Root, "rhi.backend", ToConfigToken(Backend));
		bOk &= FJsonUtils::Set(Root, "rhi.vsync", bVSync);
		bOk &= FJsonUtils::Set(Root, "rhi.backBufferCount", BackBufferCount);
		bOk &= FJsonUtils::Set(Root, "rhi.validation", Lime::ToString(Validation));
		bOk &= FJsonUtils::Set(Root, "editor.enabled", bEnableEditor);
		bOk &= FJsonUtils::Set(Root, "automation.enabled", bEnableAutomation);
		bOk &= FJsonUtils::Set(Root, "automation.port", AutomationPort);

		if (!bOk)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_CORE, "Refusing to save: '{}' has a key whose type conflicts with the schema",
			               AuthoringPath.string());
			return false;
		}

		if (!FJsonUtils::SaveToFile(AuthoringPath, Root))
		{
			return false;
		}

		// Keep the deployed copy in step so a restart without a rebuild sees the new values.
		const std::filesystem::path DeployedPath = FPlatformPaths::GetExecutableDirectory() / SettingsFileName;
		if (DeployedPath != AuthoringPath && std::filesystem::exists(DeployedPath))
		{
			std::error_code ErrorCode;
			std::filesystem::copy_file(AuthoringPath, DeployedPath, std::filesystem::copy_options::overwrite_existing, ErrorCode);
			if (ErrorCode)
			{
				// Not fatal: the authored file is already correct, the next build will copy it.
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "Saved settings but could not refresh '{}': {}", DeployedPath.string(),
				                 ErrorCode.message());
			}
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_CORE, "Project settings saved to '{}'", AuthoringPath.string());
		return true;
	}

	bool FProjectSettings::operator==(const FProjectSettings& Other) const
	{
		return ProjectName == Other.ProjectName && WindowTitle == Other.WindowTitle && WindowWidth == Other.WindowWidth &&
		       WindowHeight == Other.WindowHeight && Backend == Other.Backend && BackBufferCount == Other.BackBufferCount &&
		       bVSync == Other.bVSync && Validation == Other.Validation && bEnableEditor == Other.bEnableEditor &&
		       bEnableAutomation == Other.bEnableAutomation && AutomationPort == Other.AutomationPort;
	}

	std::filesystem::path FProjectSettings::ResolveSettingsPath()
	{
		const std::filesystem::path BesideExecutable = FPlatformPaths::GetExecutableDirectory() / SettingsFileName;
		if (std::filesystem::exists(BesideExecutable))
		{
			return BesideExecutable;
		}

		// Development fallback: the file is copied post build, but running straight from a fresh tree
		// should still work.
		const std::filesystem::path& SourceRoot = FPlatformPaths::GetProjectSourceDirectory();
		if (!SourceRoot.empty())
		{
			const std::filesystem::path InSourceTree = SourceRoot / SettingsFileName;
			if (std::filesystem::exists(InSourceTree))
			{
				return InSourceTree;
			}
		}

		return BesideExecutable;
	}

	bool FProjectSettings::LoadFromFile(const std::filesystem::path& Path)
	{
		FJson Root;
		if (!FJsonUtils::LoadFromFile(Path, Root))
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "Using built-in defaults because '{}' could not be read", Path.string());
			return false;
		}

		const int32 Version = FJsonUtils::ReadOr<int32>(Root, "version", 1, LogContext);
		if (Version != 1)
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "{}: unsupported version {}; parsing as version 1", LogContext, Version);
		}

		FJsonUtils::WarnUnknownKeys(Root, { "version", "name", "window", "rhi", "editor", "automation" }, {}, LogContext);

		ProjectName = FJsonUtils::ReadOr<std::string>(Root, "name", ProjectName, LogContext);
		// The window title defaults to the project name so the field can be omitted.
		WindowTitle = ProjectName;

		if (const FJson* Window = FJsonUtils::Find(Root, "window"))
		{
			FJsonUtils::WarnUnknownKeys(*Window, { "title", "width", "height" }, "window", LogContext);
			WindowTitle = FJsonUtils::ReadOr<std::string>(Root, "window.title", WindowTitle, LogContext);
			WindowWidth = FJsonUtils::ReadOr<uint32>(Root, "window.width", WindowWidth, LogContext);
			WindowHeight = FJsonUtils::ReadOr<uint32>(Root, "window.height", WindowHeight, LogContext);
		}

		if (const FJson* Rhi = FJsonUtils::Find(Root, "rhi"))
		{
			FJsonUtils::WarnUnknownKeys(*Rhi, { "backend", "vsync", "backBufferCount", "validation" }, "rhi", LogContext);

			const std::string BackendText = FJsonUtils::ReadOr<std::string>(Root, "rhi.backend", ToConfigToken(Backend), LogContext);
			ERHIBackend ParsedBackend = Backend;
			if (!TryParseBackend(BackendText, ParsedBackend))
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "{}: unknown backend '{}'; using {}", LogContext, BackendText,
				                 Lime::ToString(Backend));
			}
			else if (!IsBackendEnabled(ParsedBackend))
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "{}: backend {} is not compiled in; using {}", LogContext,
				                 Lime::ToString(ParsedBackend), Lime::ToString(Backend));
			}
			else
			{
				Backend = ParsedBackend;
			}

			bVSync = FJsonUtils::ReadOr<bool>(Root, "rhi.vsync", bVSync, LogContext);
			BackBufferCount = FJsonUtils::ReadOr<uint32>(Root, "rhi.backBufferCount", BackBufferCount, LogContext);
			if (BackBufferCount < 2 || BackBufferCount > 8)
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "{}: backBufferCount {} is out of range; using 3", LogContext, BackBufferCount);
				BackBufferCount = 3;
			}

			const std::string ValidationText = FJsonUtils::ReadOr<std::string>(Root, "rhi.validation", ToString(Validation), LogContext);
			if (!TryParseValidation(ValidationText, Validation))
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "{}: unknown validation mode '{}'; using {}", LogContext, ValidationText,
				                 ToString(Validation));
			}
		}

		if (const FJson* Editor = FJsonUtils::Find(Root, "editor"))
		{
			FJsonUtils::WarnUnknownKeys(*Editor, { "enabled" }, "editor", LogContext);
#if LIME_WITH_EDITOR
			bEnableEditor = FJsonUtils::ReadOr<bool>(Root, "editor.enabled", bEnableEditor, LogContext);
#endif
		}

		if (const FJson* Automation = FJsonUtils::Find(Root, "automation"))
		{
			FJsonUtils::WarnUnknownKeys(*Automation, { "enabled", "port" }, "automation", LogContext);
#if LIME_WITH_AUTOMATION
			bEnableAutomation = FJsonUtils::ReadOr<bool>(Root, "automation.enabled", bEnableAutomation, LogContext);
#endif
			const uint32 Port = FJsonUtils::ReadOr<uint32>(Root, "automation.port", AutomationPort, LogContext);
			// 0 is valid and means "let the OS choose"; the real value is written to AutomationPort.txt.
			if (Port > 65535)
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "{}: automation.port {} is out of range; using {}", LogContext, Port,
				                 AutomationPort);
			}
			else
			{
				AutomationPort = Port;
			}
		}

		return true;
	}

	FJson FProjectSettings::ToJson() const
	{
		// Same key layout LoadFromFile expects, so a client can send back what it received.
		FJson Root = FJson::object();
		Root["version"] = 1;
		Root["name"] = ProjectName;

		FJson& Window = Root["window"] = FJson::object();
		Window["title"] = WindowTitle;
		Window["width"] = WindowWidth;
		Window["height"] = WindowHeight;

		FJson& Rhi = Root["rhi"] = FJson::object();
		Rhi["backend"] = ToConfigToken(Backend);
		Rhi["vsync"] = bVSync;
		Rhi["backBufferCount"] = BackBufferCount;
		Rhi["validation"] = Lime::ToString(Validation);

		FJson& Editor = Root["editor"] = FJson::object();
		Editor["enabled"] = bEnableEditor;

		FJson& Automation = Root["automation"] = FJson::object();
		Automation["enabled"] = bEnableAutomation;
		Automation["port"] = AutomationPort;

		return Root;
	}

	bool FProjectSettings::ApplyJson(const FJson& Json, std::string& OutError)
	{
		if (!Json.is_object())
		{
			OutError = "Settings must be an object";
			return false;
		}

		// Validated against a copy first, so a rejected key leaves the live settings untouched.
		FProjectSettings Candidate = *this;

		const auto ReadBool = [&Json, &OutError](const char* Path, bool& OutValue)
		{
			const FJson* Node = FJsonUtils::Find(Json, Path);
			if (Node == nullptr)
			{
				return true;
			}
			if (!Node->is_boolean())
			{
				OutError = fmt::format("'{}' must be a boolean", Path);
				return false;
			}
			OutValue = Node->get<bool>();
			return true;
		};

		const auto ReadUInt = [&Json, &OutError](const char* Path, uint32 Minimum, uint32 Maximum, uint32& OutValue)
		{
			const FJson* Node = FJsonUtils::Find(Json, Path);
			if (Node == nullptr)
			{
				return true;
			}
			// is_number_integer rather than is_number_unsigned: a positive value assigned from a signed
			// integer is stored as signed, so the stricter check would reject valid input.
			if (!Node->is_number_integer())
			{
				OutError = fmt::format("'{}' must be an integer", Path);
				return false;
			}

			const int64 Parsed = Node->get<int64>();
			if (Parsed < static_cast<int64>(Minimum) || Parsed > static_cast<int64>(Maximum))
			{
				OutError = fmt::format("'{}' must be between {} and {}", Path, Minimum, Maximum);
				return false;
			}
			OutValue = static_cast<uint32>(Parsed);
			return true;
		};

		const auto ReadString = [&Json, &OutError](const char* Path, std::string& OutValue)
		{
			const FJson* Node = FJsonUtils::Find(Json, Path);
			if (Node == nullptr)
			{
				return true;
			}
			if (!Node->is_string())
			{
				OutError = fmt::format("'{}' must be a string", Path);
				return false;
			}
			OutValue = Node->get<std::string>();
			return true;
		};

		if (!ReadString("window.title", Candidate.WindowTitle) || !ReadUInt("window.width", 1, MaxWindowDimension, Candidate.WindowWidth) ||
		    !ReadUInt("window.height", 1, MaxWindowDimension, Candidate.WindowHeight) ||
		    !ReadUInt("rhi.backBufferCount", 2, 8, Candidate.BackBufferCount) || !ReadBool("rhi.vsync", Candidate.bVSync) ||
		    !ReadBool("editor.enabled", Candidate.bEnableEditor) ||
		    !ReadBool("automation.enabled", Candidate.bEnableAutomation) ||
		    !ReadUInt("automation.port", 0, 65535, Candidate.AutomationPort))
		{
			return false;
		}

		if (const FJson* BackendNode = FJsonUtils::Find(Json, "rhi.backend"))
		{
			if (!BackendNode->is_string())
			{
				OutError = "'rhi.backend' must be a string";
				return false;
			}

			const std::string BackendText = BackendNode->get<std::string>();
			ERHIBackend Parsed = Candidate.Backend;
			if (!TryParseBackend(BackendText, Parsed))
			{
				OutError = fmt::format("Unknown backend '{}'", BackendText);
				return false;
			}
			if (!IsBackendEnabled(Parsed))
			{
				OutError = fmt::format("Backend {} is not compiled in", Lime::ToString(Parsed));
				return false;
			}
			Candidate.Backend = Parsed;
		}

		if (const FJson* ValidationNode = FJsonUtils::Find(Json, "rhi.validation"))
		{
			if (!ValidationNode->is_string())
			{
				OutError = "'rhi.validation' must be a string";
				return false;
			}
			if (!TryParseValidation(ValidationNode->get<std::string>(), Candidate.Validation))
			{
				OutError = fmt::format("Unknown validation mode '{}'", ValidationNode->get<std::string>());
				return false;
			}
		}

		*this = std::move(Candidate);
		return true;
	}

	void FProjectSettings::ApplyCommandLine(int ArgumentCount, const char* const* Arguments)
	{
		for (int Index = 1; Index < ArgumentCount; ++Index)
		{
			const std::string_view Argument(Arguments[Index]);
			std::string_view Value;

			if (MatchOption(Argument, "--rhi", Value))
			{
				ERHIBackend Parsed = Backend;
				if (!TryParseBackend(Value, Parsed))
				{
					LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "Unknown backend '{}'; keeping {}", Value, Lime::ToString(Backend));
				}
				else if (!IsBackendEnabled(Parsed))
				{
					LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "Backend {} is not compiled in; keeping {}", Lime::ToString(Parsed),
					                 Lime::ToString(Backend));
				}
				else
				{
					Backend = Parsed;
				}
			}
			else if (MatchOption(Argument, "--width", Value))
			{
				TryParseUInt(Value, WindowWidth);
			}
			else if (MatchOption(Argument, "--height", Value))
			{
				TryParseUInt(Value, WindowHeight);
			}
			else if (MatchOption(Argument, "--validation", Value))
			{
				if (!TryParseValidation(Value, Validation))
				{
					LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "Unknown validation mode '{}'; keeping {}", Value, ToString(Validation));
				}
			}
			else if (Argument == "--no-editor")
			{
				bEnableEditor = false;
			}
			else if (Argument == "--no-vsync")
			{
				bVSync = false;
			}
			else if (Argument == "--no-validation")
			{
				Validation = EValidationMode::Off;
			}
			else if (Argument == "--automation")
			{
				bEnableAutomation = true;
			}
			else if (Argument == "--no-automation")
			{
				bEnableAutomation = false;
			}
			else if (MatchOption(Argument, "--automation-port", Value))
			{
				uint32 Port = AutomationPort;
				if (TryParsePort(Value, Port))
				{
					AutomationPort = Port;
					// Naming a port is an unambiguous request to have the server running.
					bEnableAutomation = true;
				}
				else
				{
					LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "Invalid automation port '{}'; keeping {}", Value, AutomationPort);
				}
			}
			else if (MatchOption(Argument, "--project", Value))
			{
				// Consumed before this point; listed here so it is not reported as unknown.
			}
			else
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "Ignoring unknown argument '{}'", Argument);
			}
		}
	}

	bool FProjectSettings::IsValidationEnabled() const
	{
		switch (Validation)
		{
			case EValidationMode::Off:
				return false;
			case EValidationMode::On:
				return true;
			case EValidationMode::DebugOnly:
				return LIME_DEBUG != 0;
		}
		return false;
	}

	void FProjectSettings::LogSummary() const
	{
		LIME_LOG_INFO(LIME_LOG_CATEGORY_CORE, "Project '{}' | {} | {}x{} | vsync {} | validation {} | editor {} | automation {}",
		              ProjectName, Lime::ToString(Backend), WindowWidth, WindowHeight, bVSync ? "on" : "off",
		              IsValidationEnabled() ? "on" : "off", bEnableEditor ? "on" : "off", bEnableAutomation ? "on" : "off");
	}
} // namespace Lime
