// Quaternion rotation.
//
// Exists because glTF stores node rotations as quaternions. Converting them to Euler angles on import
// and back on use would introduce gimbal lock and lose the shortest-arc property, so the quaternion
// is kept as the authored representation and only converted to a matrix when building the world
// transform.
//
// Same conventions as the rest of the math library: left handed, and composing rotations reads right
// to left, so Multiply(Second, First) applies First then Second.

#pragma once

#include "Core/Math/Matrix.h"
#include "Core/Math/Vector.h"

namespace Lime
{
	struct FQuat
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		float W = 1.0f;

		constexpr FQuat() = default;
		constexpr FQuat(float InX, float InY, float InZ, float InW)
		    : X(InX),
		      Y(InY),
		      Z(InZ),
		      W(InW)
		{
		}

		static constexpr FQuat Identity() { return { 0.0f, 0.0f, 0.0f, 1.0f }; }

		static FQuat FromAxisAngle(const FVector3& Axis, float Radians);
		// Yaw around Y, then pitch around X, then roll around Z. This is the order a fly camera needs:
		// yaw is applied in world space and pitch in the camera's own space, which is what keeps the
		// horizon level however far the camera has turned.
		static FQuat FromYawPitchRoll(float YawRadians, float PitchRadians, float RollRadians = 0.0f);

		float Length() const;
		// Returns identity when the length is below the tolerance, so a malformed glTF rotation cannot
		// turn into NaNs that spread through the whole node hierarchy.
		FQuat GetNormalized(float Tolerance = SmallNumber) const;
		FQuat GetConjugate() const { return { -X, -Y, -Z, W }; }

		FMatrix4x4 ToMatrix() const;
		// Rotates a vector without building a matrix first.
		FVector3 RotateVector(const FVector3& Vector) const;

		bool operator==(const FQuat& Other) const { return X == Other.X && Y == Other.Y && Z == Other.Z && W == Other.W; }
	};

	// Applies B first, then A.
	FQuat Multiply(const FQuat& A, const FQuat& B);
	bool IsNearlyEqual(const FQuat& A, const FQuat& B, float Tolerance = SmallNumber);

	// Composes scale, then rotation, then translation. This is the order glTF defines for a node, and
	// getting it wrong shows up as models that drift away from the origin as they rotate.
	//
	// A free function rather than a member of FMatrix4x4 so that Matrix.h stays independent of FQuat.
	FMatrix4x4 MakeTransform(const FVector3& Translation, const FQuat& Rotation, const FVector3& Scale);
} // namespace Lime
