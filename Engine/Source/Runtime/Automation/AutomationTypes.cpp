#include "Automation/AutomationTypes.h"

#include <spdlog/fmt/fmt.h>

namespace Lime
{
	namespace
	{
		// Reports the JSON type of a node so a bad parameter names its own problem.
		const char* TypeNameOf(const FJson& Node)
		{
			if (Node.is_boolean())
			{
				return "boolean";
			}
			if (Node.is_number())
			{
				return "number";
			}
			if (Node.is_string())
			{
				return "string";
			}
			if (Node.is_array())
			{
				return "array";
			}
			if (Node.is_object())
			{
				return "object";
			}
			return "null";
		}
	} // namespace

	bool FAutomationInvocation::TryGetString(const char* Key, std::string& OutValue, std::string& OutError) const
	{
		if (!Params.is_object())
		{
			return true;
		}

		const auto Iterator = Params.find(Key);
		if (Iterator == Params.end())
		{
			return true;
		}
		if (!Iterator->is_string())
		{
			OutError = fmt::format("'{}' must be a string, got {}", Key, TypeNameOf(*Iterator));
			return false;
		}

		OutValue = Iterator->get<std::string>();
		return true;
	}

	bool FAutomationInvocation::TryGetBool(const char* Key, bool& OutValue, std::string& OutError) const
	{
		if (!Params.is_object())
		{
			return true;
		}

		const auto Iterator = Params.find(Key);
		if (Iterator == Params.end())
		{
			return true;
		}
		if (!Iterator->is_boolean())
		{
			OutError = fmt::format("'{}' must be a boolean, got {}", Key, TypeNameOf(*Iterator));
			return false;
		}

		OutValue = Iterator->get<bool>();
		return true;
	}

	bool FAutomationInvocation::TryGetUInt(const char* Key, uint32& OutValue, std::string& OutError) const
	{
		if (!Params.is_object())
		{
			return true;
		}

		const auto Iterator = Params.find(Key);
		if (Iterator == Params.end())
		{
			return true;
		}
		// is_number_integer rather than is_number_unsigned: a positive value can still be stored as a
		// signed integer, so the stricter check would reject valid input. The sign is tested below.
		if (!Iterator->is_number_integer())
		{
			OutError = fmt::format("'{}' must be an integer, got {}", Key, TypeNameOf(*Iterator));
			return false;
		}

		const int64 Parsed = Iterator->get<int64>();
		if (Parsed < 0)
		{
			OutError = fmt::format("'{}' must not be negative", Key);
			return false;
		}

		OutValue = static_cast<uint32>(Parsed);
		return true;
	}

	bool FAutomationInvocation::RequireString(const char* Key, std::string& OutValue)
	{
		std::string LocalError;
		if (!TryGetString(Key, OutValue, LocalError))
		{
			Fail(std::move(LocalError));
			return false;
		}
		if (OutValue.empty())
		{
			Fail(fmt::format("'{}' is required", Key));
			return false;
		}
		return true;
	}
} // namespace Lime
