#include "Core/Json/JsonUtils.h"

#include "Core/Logging/LogManager.h"

#include <fstream>

namespace Lime
{
	namespace
	{
		std::string DescribeContext(std::string_view Context)
		{
			return Context.empty() ? std::string() : std::string(Context) + ": ";
		}
	} // namespace

	bool FJsonUtils::LoadFromFile(const std::filesystem::path& Path, FJson& OutJson)
	{
		std::ifstream Stream(Path);
		if (!Stream.is_open())
		{
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "Cannot open JSON file '{}'", Path.string());
			return false;
		}

		try
		{
			// Comments are allowed so configuration files can be annotated.
			OutJson = FJson::parse(Stream, nullptr, true, true);
		}
		catch (const FJson::parse_error& Error)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_CORE, "Failed to parse '{}': {}", Path.string(), Error.what());
			return false;
		}

		if (!OutJson.is_object())
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_CORE, "'{}' must contain a JSON object at the top level", Path.string());
			return false;
		}

		return true;
	}

	bool FJsonUtils::SaveToFile(const std::filesystem::path& Path, const FJson& Json, int32 IndentWidth, char IndentChar)
	{
		std::error_code ErrorCode;
		if (Path.has_parent_path())
		{
			std::filesystem::create_directories(Path.parent_path(), ErrorCode);
		}

		// Write to a sibling temporary first: an interrupted write must not destroy the existing file.
		std::filesystem::path TempPath = Path;
		TempPath += ".tmp";

		{
			std::ofstream Stream(TempPath, std::ios::binary | std::ios::trunc);
			if (!Stream.is_open())
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_CORE, "Cannot open '{}' for writing", TempPath.string());
				return false;
			}

			try
			{
				Stream << Json.dump(IndentWidth, IndentChar) << '\n';
			}
			catch (const FJson::exception& Error)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_CORE, "Failed to serialize JSON for '{}': {}", Path.string(), Error.what());
				Stream.close();
				std::filesystem::remove(TempPath, ErrorCode);
				return false;
			}

			if (!Stream.good())
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_CORE, "Failed while writing '{}'", TempPath.string());
				Stream.close();
				std::filesystem::remove(TempPath, ErrorCode);
				return false;
			}
		}

		// rename over an existing file fails on Windows, so replace when the target is already there.
		if (std::filesystem::exists(Path))
		{
			std::filesystem::remove(Path, ErrorCode);
		}
		std::filesystem::rename(TempPath, Path, ErrorCode);
		if (ErrorCode)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_CORE, "Cannot move '{}' into place: {}", TempPath.string(), ErrorCode.message());
			std::filesystem::remove(TempPath, ErrorCode);
			return false;
		}

		return true;
	}

	const FJson* FJsonUtils::Find(const FJson& Root, std::string_view DottedPath)
	{
		const FJson* Current = &Root;
		SizeType Offset = 0;

		while (Offset <= DottedPath.size())
		{
			const SizeType Separator = DottedPath.find('.', Offset);
			const std::string_view Segment = DottedPath.substr(Offset, Separator - Offset);

			if (Segment.empty() || !Current->is_object())
			{
				return nullptr;
			}

			const auto Iterator = Current->find(Segment);
			if (Iterator == Current->end())
			{
				return nullptr;
			}
			Current = &(*Iterator);

			if (Separator == std::string_view::npos)
			{
				break;
			}
			Offset = Separator + 1;
		}

		return Current;
	}

	bool FJsonUtils::Set(FJson& Root, std::string_view DottedPath, FJson Value)
	{
		if (DottedPath.empty())
		{
			return false;
		}

		// An empty or non-object root is turned into an object; anything else would discard the value.
		if (Root.is_null())
		{
			Root = FJson::object();
		}
		if (!Root.is_object())
		{
			return false;
		}

		FJson* Current = &Root;
		SizeType Offset = 0;

		while (true)
		{
			const SizeType Separator = DottedPath.find('.', Offset);
			const std::string_view Segment = DottedPath.substr(Offset, Separator - Offset);
			if (Segment.empty())
			{
				return false;
			}

			const std::string Key(Segment);
			if (Separator == std::string_view::npos)
			{
				// Assigning through operator[] leaves the other keys of this object untouched.
				(*Current)[Key] = std::move(Value);
				return true;
			}

			FJson& Child = (*Current)[Key];
			if (Child.is_null())
			{
				Child = FJson::object();
			}
			else if (!Child.is_object())
			{
				// Overwriting a scalar with an object would silently drop the user's data.
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_CORE, "Cannot write '{}': '{}' is not an object", DottedPath, Key);
				return false;
			}

			Current = &Child;
			Offset = Separator + 1;
		}
	}

	void FJsonUtils::WarnUnknownKeys(const FJson& Object, const std::vector<std::string_view>& KnownKeys, std::string_view DottedPath,
	                                 std::string_view Context)
	{
		if (!Object.is_object())
		{
			return;
		}

		for (const auto& Entry : Object.items())
		{
			const std::string& Key = Entry.key();
			const bool bKnown = std::find(KnownKeys.begin(), KnownKeys.end(), std::string_view(Key)) != KnownKeys.end();
			if (!bKnown)
			{
				const std::string Prefix = DottedPath.empty() ? std::string() : std::string(DottedPath) + ".";
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "{}unknown key '{}{}' ignored", DescribeContext(Context), Prefix, Key);
			}
		}
	}

	void FJsonUtils::LogTypeMismatch(std::string_view DottedPath, std::string_view ActualType, std::string_view Context)
	{
		LIME_LOG_WARNING(LIME_LOG_CATEGORY_CORE, "{}'{}' has unexpected type {}; using the default value", DescribeContext(Context),
		                 DottedPath, ActualType);
	}

	const char* FJsonUtils::TypeNameOf(const FJson& Node)
	{
		return Node.type_name();
	}

	bool FJsonUtils::TryConvert(const FJson& Node, bool& OutValue)
	{
		if (!Node.is_boolean())
		{
			return false;
		}
		OutValue = Node.get<bool>();
		return true;
	}

	bool FJsonUtils::TryConvert(const FJson& Node, int32& OutValue)
	{
		if (!Node.is_number_integer())
		{
			return false;
		}
		OutValue = Node.get<int32>();
		return true;
	}

	bool FJsonUtils::TryConvert(const FJson& Node, uint32& OutValue)
	{
		// Reject negatives explicitly instead of letting them wrap around.
		if (!Node.is_number_integer() || Node.get<int64>() < 0)
		{
			return false;
		}
		OutValue = Node.get<uint32>();
		return true;
	}

	bool FJsonUtils::TryConvert(const FJson& Node, float& OutValue)
	{
		if (!Node.is_number())
		{
			return false;
		}
		OutValue = Node.get<float>();
		return true;
	}

	bool FJsonUtils::TryConvert(const FJson& Node, std::string& OutValue)
	{
		if (!Node.is_string())
		{
			return false;
		}
		OutValue = Node.get<std::string>();
		return true;
	}
} // namespace Lime
