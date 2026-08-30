#include "Editor/PropertyDrawer.h"

#include "Core/Math/Vector.h"

#include <imgui.h>
#include <imgui_stdlib.h>

namespace Lime
{
	namespace
	{
		// Falls back to the field name when no display name was provided.
		const char* ResolveLabel(const char* FieldName, const FPropertyMeta* Meta)
		{
			if (Meta != nullptr && Meta->DisplayName != nullptr)
			{
				return Meta->DisplayName;
			}
			return FieldName != nullptr ? FieldName : "<unnamed>";
		}

		void ApplyTooltip(const FPropertyMeta* Meta)
		{
			if (Meta != nullptr && Meta->Tooltip != nullptr && ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", Meta->Tooltip);
			}
		}

		bool DrawFloat(const char* Label, const FPropertyMeta* Meta, float& Value)
		{
			if (Meta != nullptr && Meta->Widget == EPropertyWidget::Drag)
			{
				return ImGui::DragFloat(Label, &Value, 0.05f, Meta->MinValue, Meta->MaxValue);
			}
			if (Meta != nullptr && Meta->HasRange())
			{
				return ImGui::SliderFloat(Label, &Value, Meta->MinValue, Meta->MaxValue);
			}
			return ImGui::DragFloat(Label, &Value, 0.05f);
		}

		template<int32 ComponentCount>
		bool DrawFloatVector(const char* Label, const FPropertyMeta* Meta, float* Components)
		{
			const bool bAsColor = Meta != nullptr && Meta->Widget == EPropertyWidget::Color;
			if (bAsColor)
			{
				if constexpr (ComponentCount == 4)
				{
					return ImGui::ColorEdit4(Label, Components, ImGuiColorEditFlags_NoInputs);
				}
				else if constexpr (ComponentCount == 3)
				{
					return ImGui::ColorEdit3(Label, Components, ImGuiColorEditFlags_NoInputs);
				}
			}

			if (Meta != nullptr && Meta->HasRange())
			{
				return ImGui::SliderScalarN(Label, ImGuiDataType_Float, Components, ComponentCount, &Meta->MinValue, &Meta->MaxValue);
			}
			return ImGui::DragScalarN(Label, ImGuiDataType_Float, Components, ComponentCount, 0.05f);
		}
	} // namespace

	bool FPropertyDrawer::Draw(const FReflectedRef& Settings)
	{
		if (!Settings.IsValid())
		{
			return false;
		}
		return DrawFields(Settings.Type, Settings.Instance);
	}

	bool FPropertyDrawer::DrawFields(const entt::meta_type& MetaType, void* Instance)
	{
		if (!MetaType || Instance == nullptr)
		{
			return false;
		}

		// from_void produces the reference wrapper meta_data::get needs as its instance.
		entt::meta_any Owner = MetaType.from_void(Instance);

		// Labels sit to the right of the widget in ImGui and would otherwise be clipped in a narrow
		// docked panel, so the widget is capped at a fraction of the available width.
		const float AvailableWidth = ImGui::GetContentRegionAvail().x;
		ImGui::PushItemWidth(AvailableWidth * 0.5f);

		bool bChanged = false;
		for (auto&& [Id, Data] : MetaType.data())
		{
			bChanged |= DrawField(Data, Owner);
		}

		ImGui::PopItemWidth();
		return bChanged;
	}

	bool FPropertyDrawer::DrawField(const entt::meta_data& Data, entt::meta_any& Owner)
	{
		const char* FieldName = Data.name();
		const FPropertyMeta* Meta = GetPropertyMeta(Data);
		const char* Label = ResolveLabel(FieldName, Meta);

		// as_ref_t registration makes this an alias, so the widget writes into the real field.
		entt::meta_any Value = Data.get(Owner);
		if (!Value)
		{
			return false;
		}

		const bool bReadOnly = Meta != nullptr && Meta->bReadOnly;
		if (bReadOnly)
		{
			ImGui::BeginDisabled();
		}

		const bool bChanged = DrawValue(Label, Meta, Value);
		ApplyTooltip(Meta);

		if (bReadOnly)
		{
			ImGui::EndDisabled();
		}

		return bChanged && !bReadOnly;
	}

	bool FPropertyDrawer::DrawValue(const char* Label, const FPropertyMeta* Meta, entt::meta_any& Value)
	{
		if (bool* BoolValue = Value.try_cast<bool>())
		{
			return ImGui::Checkbox(Label, BoolValue);
		}
		if (float* FloatValue = Value.try_cast<float>())
		{
			return DrawFloat(Label, Meta, *FloatValue);
		}
		if (int32* IntValue = Value.try_cast<int32>())
		{
			if (Meta != nullptr && Meta->HasRange())
			{
				return ImGui::SliderInt(Label, IntValue, static_cast<int32>(Meta->MinValue), static_cast<int32>(Meta->MaxValue));
			}
			return ImGui::DragInt(Label, IntValue);
		}
		if (uint32* UIntValue = Value.try_cast<uint32>())
		{
			return ImGui::DragScalar(Label, ImGuiDataType_U32, UIntValue, 1.0f);
		}
		if (FVector4* Vector4 = Value.try_cast<FVector4>())
		{
			return DrawFloatVector<4>(Label, Meta, &Vector4->X);
		}
		if (FVector3* Vector3 = Value.try_cast<FVector3>())
		{
			return DrawFloatVector<3>(Label, Meta, &Vector3->X);
		}
		if (FVector2* Vector2 = Value.try_cast<FVector2>())
		{
			return DrawFloatVector<2>(Label, Meta, &Vector2->X);
		}
		if (std::string* StringValue = Value.try_cast<std::string>())
		{
			// Resizes through ImGui's callback so a path can be pasted without a fixed buffer to overflow.
			return ImGui::InputText(Label, StringValue);
		}

		ImGui::TextDisabled("%s: unsupported type", Label);
		return false;
	}
} // namespace Lime
