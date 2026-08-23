#include "Editor/EditorSettings.h"

#include "Core/Json/JsonUtils.h"
#include "Core/Logging/LogManager.h"
#include "Platform/PlatformPaths.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>

namespace Lime
{
	namespace
	{
		constexpr const char* SettingsFileName = "EditorSettings.json";
		constexpr const char* LogContext = "EditorSettings.json";

		// Reads one colour channel out of an array node. Clamps instead of rejecting: a value slightly
		// outside the range is a rounding artefact from an external tool, not something worth
		// refusing to start over. Indexed directly because FJsonUtils::Find only walks objects.
		bool TryReadChannel(const FJson& Array, SizeType Index, float& OutValue)
		{
			const FJson& Channel = Array[Index];
			if (!Channel.is_number())
			{
				return false;
			}
			OutValue = std::clamp(Channel.get<float>(), 0.0f, 1.0f);
			return true;
		}
	} // namespace

	const char* ToString(EEditorTheme Theme)
	{
		switch (Theme)
		{
			case EEditorTheme::Dark:
				return "dark";
			case EEditorTheme::Light:
				return "light";
			case EEditorTheme::Classic:
				return "classic";
		}
		return "dark";
	}

	bool TryParseEditorTheme(std::string_view Text, EEditorTheme& OutTheme)
	{
		if (Text == "dark")
		{
			OutTheme = EEditorTheme::Dark;
			return true;
		}
		if (Text == "light")
		{
			OutTheme = EEditorTheme::Light;
			return true;
		}
		if (Text == "classic")
		{
			OutTheme = EEditorTheme::Classic;
			return true;
		}
		return false;
	}

	std::filesystem::path FEditorSettings::ResolveAuthoringPath()
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

	std::filesystem::path FEditorSettings::ResolveSettingsPath()
	{
		std::filesystem::path BesideExecutable = FPlatformPaths::GetExecutableDirectory() / SettingsFileName;
		if (std::filesystem::exists(BesideExecutable))
		{
			return BesideExecutable;
		}

		// Development fallback: the file is copied post build, but running straight from a fresh tree
		// should still work.
		const std::filesystem::path& SourceRoot = FPlatformPaths::GetProjectSourceDirectory();
		if (!SourceRoot.empty())
		{
			std::filesystem::path InSourceTree = SourceRoot / SettingsFileName;
			if (std::filesystem::exists(InSourceTree))
			{
				return InSourceTree;
			}
		}

		return BesideExecutable;
	}

	bool FEditorSettings::LoadFromFile(const std::filesystem::path& Path)
	{
		// Absence is the normal case: a project that is happy with the defaults ships no file, so it
		// is checked before LoadFromFile can warn about an unreadable one.
		if (!std::filesystem::exists(Path))
		{
			return false;
		}

		FJson Root;
		if (!FJsonUtils::LoadFromFile(Path, Root))
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "Using built-in editor defaults because '{}' could not be read", Path.string());
			return false;
		}

		const int32 Version = FJsonUtils::ReadOr<int32>(Root, "version", 1, LogContext);
		if (Version != 1)
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "{}: unsupported version {}; parsing as version 1", LogContext, Version);
		}

		FJsonUtils::WarnUnknownKeys(Root, { "version", "appearance" }, {}, LogContext);

		if (const FJson* Appearance = FJsonUtils::Find(Root, "appearance"))
		{
			FJsonUtils::WarnUnknownKeys(*Appearance, { "fontSize", "theme", "accentColor" }, "appearance", LogContext);

			const float RequestedFontSize = FJsonUtils::ReadOr<float>(Root, "appearance.fontSize", FontSize, LogContext);
			if (RequestedFontSize < MinFontSize || RequestedFontSize > MaxFontSize)
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "{}: fontSize {} is outside {}..{}; using {}", LogContext, RequestedFontSize,
				                 MinFontSize, MaxFontSize, FontSize);
			}
			else
			{
				FontSize = RequestedFontSize;
			}

			const std::string ThemeText = FJsonUtils::ReadOr<std::string>(Root, "appearance.theme", ToString(Theme), LogContext);
			if (!TryParseEditorTheme(ThemeText, Theme))
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "{}: unknown theme '{}'; using {}", LogContext, ThemeText, ToString(Theme));
			}

			// An array rather than three keys: it is the shape every colour picker and JSON tool
			// already expects, and it keeps the file readable.
			if (const FJson* Accent = FJsonUtils::Find(Root, "appearance.accentColor"))
			{
				FVector3 Parsed = AccentColor;
				if (Accent->is_array() && Accent->size() == 3 && TryReadChannel(*Accent, 0, Parsed.X) &&
				    TryReadChannel(*Accent, 1, Parsed.Y) && TryReadChannel(*Accent, 2, Parsed.Z))
				{
					AccentColor = Parsed;
				}
				else
				{
					LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "{}: accentColor must be an array of 3 numbers; using the default",
					                 LogContext);
				}
			}
		}

		return true;
	}

	bool FEditorSettings::SaveToFile() const
	{
		const std::filesystem::path AuthoringPath = ResolveAuthoringPath();
		if (AuthoringPath.empty())
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_EDITOR, "No project source directory is known, so editor settings cannot be saved");
			return false;
		}

		// Load and modify rather than serialize from scratch: comments and any keys a newer version of
		// the engine might add have to survive a save from an older one. A missing file is expected
		// here, since the defaults ship without one.
		FJson Root;
		if (!std::filesystem::exists(AuthoringPath) || !FJsonUtils::LoadFromFile(AuthoringPath, Root) || !Root.is_object())
		{
			Root = FJson::object();
			Root["version"] = 1;
		}

		bool bOk = true;
		bOk &= FJsonUtils::Set(Root, "appearance.fontSize", FontSize);
		bOk &= FJsonUtils::Set(Root, "appearance.theme", ToString(Theme));
		bOk &= FJsonUtils::Set(Root, "appearance.accentColor", FJson::array({ AccentColor.X, AccentColor.Y, AccentColor.Z }));

		if (!bOk)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_EDITOR, "Refusing to save: '{}' has a key whose type conflicts with the schema",
			               AuthoringPath.string());
			return false;
		}

		if (!FJsonUtils::SaveToFile(AuthoringPath, Root))
		{
			return false;
		}

		// Keep the deployed copy in step so a restart without a rebuild sees the new values.
		const std::filesystem::path DeployedPath = FPlatformPaths::GetExecutableDirectory() / SettingsFileName;
		if (DeployedPath != AuthoringPath)
		{
			std::error_code ErrorCode;
			std::filesystem::copy_file(AuthoringPath, DeployedPath, std::filesystem::copy_options::overwrite_existing, ErrorCode);
			if (ErrorCode)
			{
				// Not fatal: the authored file is already correct, the next build will copy it.
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "Saved editor settings but could not refresh '{}': {}", DeployedPath.string(),
				                 ErrorCode.message());
			}
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "Editor settings saved to '{}'", AuthoringPath.string());
		return true;
	}

	FJson FEditorSettings::ToJson() const
	{
		// Same key layout LoadFromFile expects, so a client can send back what it received.
		FJson Root = FJson::object();
		Root["version"] = 1;

		FJson& Appearance = Root["appearance"] = FJson::object();
		Appearance["fontSize"] = FontSize;
		Appearance["theme"] = ToString(Theme);
		Appearance["accentColor"] = FJson::array({ AccentColor.X, AccentColor.Y, AccentColor.Z });

		return Root;
	}

	bool FEditorSettings::ApplyJson(const FJson& Json, std::string& OutError)
	{
		if (!Json.is_object())
		{
			OutError = "Editor settings must be an object";
			return false;
		}

		// Validated against a copy first, so a rejected key leaves the live settings untouched.
		FEditorSettings Candidate = *this;

		if (const FJson* FontNode = FJsonUtils::Find(Json, "appearance.fontSize"))
		{
			if (!FontNode->is_number())
			{
				OutError = "'appearance.fontSize' must be a number";
				return false;
			}

			const float Value = FontNode->get<float>();
			if (Value < MinFontSize || Value > MaxFontSize)
			{
				OutError = fmt::format("'appearance.fontSize' must be between {} and {}", MinFontSize, MaxFontSize);
				return false;
			}
			Candidate.FontSize = Value;
		}

		if (const FJson* ThemeNode = FJsonUtils::Find(Json, "appearance.theme"))
		{
			if (!ThemeNode->is_string())
			{
				OutError = "'appearance.theme' must be a string";
				return false;
			}
			if (!TryParseEditorTheme(ThemeNode->get<std::string>(), Candidate.Theme))
			{
				OutError = fmt::format("Unknown editor theme '{}'", ThemeNode->get<std::string>());
				return false;
			}
		}

		if (const FJson* AccentNode = FJsonUtils::Find(Json, "appearance.accentColor"))
		{
			if (!AccentNode->is_array() || AccentNode->size() != 3)
			{
				OutError = "'appearance.accentColor' must be an array of 3 numbers";
				return false;
			}

			float Channels[3] = { Candidate.AccentColor.X, Candidate.AccentColor.Y, Candidate.AccentColor.Z };
			for (SizeType Index = 0; Index < 3; ++Index)
			{
				const FJson& Channel = (*AccentNode)[Index];
				if (!Channel.is_number())
				{
					OutError = "'appearance.accentColor' must be an array of 3 numbers";
					return false;
				}

				const float Value = Channel.get<float>();
				if (Value < 0.0f || Value > 1.0f)
				{
					OutError = "'appearance.accentColor' channels must be between 0 and 1";
					return false;
				}
				Channels[Index] = Value;
			}

			Candidate.AccentColor = { Channels[0], Channels[1], Channels[2] };
		}

		*this = Candidate;
		return true;
	}

	bool FEditorSettings::operator==(const FEditorSettings& Other) const
	{
		return FontSize == Other.FontSize && Theme == Other.Theme && AccentColor == Other.AccentColor;
	}

	void FEditorSettings::LogSummary() const
	{
		LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "Editor appearance: theme {} | font {:.0f} | accent {:.2f},{:.2f},{:.2f}", ToString(Theme),
		              FontSize, AccentColor.X, AccentColor.Y, AccentColor.Z);
	}
} // namespace Lime
