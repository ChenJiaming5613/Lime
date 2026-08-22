#include "Core/Math/Matrix.h"

namespace Lime
{
	FMatrix4x4 FMatrix4x4::Translation(const FVector3& Offset)
	{
		FMatrix4x4 Result = Identity();
		Result.M[0][3] = Offset.X;
		Result.M[1][3] = Offset.Y;
		Result.M[2][3] = Offset.Z;
		return Result;
	}

	FMatrix4x4 FMatrix4x4::Scale(const FVector3& Factors)
	{
		FMatrix4x4 Result = Identity();
		Result.M[0][0] = Factors.X;
		Result.M[1][1] = Factors.Y;
		Result.M[2][2] = Factors.Z;
		return Result;
	}

	FMatrix4x4 FMatrix4x4::RotationX(float Radians)
	{
		const float SinValue = std::sin(Radians);
		const float CosValue = std::cos(Radians);

		FMatrix4x4 Result = Identity();
		Result.M[1][1] = CosValue;
		Result.M[1][2] = -SinValue;
		Result.M[2][1] = SinValue;
		Result.M[2][2] = CosValue;
		return Result;
	}

	FMatrix4x4 FMatrix4x4::RotationY(float Radians)
	{
		const float SinValue = std::sin(Radians);
		const float CosValue = std::cos(Radians);

		FMatrix4x4 Result = Identity();
		Result.M[0][0] = CosValue;
		Result.M[0][2] = SinValue;
		Result.M[2][0] = -SinValue;
		Result.M[2][2] = CosValue;
		return Result;
	}

	FMatrix4x4 FMatrix4x4::RotationZ(float Radians)
	{
		const float SinValue = std::sin(Radians);
		const float CosValue = std::cos(Radians);

		FMatrix4x4 Result = Identity();
		Result.M[0][0] = CosValue;
		Result.M[0][1] = -SinValue;
		Result.M[1][0] = SinValue;
		Result.M[1][1] = CosValue;
		return Result;
	}

	FMatrix4x4 FMatrix4x4::PerspectiveFovLH(float FovYRadians, float AspectRatio, float NearZ, float FarZ)
	{
		const float TanHalfFov = std::tan(FovYRadians * 0.5f);
		const float Height = 1.0f / TanHalfFov;
		const float Width = Height / AspectRatio;
		const float Range = FarZ / (FarZ - NearZ);

		FMatrix4x4 Result;
		Result.M[0][0] = Width;
		Result.M[1][1] = Height;
		Result.M[2][2] = Range;
		Result.M[2][3] = -Range * NearZ;
		Result.M[3][2] = 1.0f;
		return Result;
	}

	FMatrix4x4 FMatrix4x4::OrthographicOffCenterLH(float Left, float Right, float Bottom, float Top, float NearZ, float FarZ)
	{
		FMatrix4x4 Result = Identity();
		Result.M[0][0] = 2.0f / (Right - Left);
		Result.M[1][1] = 2.0f / (Top - Bottom);
		Result.M[2][2] = 1.0f / (FarZ - NearZ);
		Result.M[0][3] = (Left + Right) / (Left - Right);
		Result.M[1][3] = (Top + Bottom) / (Bottom - Top);
		Result.M[2][3] = NearZ / (NearZ - FarZ);
		return Result;
	}

	FMatrix4x4 FMatrix4x4::LookAtLH(const FVector3& Eye, const FVector3& Target, const FVector3& Up)
	{
		const FVector3 Forward = (Target - Eye).GetNormalized();
		const FVector3 Right = Cross(Up, Forward).GetNormalized();
		const FVector3 TrueUp = Cross(Forward, Right);

		FMatrix4x4 Result = Identity();
		Result.M[0][0] = Right.X;
		Result.M[0][1] = Right.Y;
		Result.M[0][2] = Right.Z;
		Result.M[0][3] = -Dot(Right, Eye);

		Result.M[1][0] = TrueUp.X;
		Result.M[1][1] = TrueUp.Y;
		Result.M[1][2] = TrueUp.Z;
		Result.M[1][3] = -Dot(TrueUp, Eye);

		Result.M[2][0] = Forward.X;
		Result.M[2][1] = Forward.Y;
		Result.M[2][2] = Forward.Z;
		Result.M[2][3] = -Dot(Forward, Eye);
		return Result;
	}

	FMatrix4x4 FMatrix4x4::GetTransposed() const
	{
		FMatrix4x4 Result;
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Column = 0; Column < 4; ++Column)
			{
				Result.M[Row][Column] = M[Column][Row];
			}
		}
		return Result;
	}

	FVector4 FMatrix4x4::TransformVector4(const FVector4& Vector) const
	{
		return {
			M[0][0] * Vector.X + M[0][1] * Vector.Y + M[0][2] * Vector.Z + M[0][3] * Vector.W,
			M[1][0] * Vector.X + M[1][1] * Vector.Y + M[1][2] * Vector.Z + M[1][3] * Vector.W,
			M[2][0] * Vector.X + M[2][1] * Vector.Y + M[2][2] * Vector.Z + M[2][3] * Vector.W,
			M[3][0] * Vector.X + M[3][1] * Vector.Y + M[3][2] * Vector.Z + M[3][3] * Vector.W,
		};
	}

	FVector3 FMatrix4x4::TransformPosition(const FVector3& Position) const
	{
		return TransformVector4(FVector4(Position, 1.0f)).ToVector3();
	}

	bool FMatrix4x4::operator==(const FMatrix4x4& Other) const
	{
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Column = 0; Column < 4; ++Column)
			{
				if (M[Row][Column] != Other.M[Row][Column])
				{
					return false;
				}
			}
		}
		return true;
	}

	FMatrix4x4 Multiply(const FMatrix4x4& A, const FMatrix4x4& B)
	{
		FMatrix4x4 Result;
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Column = 0; Column < 4; ++Column)
			{
				Result.M[Row][Column] = A.M[Row][0] * B.M[0][Column] + A.M[Row][1] * B.M[1][Column] + A.M[Row][2] * B.M[2][Column] +
				                        A.M[Row][3] * B.M[3][Column];
			}
		}
		return Result;
	}

	bool IsNearlyEqual(const FMatrix4x4& A, const FMatrix4x4& B, float Tolerance)
	{
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Column = 0; Column < 4; ++Column)
			{
				if (!IsNearlyEqual(A.M[Row][Column], B.M[Row][Column], Tolerance))
				{
					return false;
				}
			}
		}
		return true;
	}
} // namespace Lime
