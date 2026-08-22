// Covers the JSON helpers that ProjectSettings relies on. The contract is that a malformed file
// degrades to defaults instead of preventing startup, so the failure paths matter most.

#include "Core/Json/JsonUtils.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace Lime;

namespace
{
	FJson MakeSampleJson()
	{
		return FJson::parse(R"({
			"version": 1,
			"name": "Sample",
			"window": { "width": 1280, "height": 720, "title": "Hello" },
			"rhi": { "backend": "vulkan", "vsync": false, "backBufferCount": 2 },
			"nested": { "deep": { "value": 7 } }
		})");
	}
} // namespace

TEST_CASE("Dotted lookup walks nested objects", "[Core][Json]")
{
	const FJson Root = MakeSampleJson();

	REQUIRE(FJsonUtils::Find(Root, "name") != nullptr);
	REQUIRE(FJsonUtils::Find(Root, "window.width") != nullptr);
	REQUIRE(FJsonUtils::Find(Root, "nested.deep.value") != nullptr);

	SECTION("Absent paths return nullptr rather than throwing")
	{
		REQUIRE(FJsonUtils::Find(Root, "missing") == nullptr);
		REQUIRE(FJsonUtils::Find(Root, "window.missing") == nullptr);
		REQUIRE(FJsonUtils::Find(Root, "name.width") == nullptr);
		REQUIRE(FJsonUtils::Find(Root, "") == nullptr);
		REQUIRE(FJsonUtils::Find(Root, "window..width") == nullptr);
	}
}

TEST_CASE("Values are read with the expected types", "[Core][Json]")
{
	const FJson Root = MakeSampleJson();

	REQUIRE(FJsonUtils::ReadOr<std::string>(Root, "name", "fallback") == "Sample");
	REQUIRE(FJsonUtils::ReadOr<uint32>(Root, "window.width", 800) == 1280);
	REQUIRE(FJsonUtils::ReadOr<uint32>(Root, "window.height", 600) == 720);
	REQUIRE(FJsonUtils::ReadOr<bool>(Root, "rhi.vsync", true) == false);
	REQUIRE(FJsonUtils::ReadOr<int32>(Root, "version", 0) == 1);
	REQUIRE(FJsonUtils::ReadOr<uint32>(Root, "nested.deep.value", 0) == 7);
}

TEST_CASE("Missing keys fall back to the default", "[Core][Json]")
{
	const FJson Root = MakeSampleJson();

	REQUIRE(FJsonUtils::ReadOr<uint32>(Root, "window.depth", 42) == 42);
	REQUIRE(FJsonUtils::ReadOr<std::string>(Root, "absent", "fallback") == "fallback");
	REQUIRE(FJsonUtils::ReadOr<bool>(Root, "rhi.absent", true) == true);
}

TEST_CASE("Type mismatches fall back to the default", "[Core][Json]")
{
	const FJson Root = MakeSampleJson();

	// A string where a number is expected, and the reverse.
	REQUIRE(FJsonUtils::ReadOr<uint32>(Root, "name", 99) == 99);
	REQUIRE(FJsonUtils::ReadOr<std::string>(Root, "version", "fallback") == "fallback");
	REQUIRE(FJsonUtils::ReadOr<bool>(Root, "window.width", true) == true);

	SECTION("An object or array is not silently accepted as a scalar")
	{
		REQUIRE(FJsonUtils::ReadOr<uint32>(Root, "window", 5) == 5);
	}
}

TEST_CASE("Unsigned reads reject negative numbers", "[Core][Json]")
{
	const FJson Root = FJson::parse(R"({ "width": -100, "count": 0 })");

	REQUIRE(FJsonUtils::ReadOr<uint32>(Root, "width", 800) == 800);
	// Zero is a valid unsigned value; range checks belong to the caller.
	REQUIRE(FJsonUtils::ReadOr<uint32>(Root, "count", 3) == 0);
	REQUIRE(FJsonUtils::ReadOr<int32>(Root, "width", 0) == -100);
}

TEST_CASE("Floats accept integer literals", "[Core][Json]")
{
	const FJson Root = FJson::parse(R"({ "exact": 2, "fractional": 2.5 })");

	REQUIRE(FJsonUtils::ReadOr<float>(Root, "exact", 0.0f) == 2.0f);
	REQUIRE(FJsonUtils::ReadOr<float>(Root, "fractional", 0.0f) == 2.5f);
}
