// Metadata attached to reflected fields. Drives both the inspector UI and JSON serialization.

#pragma once

#include "Core/CoreTypes.h"

namespace Lime
{
	enum class EPropertyWidget : uint8
	{
		// Chosen from the field type when no explicit widget is requested.
		Automatic = 0,
		Slider,
		Drag,
		Color,
		CheckBox
	};

	// Built once per field at registration time and stored via entt's custom() slot. entt keeps a
	// shared_ptr to it, so the struct owns no lifetime concerns beyond string literals.
	struct FPropertyMeta
	{
		// Must be a string literal: entt stores the pointer without copying.
		const char* DisplayName = nullptr;
		const char* Tooltip = nullptr;
		float MinValue = 0.0f;
		float MaxValue = 0.0f;
		EPropertyWidget Widget = EPropertyWidget::Automatic;
		bool bReadOnly = false;
		// Excluded from serialization, for transient state such as an accumulated angle.
		bool bTransient = false;

		bool HasRange() const { return MinValue < MaxValue; }
	};

	// Fluent builders so registration reads as data rather than as positional arguments.
	struct FPropertyMetaBuilder
	{
		FPropertyMeta Meta;

		FPropertyMetaBuilder(const char* InDisplayName) { Meta.DisplayName = InDisplayName; }

		FPropertyMetaBuilder& Range(float InMin, float InMax)
		{
			Meta.MinValue = InMin;
			Meta.MaxValue = InMax;
			if (Meta.Widget == EPropertyWidget::Automatic)
			{
				Meta.Widget = EPropertyWidget::Slider;
			}
			return *this;
		}

		FPropertyMetaBuilder& AsColor()
		{
			Meta.Widget = EPropertyWidget::Color;
			return *this;
		}

		FPropertyMetaBuilder& AsDrag(float InSpeedMin = 0.0f, float InSpeedMax = 0.0f)
		{
			Meta.Widget = EPropertyWidget::Drag;
			Meta.MinValue = InSpeedMin;
			Meta.MaxValue = InSpeedMax;
			return *this;
		}

		FPropertyMetaBuilder& Tooltip(const char* InTooltip)
		{
			Meta.Tooltip = InTooltip;
			return *this;
		}

		FPropertyMetaBuilder& ReadOnly()
		{
			Meta.bReadOnly = true;
			return *this;
		}

		FPropertyMetaBuilder& Transient()
		{
			Meta.bTransient = true;
			return *this;
		}

		operator FPropertyMeta() const { return Meta; }
	};
} // namespace Lime
