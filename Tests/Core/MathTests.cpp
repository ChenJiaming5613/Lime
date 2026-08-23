#include "Core/Math/Matrix.h"
#include "Core/Math/Quaternion.h"

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

TEST_CASE("Matrix inverse", "[Math][Matrix]")
{
	SECTION("A transform composed with its inverse is the identity")
	{
		// Non uniform scale on purpose: a rigid transform shortcut would pass a uniform case and fail
		// here, and glTF nodes routinely carry non uniform scale.
		const FMatrix4x4 Transform =
		    Multiply(FMatrix4x4::Translation({ 3.0f, -2.0f, 5.0f }),
		             Multiply(FMatrix4x4::RotationY(0.7f), FMatrix4x4::Scale({ 2.0f, 0.5f, 3.0f })));

		bool bInvertible = false;
		const FMatrix4x4 Inverse = Transform.GetInverse(&bInvertible);
		REQUIRE(bInvertible);
		REQUIRE(IsNearlyEqual(Multiply(Transform, Inverse), FMatrix4x4::Identity(), 1.0e-4f));
	}

	SECTION("Inverting a view matrix recovers the camera position")
	{
		// This is exactly how the renderer derives a view matrix from a camera world transform.
		const FVector3 Eye{ 4.0f, 3.0f, -6.0f };
		const FMatrix4x4 View = FMatrix4x4::LookAtLH(Eye, FVector3::Zero(), FVector3::UnitY());

		const FMatrix4x4 CameraToWorld = View.GetInverse();
		REQUIRE(IsNearlyEqual(CameraToWorld.TransformPosition(FVector3::Zero()), Eye, 1.0e-4f));
	}

	SECTION("A singular matrix yields identity rather than NaNs")
	{
		// A zero scale collapses the matrix. Returning identity keeps one broken node from poisoning
		// every descendant with NaNs.
		bool bInvertible = true;
		const FMatrix4x4 Result = FMatrix4x4::Scale({ 1.0f, 0.0f, 1.0f }).GetInverse(&bInvertible);
		REQUIRE_FALSE(bInvertible);
		REQUIRE(Result == FMatrix4x4::Identity());
	}

	SECTION("TransformDirection ignores translation")
	{
		const FMatrix4x4 Transform = FMatrix4x4::Translation({ 10.0f, 20.0f, 30.0f });
		REQUIRE(IsNearlyEqual(Transform.TransformDirection(FVector3::UnitX()), FVector3::UnitX()));
		REQUIRE(IsNearlyEqual(Transform.TransformPosition(FVector3::UnitX()), FVector3{ 11.0f, 20.0f, 30.0f }));
	}
}

TEST_CASE("Quaternion rotation", "[Math][Quaternion]")
{
	SECTION("Identity leaves a vector untouched")
	{
		REQUIRE(IsNearlyEqual(FQuat::Identity().RotateVector(FVector3::UnitZ()), FVector3::UnitZ()));
		REQUIRE(IsNearlyEqual(FQuat::Identity().ToMatrix(), FMatrix4x4::Identity()));
	}

	SECTION("A quarter turn about Y maps +Z onto +X")
	{
		// Left handed: rotating +Z by +90 degrees about +Y gives +X. Getting the handedness wrong here
		// would mirror every imported model.
		const FQuat Rotation = FQuat::FromAxisAngle(FVector3::UnitY(), HalfPi);
		REQUIRE(IsNearlyEqual(Rotation.RotateVector(FVector3::UnitZ()), FVector3::UnitX(), 1.0e-5f));
	}

	SECTION("RotateVector agrees with the matrix form")
	{
		// The two paths are used interchangeably, so they have to produce the same result.
		const FQuat Rotation = FQuat::FromYawPitchRoll(0.6f, -0.3f, 0.2f);
		const FVector3 Vector{ 1.0f, 2.0f, -3.0f };
		REQUIRE(IsNearlyEqual(Rotation.RotateVector(Vector), Rotation.ToMatrix().TransformDirection(Vector), 1.0e-4f));
	}

	SECTION("Rotation preserves length")
	{
		const FQuat Rotation = FQuat::FromYawPitchRoll(1.1f, 0.4f);
		REQUIRE(Rotation.RotateVector(FVector3{ 0.0f, 0.0f, 5.0f }).Length() == Approx(5.0f).margin(1.0e-4f));
	}

	SECTION("A degenerate quaternion normalizes to identity")
	{
		// glTF is not required to ship normalized rotations, and a zero one must not become NaNs.
		REQUIRE(FQuat(0.0f, 0.0f, 0.0f, 0.0f).GetNormalized() == FQuat::Identity());
	}

	SECTION("An unnormalized quaternion does not scale the matrix")
	{
		const FQuat Scaled{ 0.0f, 0.0f, 0.0f, 4.0f };
		REQUIRE(IsNearlyEqual(Scaled.ToMatrix(), FMatrix4x4::Identity(), 1.0e-5f));
	}

	SECTION("Multiply applies the right operand first")
	{
		const FQuat Yaw = FQuat::FromAxisAngle(FVector3::UnitY(), HalfPi);
		const FQuat Pitch = FQuat::FromAxisAngle(FVector3::UnitX(), HalfPi);
		const FVector3 Vector{ 0.0f, 0.0f, 1.0f };

		REQUIRE(IsNearlyEqual(Multiply(Yaw, Pitch).RotateVector(Vector), Yaw.RotateVector(Pitch.RotateVector(Vector)), 1.0e-5f));
	}

	SECTION("Opposite signs describe the same rotation")
	{
		const FQuat Rotation = FQuat::FromYawPitchRoll(0.9f, 0.2f);
		const FQuat Negated{ -Rotation.X, -Rotation.Y, -Rotation.Z, -Rotation.W };
		REQUIRE(IsNearlyEqual(Rotation, Negated));
	}
}

TEST_CASE("MakeTransform composes in glTF order", "[Math][Quaternion]")
{
	const FVector3 Translation{ 5.0f, 0.0f, 0.0f };
	const FQuat Rotation = FQuat::FromAxisAngle(FVector3::UnitY(), HalfPi);
	const FVector3 Scale{ 2.0f, 2.0f, 2.0f };

	const FMatrix4x4 Transform = MakeTransform(Translation, Rotation, Scale);

	SECTION("Scale is applied before rotation, and translation last")
	{
		// Wrong order shows up as a model drifting away from its intended position as it rotates,
		// which is why the expected value is checked against the explicit composition.
		const FMatrix4x4 Expected =
		    Multiply(FMatrix4x4::Translation(Translation), Multiply(Rotation.ToMatrix(), FMatrix4x4::Scale(Scale)));
		REQUIRE(IsNearlyEqual(Transform, Expected, 1.0e-5f));
	}

	SECTION("The origin maps to the translation")
	{
		REQUIRE(IsNearlyEqual(Transform.TransformPosition(FVector3::Zero()), Translation, 1.0e-5f));
	}

	SECTION("The transform is invertible and round trips")
	{
		const FVector3 Point{ 1.0f, -2.0f, 3.0f };
		const FVector3 RoundTripped = Transform.GetInverse().TransformPosition(Transform.TransformPosition(Point));
		REQUIRE(IsNearlyEqual(RoundTripped, Point, 1.0e-4f));
	}
}

TEST_CASE("Normal matrix keeps normals perpendicular under non uniform scale", "[Math][Matrix]")
{
	// The renderer uses the inverse transpose for normals. Under non uniform scale the plain world
	// matrix would shear them off the surface, which reads as wrong lighting rather than wrong geometry.
	const FMatrix4x4 World = FMatrix4x4::Scale({ 4.0f, 1.0f, 1.0f });
	const FMatrix4x4 NormalMatrix = World.GetInverse().GetTransposed();

	// A 45 degree surface in the XY plane: tangent (1,1,0), normal (-1,1,0).
	const FVector3 Tangent = World.TransformDirection(FVector3{ 1.0f, 1.0f, 0.0f });
	const FVector3 Normal = NormalMatrix.TransformDirection(FVector3{ -1.0f, 1.0f, 0.0f });

	REQUIRE(Dot(Tangent.GetNormalized(), Normal.GetNormalized()) == Approx(0.0f).margin(1.0e-5f));

	SECTION("The plain world matrix would not stay perpendicular")
	{
		const FVector3 Wrong = World.TransformDirection(FVector3{ -1.0f, 1.0f, 0.0f });
		REQUIRE(Dot(Tangent.GetNormalized(), Wrong.GetNormalized()) != Approx(0.0f).margin(1.0e-3f));
	}
}

