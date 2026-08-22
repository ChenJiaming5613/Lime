// Scalar math helpers.

#pragma once

#include "Core/CoreTypes.h"

#include <cmath>

namespace Lime
{
	constexpr float Pi = 3.14159265358979323846f;
	constexpr float TwoPi = Pi * 2.0f;
	constexpr float HalfPi = Pi * 0.5f;
	constexpr float SmallNumber = 1.0e-6f;

	constexpr float DegreesToRadians(float Degrees)
	{
		return Degrees * (Pi / 180.0f);
	}

	constexpr float RadiansToDegrees(float Radians)
	{
		return Radians * (180.0f / Pi);
	}

	template<typename T>
	constexpr T Clamp(T Value, T Min, T Max)
	{
		return Value < Min ? Min : (Value > Max ? Max : Value);
	}

	constexpr float Lerp(float A, float B, float Alpha)
	{
		return A + (B - A) * Alpha;
	}

	inline bool IsNearlyEqual(float A, float B, float Tolerance = SmallNumber)
	{
		return std::fabs(A - B) <= Tolerance;
	}

	inline bool IsNearlyZero(float Value, float Tolerance = SmallNumber)
	{
		return std::fabs(Value) <= Tolerance;
	}

	// Wraps an angle into [-Pi, Pi] so accumulated rotations never lose precision.
	inline float WrapAngle(float Radians)
	{
		const float Wrapped = std::fmod(Radians + Pi, TwoPi);
		return (Wrapped < 0.0f ? Wrapped + TwoPi : Wrapped) - Pi;
	}
} // namespace Lime
