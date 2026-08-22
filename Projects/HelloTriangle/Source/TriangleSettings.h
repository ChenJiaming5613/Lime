// Tunable state of the rotating triangle.
//
// The LIME_REFLECT block below is the only place these fields are described: the engine derives the
// inspector controls and, when enabled, the saved settings from it.

#pragma once

#include "Core/Math/Vector.h"
#include "Core/Reflection/Reflection.h"

namespace HelloTriangle
{
	struct FTriangleSettings
	{
		bool bPaused = false;
		float RotationSpeed = 1.0f;
		float FovDegrees = 60.0f;
		Lime::FVector4 Tint{ 1.0f, 1.0f, 1.0f, 1.0f };
		Lime::FVector4 BackgroundColor{ 0.06f, 0.07f, 0.09f, 1.0f };
	};
} // namespace HelloTriangle

LIME_REFLECT(HelloTriangle::FTriangleSettings)
{
	LIME_PROPERTY(bPaused, Lime::FProp("Pause rotation"));
	LIME_PROPERTY(RotationSpeed, Lime::FProp("Speed").Range(-6.0f, 6.0f).Tooltip("Radians per second"));
	LIME_PROPERTY(FovDegrees, Lime::FProp("Field of view").Range(20.0f, 110.0f));
	LIME_PROPERTY(Tint, Lime::FProp("Tint").AsColor());
	LIME_PROPERTY(BackgroundColor, Lime::FProp("Background").AsColor());
}
