#include "Core/Math/Matrix.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace Lime;

TEST_CASE("Vector arithmetic", "[Math][Vector]")
{
	SECTION("Dot and cross products follow the right hand rule")
	{
		REQUIRE(Dot(FVector3::UnitX(), FVector3::UnitY()) == Approx(0.0f));
		REQUIRE(Dot(FVector3::UnitX(), FVector3::UnitX()) == Approx(1.0f));
		REQUIRE(IsNearlyEqual(Cross(FVector3::UnitX(), FVector3::UnitY()), FVector3::UnitZ()));
		REQUIRE(IsNearlyEqual(Cross(FVector3::UnitY(), FVector3::UnitX()), -FVector3::UnitZ()));
	}

	SECTION("Normalization")
	{
		const FVector3 Normalized = FVector3(3.0f, 4.0f, 0.0f).GetNormalized();
		REQUIRE(Normalized.Length() == Approx(1.0f));
		REQUIRE(Normalized.X == Approx(0.6f));
		REQUIRE(Normalized.Y == Approx(0.8f));
	}

	SECTION("Normalizing a degenerate vector yields zero instead of NaN")
	{
		REQUIRE(FVector3::Zero().GetNormalized() == FVector3::Zero());
		REQUIRE(FVector3(1.0e-9f, 0.0f, 0.0f).GetNormalized() == FVector3::Zero());
	}
}

TEST_CASE("Matrix identity and associativity", "[Math][Matrix]")
{
	const FMatrix4x4 Identity = FMatrix4x4::Identity();
	const FMatrix4x4 A = FMatrix4x4::RotationZ(0.7f);
	const FMatrix4x4 B = FMatrix4x4::Translation({ 1.0f, 2.0f, 3.0f });
	const FMatrix4x4 C = FMatrix4x4::Scale({ 2.0f, 2.0f, 2.0f });

	SECTION("Identity is neutral")
	{
		REQUIRE(IsNearlyEqual(Multiply(Identity, A), A));
		REQUIRE(IsNearlyEqual(Multiply(A, Identity), A));
	}

	SECTION("Multiplication is associative")
	{
		REQUIRE(IsNearlyEqual(Multiply(Multiply(A, B), C), Multiply(A, Multiply(B, C))));
	}

	SECTION("Transpose is an involution")
	{
		REQUIRE(IsNearlyEqual(A.GetTransposed().GetTransposed(), A));
	}
}

TEST_CASE("Matrix transforms", "[Math][Matrix]")
{
	SECTION("RotationZ by 90 degrees maps +X onto +Y")
	{
		const FMatrix4x4 Rotation = FMatrix4x4::RotationZ(HalfPi);
		REQUIRE(IsNearlyEqual(Rotation.TransformPosition(FVector3::UnitX()), FVector3::UnitY(), 1.0e-5f));
		REQUIRE(IsNearlyEqual(Rotation.TransformPosition(FVector3::UnitY()), -FVector3::UnitX(), 1.0e-5f));
	}

	SECTION("Translation moves a point but not a direction")
	{
		const FMatrix4x4 Translation = FMatrix4x4::Translation({ 5.0f, -2.0f, 1.0f });
		REQUIRE(IsNearlyEqual(Translation.TransformPosition(FVector3::Zero()), { 5.0f, -2.0f, 1.0f }));

		const FVector4 Direction = Translation.TransformVector4(FVector4(FVector3::UnitX(), 0.0f));
		REQUIRE(IsNearlyEqual(Direction.ToVector3(), FVector3::UnitX()));
	}

	SECTION("LookAtLH puts the target on the forward axis")
	{
		const FMatrix4x4 View = FMatrix4x4::LookAtLH({ 0.0f, 0.0f, -5.0f }, FVector3::Zero(), FVector3::UnitY());
		const FVector3 Origin = View.TransformPosition(FVector3::Zero());
		REQUIRE(Origin.X == Approx(0.0f).margin(1.0e-5f));
		REQUIRE(Origin.Y == Approx(0.0f).margin(1.0e-5f));
		REQUIRE(Origin.Z == Approx(5.0f).margin(1.0e-5f));
	}
}

TEST_CASE("PerspectiveFovLH maps the depth range onto zero to one", "[Math][Matrix]")
{
	constexpr float NearZ = 0.1f;
	constexpr float FarZ = 100.0f;
	const FMatrix4x4 Projection = FMatrix4x4::PerspectiveFovLH(DegreesToRadians(60.0f), 16.0f / 9.0f, NearZ, FarZ);

	// A left handed projection puts w in row 3, so the clip space depth is z/w.
	const FVector4 AtNear = Projection.TransformVector4(FVector4(0.0f, 0.0f, NearZ, 1.0f));
	const FVector4 AtFar = Projection.TransformVector4(FVector4(0.0f, 0.0f, FarZ, 1.0f));

	REQUIRE(AtNear.W == Approx(NearZ));
	REQUIRE(AtFar.W == Approx(FarZ));
	REQUIRE(AtNear.Z / AtNear.W == Approx(0.0f).margin(1.0e-5f));
	REQUIRE(AtFar.Z / AtFar.W == Approx(1.0f).margin(1.0e-5f));
}

TEST_CASE("OrthographicOffCenterLH maps the viewport corners onto clip space", "[Math][Matrix]")
{
	// Matches the ImGui setup: origin at the top left, y growing downwards.
	const FMatrix4x4 Projection = FMatrix4x4::OrthographicOffCenterLH(0.0f, 800.0f, 600.0f, 0.0f, 0.0f, 1.0f);

	const FVector3 TopLeft = Projection.TransformPosition({ 0.0f, 0.0f, 0.0f });
	REQUIRE(TopLeft.X == Approx(-1.0f));
	REQUIRE(TopLeft.Y == Approx(1.0f));

	const FVector3 BottomRight = Projection.TransformPosition({ 800.0f, 600.0f, 0.0f });
	REQUIRE(BottomRight.X == Approx(1.0f));
	REQUIRE(BottomRight.Y == Approx(-1.0f));
}

TEST_CASE("Angle helpers", "[Math][Utils]")
{
	REQUIRE(DegreesToRadians(180.0f) == Approx(Pi));
	REQUIRE(RadiansToDegrees(Pi) == Approx(180.0f));
	REQUIRE(Clamp(5.0f, 0.0f, 1.0f) == Approx(1.0f));
	REQUIRE(Lerp(0.0f, 10.0f, 0.25f) == Approx(2.5f));

	SECTION("WrapAngle keeps accumulated rotations bounded")
	{
		REQUIRE(WrapAngle(0.5f) == Approx(0.5f));
		REQUIRE(WrapAngle(TwoPi + 0.5f) == Approx(0.5f).margin(1.0e-5f));
		REQUIRE(WrapAngle(-TwoPi - 0.5f) == Approx(-0.5f).margin(1.0e-5f));
		REQUIRE(WrapAngle(100.0f * TwoPi + 1.0f) == Approx(1.0f).margin(1.0e-3f));
	}
}
