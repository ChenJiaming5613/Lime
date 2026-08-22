// Covers the reflection layer that both the inspector and the settings serialization depend on.
//
// The critical property is that fields are registered with entt::as_ref_t: with the default
// as-value policy the widgets and the loader would write into a temporary copy, which is a silent
// failure rather than a compile error.

#include "Core/Math/Vector.h"
#include "Core/Reflection/JsonArchive.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace ReflectionTests
{
	struct FSampleSettings
	{
		bool bFlag = false;
		Lime::int32 IntValue = 1;
		Lime::uint32 UIntValue = 2;
		float FloatValue = 3.5f;
		Lime::FVector3 Direction{ 1.0f, 0.0f, 0.0f };
		Lime::FVector4 Color{ 0.1f, 0.2f, 0.3f, 1.0f };
		std::string Label = "initial";
		float TransientValue = 0.0f;
	};
} // namespace ReflectionTests

LIME_REFLECT(ReflectionTests::FSampleSettings)
{
	LIME_PROPERTY(bFlag, Lime::FProp("Flag"));
	LIME_PROPERTY(IntValue, Lime::FProp("Int").Range(0.0f, 10.0f));
	LIME_PROPERTY(UIntValue, Lime::FProp("UInt"));
	LIME_PROPERTY(FloatValue, Lime::FProp("Float").Range(-1.0f, 1.0f).Tooltip("A float"));
	LIME_PROPERTY(Direction, Lime::FProp("Direction"));
	LIME_PROPERTY(Color, Lime::FProp("Color").AsColor());
	LIME_PROPERTY(Label, Lime::FProp("Label"));
	LIME_PROPERTY(TransientValue, Lime::FProp("Transient").Transient());
}

using namespace Lime;
using ReflectionTests::FSampleSettings;

TEST_CASE("Reflected types resolve and expose every declared field", "[Core][Reflection]")
{
	const entt::meta_type Type = Reflect<FSampleSettings>();
	REQUIRE(static_cast<bool>(Type));

	int32 FieldCount = 0;
	bool bFoundFloat = false;
	for (auto&& [Id, Data] : Type.data())
	{
		++FieldCount;
		if (Data.name() != nullptr && std::string(Data.name()) == "FloatValue")
		{
			bFoundFloat = true;
		}
	}

	REQUIRE(FieldCount == 8);
	REQUIRE(bFoundFloat);
}

TEST_CASE("Property metadata survives registration", "[Core][Reflection]")
{
	const entt::meta_type Type = Reflect<FSampleSettings>();

	for (auto&& [Id, Data] : Type.data())
	{
		const FPropertyMeta* Meta = GetPropertyMeta(Data);
		REQUIRE(Meta != nullptr);
		REQUIRE(Meta->DisplayName != nullptr);

		const std::string Name = Data.name();
		if (Name == "FloatValue")
		{
			REQUIRE(std::string(Meta->DisplayName) == "Float");
			REQUIRE(Meta->HasRange());
			REQUIRE(Meta->MinValue == -1.0f);
			REQUIRE(Meta->MaxValue == 1.0f);
			REQUIRE(Meta->Widget == EPropertyWidget::Slider);
			REQUIRE(std::string(Meta->Tooltip) == "A float");
		}
		else if (Name == "Color")
		{
			REQUIRE(Meta->Widget == EPropertyWidget::Color);
		}
		else if (Name == "TransientValue")
		{
			REQUIRE(Meta->bTransient);
		}
		else if (Name == "UIntValue")
		{
			REQUIRE_FALSE(Meta->HasRange());
			REQUIRE(Meta->Widget == EPropertyWidget::Automatic);
		}
	}
}

TEST_CASE("as_ref_t registration yields writable pointers into the instance", "[Core][Reflection]")
{
	// This is the property the inspector relies on: without as_ref_t the widget would edit a copy.
	FSampleSettings Settings;
	const entt::meta_type Type = Reflect<FSampleSettings>();
	entt::meta_any Owner = Type.from_void(&Settings);

	for (auto&& [Id, Data] : Type.data())
	{
		const std::string Name = Data.name();
		entt::meta_any Value = Data.get(Owner);

		if (Name == "FloatValue")
		{
			float* Pointer = Value.try_cast<float>();
			REQUIRE(Pointer != nullptr);
			REQUIRE(Pointer == &Settings.FloatValue);
			*Pointer = 42.0f;
		}
		else if (Name == "bFlag")
		{
			bool* Pointer = Value.try_cast<bool>();
			REQUIRE(Pointer != nullptr);
			*Pointer = true;
		}
	}

	REQUIRE(Settings.FloatValue == 42.0f);
	REQUIRE(Settings.bFlag);
}

TEST_CASE("Save and load round trip every supported type", "[Core][Reflection][Json]")
{
	FSampleSettings Source;
	Source.bFlag = true;
	Source.IntValue = -7;
	Source.UIntValue = 9;
	Source.FloatValue = 0.25f;
	Source.Direction = { 0.0f, 1.0f, 0.0f };
	Source.Color = { 0.5f, 0.6f, 0.7f, 0.8f };
	Source.Label = "saved";

	FJson Json;
	FJsonArchive::Save(Source, Json);

	FSampleSettings Restored;
	FJsonArchive::Load(Json, Restored);

	REQUIRE(Restored.bFlag == Source.bFlag);
	REQUIRE(Restored.IntValue == Source.IntValue);
	REQUIRE(Restored.UIntValue == Source.UIntValue);
	REQUIRE(Restored.FloatValue == Source.FloatValue);
	REQUIRE(Restored.Direction == Source.Direction);
	REQUIRE(Restored.Color == Source.Color);
	REQUIRE(Restored.Label == Source.Label);
}

TEST_CASE("Transient fields are skipped by serialization", "[Core][Reflection][Json]")
{
	FSampleSettings Source;
	Source.TransientValue = 123.0f;

	FJson Json;
	FJsonArchive::Save(Source, Json);
	REQUIRE(Json.find("TransientValue") == Json.end());

	// A transient field keeps whatever the constructor set, even if the file happens to name it.
	Json["TransientValue"] = 999.0f;
	FSampleSettings Restored;
	FJsonArchive::Load(Json, Restored);
	REQUIRE(Restored.TransientValue == 0.0f);
}

TEST_CASE("Vectors are stored as arrays", "[Core][Reflection][Json]")
{
	FSampleSettings Source;
	Source.Direction = { 1.0f, 2.0f, 3.0f };

	FJson Json;
	FJsonArchive::Save(Source, Json);

	REQUIRE(Json["Direction"].is_array());
	REQUIRE(Json["Direction"].size() == 3);
	REQUIRE(Json["Direction"][1].get<float>() == 2.0f);
	REQUIRE(Json["Color"].size() == 4);
}

TEST_CASE("Loading a partial or malformed object leaves the rest untouched", "[Core][Reflection][Json]")
{
	FSampleSettings Settings;
	Settings.FloatValue = 5.0f;
	Settings.Label = "keep";

	SECTION("Absent fields are left alone")
	{
		const FJson Partial = FJson::parse(R"({ "IntValue": 11 })");
		FJsonArchive::Load(Partial, Settings);

		REQUIRE(Settings.IntValue == 11);
		REQUIRE(Settings.FloatValue == 5.0f);
		REQUIRE(Settings.Label == "keep");
	}

	SECTION("Wrong types are rejected without corrupting the value")
	{
		const FJson Malformed = FJson::parse(R"({ "FloatValue": "not a number", "Label": 42 })");
		FJsonArchive::Load(Malformed, Settings);

		REQUIRE(Settings.FloatValue == 5.0f);
		REQUIRE(Settings.Label == "keep");
	}

	SECTION("A vector of the wrong length is rejected")
	{
		const FJson Malformed = FJson::parse(R"({ "Direction": [1.0, 2.0] })");
		FJsonArchive::Load(Malformed, Settings);

		REQUIRE(Settings.Direction == FVector3(1.0f, 0.0f, 0.0f));
	}

	SECTION("A non-object is ignored entirely")
	{
		const FJson NotAnObject = FJson::parse("[1, 2, 3]");
		FJsonArchive::Load(NotAnObject, Settings);

		REQUIRE(Settings.FloatValue == 5.0f);
	}
}
