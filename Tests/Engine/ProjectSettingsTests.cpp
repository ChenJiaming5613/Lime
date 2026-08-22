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
	Other.bEnableAutomation = !Base.bEnableAutomation;
	REQUIRE(Other != Base);

	Other = Base;
	Other.AutomationPort += 1;
	REQUIRE(Other != Base);

	Other = Base;
	Other.ProjectName = "Renamed";
	REQUIRE(Other != Base);
}

TEST_CASE("Settings survive a JSON round trip", "[Engine][ProjectSettings]")
{
	// ToJson feeds the automation clients, ApplyJson reads their edits back. A field lost between the
	// two would silently revert whatever a script tried to change.
	FProjectSettings Source;
	Source.WindowTitle = "Round Trip";
	Source.WindowWidth = 1280;
	Source.WindowHeight = 720;
	Source.BackBufferCount = 2;
	Source.bVSync = false;
	Source.Validation = EValidationMode::On;
	Source.bPersistPassSettings = true;
	Source.bEnableAutomation = true;
	Source.AutomationPort = 9001;

	FProjectSettings Target;
	std::string Error;
	REQUIRE(Target.ApplyJson(Source.ToJson(), Error));
	REQUIRE(Error.empty());

	REQUIRE(Target.WindowTitle == Source.WindowTitle);
	REQUIRE(Target.WindowWidth == Source.WindowWidth);
	REQUIRE(Target.WindowHeight == Source.WindowHeight);
	REQUIRE(Target.BackBufferCount == Source.BackBufferCount);
	REQUIRE(Target.bVSync == Source.bVSync);
	REQUIRE(Target.Validation == Source.Validation);
	REQUIRE(Target.bPersistPassSettings == Source.bPersistPassSettings);
	REQUIRE(Target.bEnableAutomation == Source.bEnableAutomation);
	REQUIRE(Target.AutomationPort == Source.AutomationPort);
}

TEST_CASE("A partial settings object leaves absent fields alone", "[Engine][ProjectSettings]")
{
	FProjectSettings Settings;
	const uint32 OriginalHeight = Settings.WindowHeight;

	FJson Partial = FJson::object();
	Partial["window"] = FJson::object();
	Partial["window"]["width"] = 1024;

	std::string Error;
	REQUIRE(Settings.ApplyJson(Partial, Error));
	REQUIRE(Settings.WindowWidth == 1024);
	REQUIRE(Settings.WindowHeight == OriginalHeight);
}

TEST_CASE("An invalid settings value is rejected atomically", "[Engine][ProjectSettings]")
{
	// Validation happens on a copy, so a request that names one bad key must not apply the good ones
	// alongside it. Otherwise a rejected call would leave a half applied configuration behind.
	FProjectSettings Settings;
	const FProjectSettings Original = Settings;

	FJson Request = FJson::object();
	Request["window"] = FJson::object();
	Request["window"]["width"] = 1024;
	Request["rhi"] = FJson::object();
	Request["rhi"]["backBufferCount"] = 99;

	std::string Error;
	REQUIRE_FALSE(Settings.ApplyJson(Request, Error));
	REQUIRE_FALSE(Error.empty());
	REQUIRE(Settings == Original);
}

TEST_CASE("Settings type mismatches are reported by key", "[Engine][ProjectSettings]")
{
	FProjectSettings Settings;
	std::string Error;

	FJson Request = FJson::object();
	Request["window"] = FJson::object();
	Request["window"]["width"] = "wide";
	REQUIRE_FALSE(Settings.ApplyJson(Request, Error));
	REQUIRE(Error.find("window.width") != std::string::npos);

	Request = FJson::object();
	Request["rhi"] = FJson::object();
	Request["rhi"]["backend"] = "metal";
	REQUIRE_FALSE(Settings.ApplyJson(Request, Error));
	REQUIRE(Error.find("metal") != std::string::npos);

	// A non-object request is a client bug and must not be silently accepted.
	REQUIRE_FALSE(Settings.ApplyJson(FJson(42), Error));
}

TEST_CASE("Automation command line switches are parsed", "[Engine][ProjectSettings]")
{
	FProjectSettings Settings;

	const char* EnableArguments[] = { "LimeEngine.exe", "--automation-port=9100" };
	Settings.ApplyCommandLine(2, EnableArguments);
	REQUIRE(Settings.AutomationPort == 9100);
	// Naming a port implies the server should run, so it does not have to be enabled separately.
	REQUIRE(Settings.bEnableAutomation);

	// Port 0 is a valid request for an OS assigned port, not a parse failure.
	const char* ZeroArguments[] = { "LimeEngine.exe", "--automation-port=0" };
	Settings.ApplyCommandLine(2, ZeroArguments);
	REQUIRE(Settings.AutomationPort == 0);

	const char* DisableArguments[] = { "LimeEngine.exe", "--no-automation" };
	Settings.ApplyCommandLine(2, DisableArguments);
	REQUIRE_FALSE(Settings.bEnableAutomation);

	// An out of range port is ignored rather than truncated into a different one.
	Settings.AutomationPort = 8787;
	const char* BadArguments[] = { "LimeEngine.exe", "--automation-port=70000" };
	Settings.ApplyCommandLine(2, BadArguments);
	REQUIRE(Settings.AutomationPort == 8787);
}
