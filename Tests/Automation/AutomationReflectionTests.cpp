// Covers the reflection to JSON bridge the automation commands rely on. A field that cannot be
// described or written here is one a script cannot reach, so this is tested without a GPU.

#include "Automation/AutomationReflection.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
	struct FTestSettings
	{
		bool bFlag = false;
		float Scalar = 1.0f;
		// Qualified: the using directive for Lime comes after this declaration.
		Lime::int32 ItemCount = 3;
		Lime::FVector4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
		float Derived = 0.5f;
		float Accumulated = 0.0f;
	};
} // namespace

LIME_REFLECT(FTestSettings)
{
	LIME_PROPERTY(bFlag, Lime::FProp("Flag"));
	LIME_PROPERTY(Scalar, Lime::FProp("Scalar").Range(0.0f, 10.0f).Tooltip("Units per second"));
	LIME_PROPERTY(ItemCount, Lime::FProp("Item count"));
	LIME_PROPERTY(Color, Lime::FProp("Color").AsColor());
	LIME_PROPERTY(Derived, Lime::FProp("Derived").ReadOnly());
	LIME_PROPERTY(Accumulated, Lime::FProp("Accumulated").Transient());
}

using namespace Lime;

namespace
{
	// Locates one described field by name; the description is an array, not an object.
	const FJson* FindField(const FJson& Fields, const char* Name)
	{
		for (const FJson& Field : Fields)
		{
			if (Field.contains("name") && Field["name"] == Name)
			{
				return &Field;
			}
		}
		return nullptr;
	}
} // namespace

TEST_CASE("Describing fields exposes names, values and metadata", "[Automation][Reflection]")
{
	FTestSettings Settings;
	const FJson Fields = FAutomationReflection::DescribeFields(MakeReflectedRef(Settings));

	REQUIRE(Fields.is_array());
	REQUIRE(Fields.size() == 6);

	const FJson* Scalar = FindField(Fields, "Scalar");
	REQUIRE(Scalar != nullptr);
	REQUIRE((*Scalar)["type"] == "float");
	REQUIRE((*Scalar)["value"].get<float>() == 1.0f);
	// The range is what lets a client stay inside valid values without hardcoding them.
	REQUIRE((*Scalar)["min"].get<float>() == 0.0f);
	REQUIRE((*Scalar)["max"].get<float>() == 10.0f);
	REQUIRE((*Scalar)["widget"] == "slider");
	REQUIRE((*Scalar)["displayName"] == "Scalar");
	REQUIRE((*Scalar)["tooltip"] == "Units per second");

	const FJson* Color = FindField(Fields, "Color");
	REQUIRE(Color != nullptr);
	REQUIRE((*Color)["type"] == "vector4");
	REQUIRE((*Color)["widget"] == "color");
	REQUIRE((*Color)["value"].is_array());
	REQUIRE((*Color)["value"].size() == 4);

	// Read only and transient have to be visible, otherwise a client cannot tell why a write fails.
	REQUIRE((*FindField(Fields, "Derived"))["readOnly"] == true);
	REQUIRE((*FindField(Fields, "Accumulated"))["transient"] == true);
}

TEST_CASE("Reading values yields every supported field", "[Automation][Reflection]")
{
	FTestSettings Settings;
	Settings.bFlag = true;
	Settings.Scalar = 2.5f;
	Settings.ItemCount = 7;

	const FJson Values = FAutomationReflection::ReadValues(MakeReflectedRef(Settings));

	REQUIRE(Values.is_object());
	REQUIRE(Values["bFlag"].get<bool>() == true);
	REQUIRE(Values["Scalar"].get<float>() == 2.5f);
	REQUIRE(Values["ItemCount"].get<int32>() == 7);
	// Transient fields are excluded from files but still readable, since they are useful to observe.
	REQUIRE(Values.contains("Accumulated"));
}

TEST_CASE("Writing a field mutates the instance", "[Automation][Reflection]")
{
	FTestSettings Settings;
	const FReflectedRef Ref = MakeReflectedRef(Settings);
	std::string Error;

	REQUIRE(FAutomationReflection::WriteField(Ref, "bFlag", FJson(true), Error));
	REQUIRE(Settings.bFlag == true);

	REQUIRE(FAutomationReflection::WriteField(Ref, "Scalar", FJson(4.25f), Error));
	REQUIRE(Settings.Scalar == 4.25f);

	REQUIRE(FAutomationReflection::WriteField(Ref, "ItemCount", FJson(11), Error));
	REQUIRE(Settings.ItemCount == 11);

	REQUIRE(FAutomationReflection::WriteField(Ref, "Color", FJson::array({ 0.1f, 0.2f, 0.3f, 0.4f }), Error));
	REQUIRE(Settings.Color.X == 0.1f);
	REQUIRE(Settings.Color.W == 0.4f);
}

TEST_CASE("Writing rejects unknown, read only and mistyped fields", "[Automation][Reflection]")
{
	FTestSettings Settings;
	const FReflectedRef Ref = MakeReflectedRef(Settings);
	std::string Error;

	REQUIRE_FALSE(FAutomationReflection::WriteField(Ref, "Missing", FJson(1.0f), Error));
	REQUIRE(Error.find("Unknown field") != std::string::npos);

	// Read only fields are derived by the owning code, so a write would be overwritten anyway.
	REQUIRE_FALSE(FAutomationReflection::WriteField(Ref, "Derived", FJson(1.0f), Error));
	REQUIRE(Error.find("read only") != std::string::npos);
	REQUIRE(Settings.Derived == 0.5f);

	REQUIRE_FALSE(FAutomationReflection::WriteField(Ref, "Scalar", FJson("fast"), Error));
	REQUIRE(Error.find("does not fit") != std::string::npos);
	REQUIRE(Settings.Scalar == 1.0f);

	// A vector of the wrong length must not be applied component by component.
	REQUIRE_FALSE(FAutomationReflection::WriteField(Ref, "Color", FJson::array({ 0.5f, 0.5f }), Error));
	REQUIRE(Settings.Color.X == 1.0f);
}

TEST_CASE("Writing several fields reports what was applied", "[Automation][Reflection]")
{
	FTestSettings Settings;
	const FReflectedRef Ref = MakeReflectedRef(Settings);

	FJson Values = FJson::object();
	Values["bFlag"] = true;
	Values["Scalar"] = 3.0f;

	std::vector<std::string> Applied;
	std::string Error;
	REQUIRE(FAutomationReflection::WriteFields(Ref, Values, Applied, Error));
	REQUIRE(Applied.size() == 2);
	REQUIRE(Settings.bFlag == true);
	REQUIRE(Settings.Scalar == 3.0f);

	// On failure the names already written are still reported, so the caller can tell the engine is
	// in a partially updated state rather than untouched.
	FJson Mixed = FJson::object();
	Mixed["ItemCount"] = 5;
	Mixed["Missing"] = 1;

	Applied.clear();
	REQUIRE_FALSE(FAutomationReflection::WriteFields(Ref, Mixed, Applied, Error));
	REQUIRE(Applied.size() == 1);
	REQUIRE(Applied[0] == "ItemCount");
	REQUIRE(Settings.ItemCount == 5);
}

TEST_CASE("An invalid reflected reference is handled", "[Automation][Reflection]")
{
	// Passes without reflected settings return an empty ref, which must not be treated as an error.
	const FReflectedRef Empty;
	REQUIRE(FAutomationReflection::DescribeFields(Empty).empty());
	REQUIRE(FAutomationReflection::ReadValues(Empty).empty());

	std::string Error;
	REQUIRE_FALSE(FAutomationReflection::WriteField(Empty, "Anything", FJson(1), Error));
	REQUIRE_FALSE(Error.empty());
}
