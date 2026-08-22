// Vector types. Value semantics throughout, no SIMD yet.

#pragma once

#include "Core/Math/MathUtils.h"

namespace Lime
{
	struct FVector2
	{
		float X = 0.0f;
		float Y = 0.0f;

		constexpr FVector2() = default;
		constexpr FVector2(float InX, float InY)
		    : X(InX),
		      Y(InY)
		{
		}

		constexpr FVector2 operator+(const FVector2& Other) const { return { X + Other.X, Y + Other.Y }; }
		constexpr FVector2 operator-(const FVector2& Other) const { return { X - Other.X, Y - Other.Y }; }
		constexpr FVector2 operator*(float Scalar) const { return { X * Scalar, Y * Scalar }; }
		constexpr bool operator==(const FVector2& Other) const { return X == Other.X && Y == Other.Y; }

		float Length() const;
		FVector2 GetNormalized() const;
	};

	struct FVector3
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;

		constexpr FVector3() = default;
		constexpr FVector3(float InX, float InY, float InZ)
		    : X(InX),
		      Y(InY),
		      Z(InZ)
		{
		}

		constexpr FVector3 operator+(const FVector3& Other) const { return { X + Other.X, Y + Other.Y, Z + Other.Z }; }
		constexpr FVector3 operator-(const FVector3& Other) const { return { X - Other.X, Y - Other.Y, Z - Other.Z }; }
		constexpr FVector3 operator-() const { return { -X, -Y, -Z }; }
		constexpr FVector3 operator*(float Scalar) const { return { X * Scalar, Y * Scalar, Z * Scalar }; }
		constexpr bool operator==(const FVector3& Other) const { return X == Other.X && Y == Other.Y && Z == Other.Z; }

		float Length() const;
		float LengthSquared() const { return X * X + Y * Y + Z * Z; }
		// Returns a zero vector when the input length is below the tolerance.
		FVector3 GetNormalized(float Tolerance = SmallNumber) const;

		static constexpr FVector3 Zero() { return { 0.0f, 0.0f, 0.0f }; }
		static constexpr FVector3 One() { return { 1.0f, 1.0f, 1.0f }; }
		static constexpr FVector3 UnitX() { return { 1.0f, 0.0f, 0.0f }; }
		static constexpr FVector3 UnitY() { return { 0.0f, 1.0f, 0.0f }; }
		static constexpr FVector3 UnitZ() { return { 0.0f, 0.0f, 1.0f }; }
	};

	struct FVector4
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		float W = 0.0f;

		constexpr FVector4() = default;
		constexpr FVector4(float InX, float InY, float InZ, float InW)
		    : X(InX),
		      Y(InY),
		      Z(InZ),
		      W(InW)
		{
		}
		constexpr explicit FVector4(const FVector3& Vector, float InW = 1.0f)
		    : X(Vector.X),
		      Y(Vector.Y),
		      Z(Vector.Z),
		      W(InW)
		{
		}

		constexpr FVector4 operator+(const FVector4& Other) const { return { X + Other.X, Y + Other.Y, Z + Other.Z, W + Other.W }; }
		constexpr FVector4 operator-(const FVector4& Other) const { return { X - Other.X, Y - Other.Y, Z - Other.Z, W - Other.W }; }
		constexpr FVector4 operator*(float Scalar) const { return { X * Scalar, Y * Scalar, Z * Scalar, W * Scalar }; }
		constexpr bool operator==(const FVector4& Other) const { return X == Other.X && Y == Other.Y && Z == Other.Z && W == Other.W; }

		constexpr FVector3 ToVector3() const { return { X, Y, Z }; }
	};

	constexpr float Dot(const FVector2& A, const FVector2& B)
	{
		return A.X * B.X + A.Y * B.Y;
	}

	constexpr float Dot(const FVector3& A, const FVector3& B)
	{
		return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
	}

	constexpr float Dot(const FVector4& A, const FVector4& B)
	{
		return A.X * B.X + A.Y * B.Y + A.Z * B.Z + A.W * B.W;
	}

	constexpr FVector3 Cross(const FVector3& A, const FVector3& B)
	{
		return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X };
	}

	bool IsNearlyEqual(const FVector3& A, const FVector3& B, float Tolerance = SmallNumber);
	bool IsNearlyEqual(const FVector4& A, const FVector4& B, float Tolerance = SmallNumber);
} // namespace Lime
