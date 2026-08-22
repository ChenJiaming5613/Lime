// Small helpers over nlohmann/json. Reads never fail hard: a missing or malformed value logs a
// warning naming the field and falls back to the caller's default.

#pragma once

#include "Core/CoreTypes.h"

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace Lime
{
	// ordered_json rather than json: it preserves key insertion order, so saving a config file keeps
	// the hand authored layout instead of reordering everything alphabetically.
	using FJson = nlohmann::ordered_json;

	class FJsonUtils
	{
	public:
		// Parses the file with comments allowed. Returns false and logs on IO or syntax errors.
		static bool LoadFromFile(const std::filesystem::path& Path, FJson& OutJson);

		// Writes via a temporary file and a rename, so a crash mid-write cannot leave a truncated
		// config behind. Creates parent directories as needed.
		static bool SaveToFile(const std::filesystem::path& Path, const FJson& Json, int32 IndentWidth = 1, char IndentChar = '\t');

		// Dot separated lookup, for example "window.width". Returns nullptr when absent.
		static const FJson* Find(const FJson& Root, std::string_view DottedPath);

		// Assigns a value at a dot separated path, creating intermediate objects. Existing siblings are
		// preserved, which is what keeps hand written keys intact when the editor saves.
		// Returns false when a path segment exists but is not an object.
		static bool Set(FJson& Root, std::string_view DottedPath, FJson Value);

		// Reads a value, logging a warning and returning Fallback on a type mismatch.
		// Context is prefixed to diagnostics so the caller can name the file.
		template<typename ValueType>
		static ValueType ReadOr(const FJson& Root, std::string_view DottedPath, const ValueType& Fallback, std::string_view Context = {})
		{
			const FJson* Node = Find(Root, DottedPath);
			if (Node == nullptr)
			{
				return Fallback;
			}

			ValueType Result = Fallback;
			if (!TryConvert(*Node, Result))
			{
				LogTypeMismatch(DottedPath, TypeNameOf(*Node), Context);
				return Fallback;
			}
			return Result;
		}

		// Warns about keys the engine does not know about; catching typos early is worth the noise.
		static void WarnUnknownKeys(const FJson& Object, const std::vector<std::string_view>& KnownKeys, std::string_view DottedPath,
		                            std::string_view Context = {});

	private:
		static void LogTypeMismatch(std::string_view DottedPath, std::string_view ActualType, std::string_view Context);
		static const char* TypeNameOf(const FJson& Node);

		static bool TryConvert(const FJson& Node, bool& OutValue);
		static bool TryConvert(const FJson& Node, int32& OutValue);
		static bool TryConvert(const FJson& Node, uint32& OutValue);
		static bool TryConvert(const FJson& Node, float& OutValue);
		static bool TryConvert(const FJson& Node, std::string& OutValue);
	};
} // namespace Lime
