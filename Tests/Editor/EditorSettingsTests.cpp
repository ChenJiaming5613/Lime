// Covers the value logic behind the editor settings panel and its file.
//
// Two things matter here. Whatever the panel writes has to parse back to the same value, or saving
// silently changes the appearance. And ApplyJson has to reject bad input without partially applying
// it, since it is reachable from automation where a rejected request should leave the settings as
// they were.

#include "Editor/EditorSettings.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace Lime;

TEST_CASE("Theme tokens round trip", "[Editor][EditorSettings]")
{
	for (const EEditorTheme Theme : { EEditorTheme::Dark, EEditorTheme::Light, EEditorTheme::Classic })
	{
		EEditorTheme Parsed = EEditorTheme::Classic;
		REQUIRE(TryParseEditorTheme(ToString(Theme), Parsed));
		REQUIRE(Parsed == Theme);
	}

	REQUIRE(std::string(ToString(EEditorTheme::Dark)) == "dark");
	REQUIRE(std::string(ToString(EEditorTheme::Light)) == "light");
	REQUIRE(std::string(ToString(EEditorTheme::Classic)) == "classic");
}

TEST_CASE("Unknown theme text is rejected without touching the target", "[Editor][EditorSettings]")
{
	EEditorTheme Theme = EEditorTheme::Light;
	REQUIRE_FALSE(TryParseEditorTheme("neon", Theme));
	REQUIRE(Theme == EEditorTheme::Light);
}

TEST_CASE("Defaults survive a JSON round trip", "[Editor][EditorSettings]")
{
	// ToJson feeds both the file and the automation reply, ApplyJson reads either back.
	const FEditorSettings Original;

	FEditorSettings Restored;
	Restored.FontSize = 31.0f;
	Restored.Theme = EEditorTheme::Classic;
	Restored.AccentColor = { 1.0f, 0.0f, 0.0f };

	std::string Error;
	REQUIRE(Restored.ApplyJson(Original.ToJson(), Error));
	REQUIRE(Error.empty());
	REQUIRE(Restored == Original);
}

TEST_CASE("ApplyJson accepts a partial object", "[Editor][EditorSettings]")
{
	// A client should be able to change one field without restating the rest.
	FEditorSettings Settings;
	const FEditorSettings Before = Settings;

	FJson Patch = FJson::object();
	Patch["appearance"] = FJson::object();
	Patch["appearance"]["theme"] = "light";

	std::string Error;
	REQUIRE(Settings.ApplyJson(Patch, Error));
	REQUIRE(Settings.Theme == EEditorTheme::Light);
	// Untouched keys keep their values.
	REQUIRE(Settings.FontSize == Before.FontSize);
	REQUIRE(Settings.AccentColor == Before.AccentColor);
}

TEST_CASE("ApplyJson rejects bad values without applying any of them", "[Editor][EditorSettings]")
{
	// The rejected key comes after a valid one, so a half applied change would be visible.
	auto RequireRejected = [](const FJson& Appearance) {
		FEditorSettings Settings;
		const FEditorSettings Before = Settings;

		FJson Patch = FJson::object();
		Patch["appearance"] = Appearance;

		std::string Error;
		REQUIRE_FALSE(Settings.ApplyJson(Patch, Error));
		REQUIRE_FALSE(Error.empty());
		REQUIRE(Settings == Before);
	};

	SECTION("font size out of range")
	{
		FJson Appearance = FJson::object();
		Appearance["theme"] = "light";
		Appearance["fontSize"] = FEditorSettings::MaxFontSize + 1.0f;
		RequireRejected(Appearance);
	}

	SECTION("font size of the wrong type")
	{
		FJson Appearance = FJson::object();
		Appearance["fontSize"] = "large";
		RequireRejected(Appearance);
	}

	SECTION("unknown theme")
	{
		FJson Appearance = FJson::object();
		Appearance["theme"] = "neon";
		RequireRejected(Appearance);
	}

	SECTION("accent colour of the wrong length")
	{
		FJson Appearance = FJson::object();
		Appearance["theme"] = "light";
		Appearance["accentColor"] = FJson::array({ 0.1f, 0.2f });
		RequireRejected(Appearance);
	}

	SECTION("accent colour channel out of range")
	{
		FJson Appearance = FJson::object();
		Appearance["accentColor"] = FJson::array({ 0.1f, 1.5f, 0.2f });
		RequireRejected(Appearance);
	}

	SECTION("accent colour channel of the wrong type")
	{
		FJson Appearance = FJson::object();
		Appearance["accentColor"] = FJson::array({ 0.1f, "green", 0.2f });
		RequireRejected(Appearance);
	}
}

TEST_CASE("ApplyJson rejects a non object", "[Editor][EditorSettings]")
{
	FEditorSettings Settings;
	std::string Error;
	REQUIRE_FALSE(Settings.ApplyJson(FJson::array({ 1, 2 }), Error));
	REQUIRE_FALSE(Error.empty());
}

TEST_CASE("Font size bounds are usable as a slider range", "[Editor][EditorSettings]")
{
	// The panel clamps to these, so a value the panel can produce must always load back unchanged.
	REQUIRE(FEditorSettings::MinFontSize < FEditorSettings::MaxFontSize);
	REQUIRE(FEditorSettings::DefaultFontSize >= FEditorSettings::MinFontSize);
	REQUIRE(FEditorSettings::DefaultFontSize <= FEditorSettings::MaxFontSize);
}
