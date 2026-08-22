#include "Automation/AutomationReflection.h"

#include "Core/Reflection/JsonArchive.h"

#include <spdlog/fmt/fmt.h>

namespace Lime
{
	namespace
	{
		const char* ToString(EPropertyWidget Widget)
		{
			switch (Widget)
			{
				case EPropertyWidget::Slider:
					return "slider";
				case EPropertyWidget::Drag:
					return "drag";
				case EPropertyWidget::Color:
					return "color";
				case EPropertyWidget::CheckBox:
					return "checkBox";
				case EPropertyWidget::Automatic:
					return "automatic";
			}
			return "automatic";
		}

		// Resolves a field by its declared name. entt indexes fields by hashed id, and the readable
		// name lives in the data itself, so a linear scan is the way to look one up.
		entt::meta_data FindField(const entt::meta_type& MetaType, const std::string& FieldName)
		{
			for (auto&& [Id, Data] : MetaType.data())
			{
				const char* Name = Data.name();
				if (Name != nullptr && FieldName == Name)
				{
					return Data;
				}
			}
			return {};
		}
	} // namespace

	FJson FAutomationReflection::DescribeFields(const FReflectedRef& Ref)
	{
		FJson Fields = FJson::array();
		if (!Ref.IsValid())
		{
			return Fields;
		}

		entt::meta_any Owner = Ref.Type.from_void(Ref.Instance);

		for (auto&& [Id, Data] : Ref.Type.data())
		{
			const char* Name = Data.name();
			if (Name == nullptr)
			{
				continue;
			}

			entt::meta_any Value = Data.get(Owner);

			FJson Field = FJson::object();
			Field["name"] = Name;
			Field["type"] = FJsonArchive::GetValueTypeName(Value);

			FJson ValueJson;
			if (FJsonArchive::SaveValue(Value, ValueJson))
			{
				Field["value"] = std::move(ValueJson);
			}
			else
			{
				Field["value"] = nullptr;
			}

			// The metadata is what makes a field discoverable: a script can read the range instead of
			// hardcoding limits that would silently drift from the engine.
			if (const FPropertyMeta* Meta = GetPropertyMeta(Data))
			{
				if (Meta->DisplayName != nullptr)
				{
					Field["displayName"] = Meta->DisplayName;
				}
				if (Meta->Tooltip != nullptr)
				{
					Field["tooltip"] = Meta->Tooltip;
				}
				if (Meta->HasRange())
				{
					Field["min"] = Meta->MinValue;
					Field["max"] = Meta->MaxValue;
				}
				Field["widget"] = ToString(Meta->Widget);
				Field["readOnly"] = Meta->bReadOnly;
				Field["transient"] = Meta->bTransient;
			}

			Fields.push_back(std::move(Field));
		}

		return Fields;
	}

	FJson FAutomationReflection::ReadValues(const FReflectedRef& Ref)
	{
		FJson Values = FJson::object();
		if (!Ref.IsValid())
		{
			return Values;
		}

		entt::meta_any Owner = Ref.Type.from_void(Ref.Instance);

		for (auto&& [Id, Data] : Ref.Type.data())
		{
			const char* Name = Data.name();
			if (Name == nullptr)
			{
				continue;
			}

			entt::meta_any Value = Data.get(Owner);
			FJson ValueJson;
			if (FJsonArchive::SaveValue(Value, ValueJson))
			{
				Values[Name] = std::move(ValueJson);
			}
		}

		return Values;
	}

	bool FAutomationReflection::WriteField(const FReflectedRef& Ref, const std::string& FieldName, const FJson& Value,
	                                       std::string& OutError)
	{
		if (!Ref.IsValid())
		{
			OutError = "The object exposes no reflection";
			return false;
		}

		const entt::meta_data Data = FindField(Ref.Type, FieldName);
		if (!Data)
		{
			OutError = fmt::format("Unknown field '{}'", FieldName);
			return false;
		}

		// Read only fields are excluded deliberately: they are the ones the owning code derives, and
		// writing them would be overwritten on the next frame anyway.
		if (const FPropertyMeta* Meta = GetPropertyMeta(Data); Meta != nullptr && Meta->bReadOnly)
		{
			OutError = fmt::format("Field '{}' is read only", FieldName);
			return false;
		}

		entt::meta_any Owner = Ref.Type.from_void(Ref.Instance);
		// as_ref_t registration means this aliases the real field, so the write lands on the instance.
		entt::meta_any Target = Data.get(Owner);

		if (!FJsonArchive::LoadValue(Target, Value))
		{
			OutError = fmt::format("Value does not fit field '{}' of type {}", FieldName, FJsonArchive::GetValueTypeName(Target));
			return false;
		}

		return true;
	}

	bool FAutomationReflection::WriteFields(const FReflectedRef& Ref, const FJson& Values, std::vector<std::string>& OutApplied,
	                                        std::string& OutError)
	{
		if (!Values.is_object())
		{
			OutError = "Values must be an object";
			return false;
		}

		for (const auto& [Name, Value] : Values.items())
		{
			if (!WriteField(Ref, Name, Value, OutError))
			{
				return false;
			}
			OutApplied.push_back(Name);
		}

		return true;
	}
} // namespace Lime
