// Covers the value mapping the project settings editor depends on: whatever the panel writes has to
// parse back to the same value, otherwise saving silently changes the configuration.

#include "Engine/ProjectSettings.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace Lime;

TEST_CASE("Backend config tokens round trip", "[Engine][ProjectSettings]")
{
	// ToConfigToken feeds the JSON, TryParseBackend reads it back on the next launch.
	for (const ERHIBackend Backend : { ERHIBackend::D3D12, ERHIBackend::Vulkan })
	{
		ERHIBackend Parsed = Backend == ERHIBackend::D3D12 ? ERHIBackend::Vulkan : ERHIBackend::D3D12;
		REQUIRE(TryParseBackend(ToConfigToken(Backend), Parsed));
		REQUIRE(Parsed == Backend);
	}

	// The token is the lower case form; the display name is separate and not written to the file.
	REQUIRE(std::string(ToConfigToken(ERHIBackend::D3D12)) == "d3d12");
	REQUIRE(std::string(ToConfigToken(ERHIBackend::Vulkan)) == "vulkan");
}

TEST_CASE("Validation mode strings round trip", "[Engine][ProjectSettings]")
{
	for (const EValidationMode Mode : { EValidationMode::Off, EValidationMode::DebugOnly, EValidationMode::On })
	{
		EValidationMode Parsed = EValidationMode::Off;
		REQUIRE(TryParseValidation(ToString(Mode), Parsed));
		REQUIRE(Parsed == Mode);
	}

	REQUIRE(std::string(ToString(EValidationMode::Off)) == "off");
	REQUIRE(std::string(ToString(EValidationMode::DebugOnly)) == "debugOnly");
	REQUIRE(std::string(ToString(EValidationMode::On)) == "on");
}

TEST_CASE("Unknown validation text is rejected without touching the target", "[Engine][ProjectSettings]")
{
	EValidationMode Mode = EValidationMode::DebugOnly;
	REQUIRE_FALSE(TryParseValidation("sometimes", Mode));
	REQUIRE(Mode == EValidationMode::DebugOnly);
}

TEST_CASE("Validation modes resolve against the build configuration", "[Engine][ProjectSettings]")
{
	FProjectSettings Settings;

	Settings.Validation = EValidationMode::Off;
	REQUIRE_FALSE(Settings.IsValidationEnabled());

	Settings.Validation = EValidationMode::On;
	REQUIRE(Settings.IsValidationEnabled());

	// The default follows the build, which is what makes it usable as a shipped value.
	Settings.Validation = EValidationMode::DebugOnly;
	REQUIRE(Settings.IsValidationEnabled() == (LIME_DEBUG != 0));
}

TEST_CASE("Equality covers every persisted field", "[Engine][ProjectSettings]")
{
	// The editor uses this to decide whether there are unsaved changes, so a field missing from the
	// comparison would make its edits look already saved.
	const FProjectSettings Base;

	FProjectSettings Other = Base;
	REQUIRE(Other == Base);

	Other = Base;
	Other.WindowTitle = "Changed";
	REQUIRE(Other != Base);

	Other = Base;
	Other.WindowWidth += 1;
	REQUIRE(Other != Base);

	Other = Base;
	Other.WindowHeight += 1;
	REQUIRE(Other != Base);

	Other = Base;
	Other.Backend = Base.Backend == ERHIBackend::D3D12 ? ERHIBackend::Vulkan : ERHIBackend::D3D12;
	REQUIRE(Other != Base);

	Other = Base;
	Other.BackBufferCount += 1;
	REQUIRE(Other != Base);

	Other = Base;
	Other.bVSync = !Base.bVSync;
	REQUIRE(Other != Base);

	Other = Base;
	Other.Validation = Base.Validation == EValidationMode::On ? EValidationMode::Off : EValidationMode::On;
	REQUIRE(Other != Base);

	Other = Base;
	Other.bEnableEditor = !Base.bEnableEditor;
	REQUIRE(Other != Base);

	Other = Base;
	Other.bPersistPassSettings = !Base.bPersistPassSettings;
	REQUIRE(Other != Base);

	Other = Base;
	Other.ProjectName = "Renamed";
	REQUIRE(Other != Base);
}
