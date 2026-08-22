// Covers the JSON write path the project settings editor relies on. The important guarantee is that
// saving preserves keys the engine does not know about: a config edited by a newer engine version, or
// annotated by hand, must survive a save from this one.

#include "Core/Json/JsonUtils.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Lime;

namespace
{
	// Each test gets its own directory so they can run in any order.
	class FTempDirectory
	{
	public:
		explicit FTempDirectory(const char* Name)
		{
			Path = std::filesystem::temp_directory_path() / "LimeEngineTests" / Name;
			std::error_code ErrorCode;
			std::filesystem::remove_all(Path, ErrorCode);
			std::filesystem::create_directories(Path, ErrorCode);
		}

		~FTempDirectory()
		{
			std::error_code ErrorCode;
			std::filesystem::remove_all(Path, ErrorCode);
		}

		FTempDirectory(const FTempDirectory&) = delete;
		FTempDirectory& operator=(const FTempDirectory&) = delete;

		std::filesystem::path File(const char* Name) const { return Path / Name; }

	private:
		std::filesystem::path Path;
	};

	void WriteText(const std::filesystem::path& Path, std::string_view Text)
	{
		std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
		Stream << Text;
	}
} // namespace

TEST_CASE("Set assigns values at nested paths", "[Core][Json][Save]")
{
	FJson Root = FJson::object();

	REQUIRE(FJsonUtils::Set(Root, "name", "Sample"));
	REQUIRE(FJsonUtils::Set(Root, "window.width", 1280u));
	REQUIRE(FJsonUtils::Set(Root, "deeply.nested.flag", true));

	REQUIRE(Root["name"].get<std::string>() == "Sample");
	REQUIRE(Root["window"]["width"].get<uint32>() == 1280);
	REQUIRE(Root["deeply"]["nested"]["flag"].get<bool>() == true);
}

TEST_CASE("Set works on a null root by turning it into an object", "[Core][Json][Save]")
{
	FJson Root;
	REQUIRE(Root.is_null());
	REQUIRE(FJsonUtils::Set(Root, "a.b", 1));
	REQUIRE(Root.is_object());
	REQUIRE(Root["a"]["b"].get<int32>() == 1);
}

TEST_CASE("Set preserves sibling keys, including unknown ones", "[Core][Json][Save]")
{
	// This is the property that makes load-modify-save safe.
	FJson Root = FJson::parse(R"({
		"version": 1,
		"futureFeature": { "enabled": true },
		"window": { "title": "Old", "customKey": 42 }
	})");

	REQUIRE(FJsonUtils::Set(Root, "window.title", "New"));

	REQUIRE(Root["window"]["title"].get<std::string>() == "New");
	REQUIRE(Root["window"]["customKey"].get<int32>() == 42);
	REQUIRE(Root["futureFeature"]["enabled"].get<bool>() == true);
	REQUIRE(Root["version"].get<int32>() == 1);
}

TEST_CASE("Set refuses to overwrite a scalar with an object", "[Core][Json][Save]")
{
	// Doing so would silently discard whatever the user had written there.
	FJson Root = FJson::parse(R"({ "window": "not an object" })");
	REQUIRE_FALSE(FJsonUtils::Set(Root, "window.width", 800u));
	REQUIRE(Root["window"].get<std::string>() == "not an object");
}

TEST_CASE("Set rejects malformed paths", "[Core][Json][Save]")
{
	FJson Root = FJson::object();
	REQUIRE_FALSE(FJsonUtils::Set(Root, "", 1));
	REQUIRE_FALSE(FJsonUtils::Set(Root, "a..b", 1));
	REQUIRE_FALSE(FJsonUtils::Set(Root, "a.", 1));
}

TEST_CASE("Save and load round trip through a file", "[Core][Json][Save]")
{
	const FTempDirectory Temp("JsonRoundTrip");
	const std::filesystem::path Path = Temp.File("settings.json");

	FJson Written = FJson::object();
	FJsonUtils::Set(Written, "name", "RoundTrip");
	FJsonUtils::Set(Written, "window.width", 1920u);
	FJsonUtils::Set(Written, "rhi.vsync", false);

	REQUIRE(FJsonUtils::SaveToFile(Path, Written));
	REQUIRE(std::filesystem::exists(Path));

	FJson Read;
	REQUIRE(FJsonUtils::LoadFromFile(Path, Read));
	REQUIRE(FJsonUtils::ReadOr<std::string>(Read, "name", "") == "RoundTrip");
	REQUIRE(FJsonUtils::ReadOr<uint32>(Read, "window.width", 0) == 1920);
	REQUIRE(FJsonUtils::ReadOr<bool>(Read, "rhi.vsync", true) == false);
}

TEST_CASE("Saving creates missing parent directories", "[Core][Json][Save]")
{
	const FTempDirectory Temp("JsonNestedDir");
	const std::filesystem::path Path = Temp.File("a") / "b" / "settings.json";

	REQUIRE(FJsonUtils::SaveToFile(Path, FJson::parse(R"({ "ok": true })")));
	REQUIRE(std::filesystem::exists(Path));
}

TEST_CASE("Saving replaces an existing file and leaves no temporary behind", "[Core][Json][Save]")
{
	const FTempDirectory Temp("JsonReplace");
	const std::filesystem::path Path = Temp.File("settings.json");

	WriteText(Path, R"({ "old": true })");
	REQUIRE(FJsonUtils::SaveToFile(Path, FJson::parse(R"({ "new": true })")));

	FJson Read;
	REQUIRE(FJsonUtils::LoadFromFile(Path, Read));
	REQUIRE(Read.contains("new"));
	REQUIRE_FALSE(Read.contains("old"));

	std::filesystem::path TempFile = Path;
	TempFile += ".tmp";
	REQUIRE_FALSE(std::filesystem::exists(TempFile));
}

TEST_CASE("A load-modify-save cycle keeps unrelated content intact", "[Core][Json][Save]")
{
	const FTempDirectory Temp("JsonModifyCycle");
	const std::filesystem::path Path = Temp.File("settings.json");

	// Mirrors a ProjectSettings.json annotated by hand or written by a newer engine.
	WriteText(Path, R"({
	"version": 1,
	"name": "HelloTriangle",
	"experimental": { "raytracing": true },
	"window": { "title": "Original", "width": 1600, "height": 900 }
})");

	FJson Root;
	REQUIRE(FJsonUtils::LoadFromFile(Path, Root));
	REQUIRE(FJsonUtils::Set(Root, "window.width", 1280u));
	REQUIRE(FJsonUtils::SaveToFile(Path, Root));

	FJson Reloaded;
	REQUIRE(FJsonUtils::LoadFromFile(Path, Reloaded));
	REQUIRE(FJsonUtils::ReadOr<uint32>(Reloaded, "window.width", 0) == 1280);
	REQUIRE(FJsonUtils::ReadOr<uint32>(Reloaded, "window.height", 0) == 900);
	REQUIRE(FJsonUtils::ReadOr<std::string>(Reloaded, "window.title", "") == "Original");
	REQUIRE(FJsonUtils::ReadOr<bool>(Reloaded, "experimental.raytracing", false) == true);
	REQUIRE(FJsonUtils::ReadOr<std::string>(Reloaded, "name", "") == "HelloTriangle");
}

TEST_CASE("Key order survives a save, so hand authored files stay readable", "[Core][Json][Save]")
{
	const FTempDirectory Temp("JsonKeyOrder");
	const std::filesystem::path Path = Temp.File("settings.json");

	// Alphabetical ordering would scramble this into editor, name, rhi, version, window.
	WriteText(Path, R"({
	"version": 1,
	"name": "Ordered",
	"window": { "title": "T", "width": 100, "height": 200 },
	"rhi": { "backend": "d3d12" },
	"editor": { "enabled": true }
})");

	FJson Root;
	REQUIRE(FJsonUtils::LoadFromFile(Path, Root));
	REQUIRE(FJsonUtils::Set(Root, "window.width", 300u));
	REQUIRE(FJsonUtils::SaveToFile(Path, Root));

	FJson Reloaded;
	REQUIRE(FJsonUtils::LoadFromFile(Path, Reloaded));

	std::vector<std::string> TopLevelKeys;
	for (const auto& Entry : Reloaded.items())
	{
		TopLevelKeys.push_back(Entry.key());
	}
	REQUIRE(TopLevelKeys == std::vector<std::string>{ "version", "name", "window", "rhi", "editor" });

	std::vector<std::string> WindowKeys;
	for (const auto& Entry : Reloaded["window"].items())
	{
		WindowKeys.push_back(Entry.key());
	}
	REQUIRE(WindowKeys == std::vector<std::string>{ "title", "width", "height" });
}

TEST_CASE("A key added by Set lands at the end without disturbing the rest", "[Core][Json][Save]")
{
	FJson Root = FJson::parse(R"({ "version": 1, "name": "Test" })");
	REQUIRE(FJsonUtils::Set(Root, "window.width", 800u));

	std::vector<std::string> Keys;
	for (const auto& Entry : Root.items())
	{
		Keys.push_back(Entry.key());
	}
	REQUIRE(Keys == std::vector<std::string>{ "version", "name", "window" });
}
