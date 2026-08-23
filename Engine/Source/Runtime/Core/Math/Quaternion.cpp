#include "Core/Math/Quaternion.h"

#include <cmath>

namespace Lime
{
	FQuat FQuat::FromAxisAngle(const FVector3& Axis, float Radians)
	{
		const FVector3 Unit = Axis.GetNormalized();
		if (Unit == FVector3::Zero())
		{
			return Identity();
		}

		const float HalfAngle = Radians * 0.5f;
		const float SinHalf = std::sin(HalfAngle);
		return { Unit.X * SinHalf, Unit.Y * SinHalf, Unit.Z * SinHalf, std::cos(HalfAngle) };
	}

	FQuat FQuat::FromYawPitchRoll(float YawRadians, float PitchRadians, float RollRadians)
	{
		// Built from three axis rotations rather than a closed form, so the order is visible in the
		// code and matches the comment in the header.
		const FQuat Yaw = FromAxisAngle(FVector3::UnitY(), YawRadians);
		const FQuat Pitch = FromAxisAngle(FVector3::UnitX(), PitchRadians);
		const FQuat Roll = FromAxisAngle(FVector3::UnitZ(), RollRadians);
		return Multiply(Yaw, Multiply(Pitch, Roll));
	}

	float FQuat::Length() const
	{
		return std::sqrt(X * X + Y * Y + Z * Z + W * W);
	}

	FQuat FQuat::GetNormalized(float Tolerance) const
	{
		const float Magnitude = Length();
		if (Magnitude <= Tolerance)
		{
			return Identity();
		}

		const float Inverse = 1.0f / Magnitude;
		return { X * Inverse, Y * Inverse, Z * Inverse, W * Inverse };
	}

	FMatrix4x4 FQuat::ToMatrix() const
	{
		// Normalized first: an unnormalized quaternion produces a matrix that also scales, which would
		// silently stretch the model instead of only rotating it.
		const FQuat Q = GetNormalized();

		const float XX = Q.X * Q.X;
		const float YY = Q.Y * Q.Y;
		const float ZZ = Q.Z * Q.Z;
		const float XY = Q.X * Q.Y;
		const float XZ = Q.X * Q.Z;
		const float YZ = Q.Y * Q.Z;
		const float WX = Q.W * Q.X;
		const float WY = Q.W * Q.Y;
		const float WZ = Q.W * Q.Z;

		FMatrix4x4 Result = FMatrix4x4::Identity();

		Result.M[0][0] = 1.0f - 2.0f * (YY + ZZ);
		Result.M[0][1] = 2.0f * (XY - WZ);
		Result.M[0][2] = 2.0f * (XZ + WY);

		Result.M[1][0] = 2.0f * (XY + WZ);
		Result.M[1][1] = 1.0f - 2.0f * (XX + ZZ);
		Result.M[1][2] = 2.0f * (YZ - WX);

		Result.M[2][0] = 2.0f * (XZ - WY);
		Result.M[2][1] = 2.0f * (YZ + WX);
		Result.M[2][2] = 1.0f - 2.0f * (XX + YY);

		return Result;
	}

	FVector3 FQuat::RotateVector(const FVector3& Vector) const
	{
		// v' = v + 2w(q x v) + 2(q x (q x v)), which avoids materializing the matrix.
		const FQuat Q = GetNormalized();
		const FVector3 Axis{ Q.X, Q.Y, Q.Z };
		const FVector3 First = Cross(Axis, Vector);
		const FVector3 Second = Cross(Axis, First);
		return Vector + First * (2.0f * Q.W) + Second * 2.0f;
	}

	FQuat Multiply(const FQuat& A, const FQuat& B)
	{
		return {
			A.W * B.X + A.X * B.W + A.Y * B.Z - A.Z * B.Y,
			A.W * B.Y - A.X * B.Z + A.Y * B.W + A.Z * B.X,
			A.W * B.Z + A.X * B.Y - A.Y * B.X + A.Z * B.W,
			A.W * B.W - A.X * B.X - A.Y * B.Y - A.Z * B.Z,
		};
	}

	bool IsNearlyEqual(const FQuat& A, const FQuat& B, float Tolerance)
	{
		// q and -q are the same rotation, so both signs count as equal. Comparing components directly
		// would report a difference where none exists visually.
		const bool bSameSign = IsNearlyEqual(A.X, B.X, Tolerance) && IsNearlyEqual(A.Y, B.Y, Tolerance) &&
		                       IsNearlyEqual(A.Z, B.Z, Tolerance) && IsNearlyEqual(A.W, B.W, Tolerance);
		if (bSameSign)
		{
			return true;
		}

		return IsNearlyEqual(A.X, -B.X, Tolerance) && IsNearlyEqual(A.Y, -B.Y, Tolerance) && IsNearlyEqual(A.Z, -B.Z, Tolerance) &&
		       IsNearlyEqual(A.W, -B.W, Tolerance);
	}

	FMatrix4x4 MakeTransform(const FVector3& Translation, const FQuat& Rotation, const FVector3& Scale)
	{
		FMatrix4x4 Result = Rotation.ToMatrix();

		// Scale is folded into the rotation columns rather than multiplied as a separate matrix, which
		// is the same result as Rotation * Scale but without the extra 4x4 multiply per node.
		for (int32 Row = 0; Row < 3; ++Row)
		{
			Result.M[Row][0] *= Scale.X;
			Result.M[Row][1] *= Scale.Y;
			Result.M[Row][2] *= Scale.Z;
		}

		Result.M[0][3] = Translation.X;
		Result.M[1][3] = Translation.Y;
		Result.M[2][3] = Translation.Z;
		return Result;
	}
} // namespace Lime
