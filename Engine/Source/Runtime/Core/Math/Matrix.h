// 4x4 matrix, row major storage, left handed coordinate system.
//
// Convention: vectors are treated as columns, so a transform is applied as M * v and composing
// transforms reads right to left: Multiply(Projection, Multiply(View, World)).
// Storage is row major to match the shader side, which is compiled with -Zpr, so no transpose is
// needed when uploading to a constant buffer.

#pragma once

#include "Core/Math/Vector.h"

namespace Lime
{
	struct FMatrix4x4
	{
		// M[Row][Column]
		float M[4][4] = {};

		constexpr FMatrix4x4() = default;

		static constexpr FMatrix4x4 Identity()
		{
			FMatrix4x4 Result;
			Result.M[0][0] = 1.0f;
			Result.M[1][1] = 1.0f;
			Result.M[2][2] = 1.0f;
			Result.M[3][3] = 1.0f;
			return Result;
		}

		static FMatrix4x4 Translation(const FVector3& Offset);
		static FMatrix4x4 Scale(const FVector3& Factors);
		static FMatrix4x4 RotationX(float Radians);
		static FMatrix4x4 RotationY(float Radians);
		static FMatrix4x4 RotationZ(float Radians);

		// Depth range is [0, 1], matching both D3D12 and the Vulkan setup NVRHI configures.
		static FMatrix4x4 PerspectiveFovLH(float FovYRadians, float AspectRatio, float NearZ, float FarZ);
		static FMatrix4x4 OrthographicOffCenterLH(float Left, float Right, float Bottom, float Top, float NearZ, float FarZ);
		static FMatrix4x4 LookAtLH(const FVector3& Eye, const FVector3& Target, const FVector3& Up);

		FMatrix4x4 GetTransposed() const;

		// Full 4x4 transform; the W component participates.
		FVector4 TransformVector4(const FVector4& Vector) const;
		// Treats the input as a point (W = 1) and returns the transformed XYZ without dividing by W.
		FVector3 TransformPosition(const FVector3& Position) const;

		bool operator==(const FMatrix4x4& Other) const;
	};

	FMatrix4x4 Multiply(const FMatrix4x4& A, const FMatrix4x4& B);
	bool IsNearlyEqual(const FMatrix4x4& A, const FMatrix4x4& B, float Tolerance = SmallNumber);
} // namespace Lime
