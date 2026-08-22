#include "Engine/EngineConfig.h"

#include "Core/Logging/LogManager.h"

#include <charconv>
#include <string_view>

namespace Lime
{
	namespace
	{
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

	void FEngineConfig::ParseCommandLine(int ArgumentCount, const char* const* Arguments)
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
					LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "Unknown backend '{}'; keeping {}", Value, ToString(Backend));
				}
				else if (!IsBackendEnabled(Parsed))
				{
					LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "Backend {} is not compiled in; keeping {}", ToString(Parsed),
					                 ToString(Backend));
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
				bEnableNvrhiValidation = false;
				bEnableDebugRuntime = false;
			}
			else
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "Ignoring unknown argument '{}'", Argument);
			}
		}
	}
} // namespace Lime
