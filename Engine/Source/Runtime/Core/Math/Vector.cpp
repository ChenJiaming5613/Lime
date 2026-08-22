#include "Core/Math/Vector.h"

namespace Lime
{
	float FVector2::Length() const
	{
		return std::sqrt(X * X + Y * Y);
	}

	FVector2 FVector2::GetNormalized() const
	{
		const float Len = Length();
		if (Len <= SmallNumber)
		{
			return {};
		}
		const float Inverse = 1.0f / Len;
		return { X * Inverse, Y * Inverse };
	}

	float FVector3::Length() const
	{
		return std::sqrt(LengthSquared());
	}

	FVector3 FVector3::GetNormalized(float Tolerance) const
	{
		const float Len = Length();
		if (Len <= Tolerance)
		{
			return {};
		}
		const float Inverse = 1.0f / Len;
		return { X * Inverse, Y * Inverse, Z * Inverse };
	}

	bool IsNearlyEqual(const FVector3& A, const FVector3& B, float Tolerance)
	{
		return IsNearlyEqual(A.X, B.X, Tolerance) && IsNearlyEqual(A.Y, B.Y, Tolerance) && IsNearlyEqual(A.Z, B.Z, Tolerance);
	}

	bool IsNearlyEqual(const FVector4& A, const FVector4& B, float Tolerance)
	{
		return IsNearlyEqual(A.X, B.X, Tolerance) && IsNearlyEqual(A.Y, B.Y, Tolerance) && IsNearlyEqual(A.Z, B.Z, Tolerance) &&
		       IsNearlyEqual(A.W, B.W, Tolerance);
	}
} // namespace Lime
