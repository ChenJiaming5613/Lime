// Perspective camera tests.
//
// Covers the pose and matrix logic that used to live in the fly camera, now that it belongs to the
// camera itself. All pure value logic: no window, no device.

#include "Camera/PerspectiveCamera.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

using Catch::Approx;
using namespace Lime;

TEST_CASE("Perspective camera basis", "[Camera][Perspective]")
{
	SECTION("The default camera looks down +Z")
	{
		// Left handed convention. Getting this backwards would make every model appear behind the camera.
		const FPerspectiveCamera Camera;
		REQUIRE(IsNearlyEqual(Camera.GetForward(), FVector3::UnitZ(), 1.0e-5f));
	}

	SECTION("Forward, right and up stay orthonormal at any aim")
	{
		FPerspectiveCamera Camera;
		Camera.SetRotation(DegreesToRadians(123.0f), DegreesToRadians(-34.0f));

		const FVector3 Forward = Camera.GetForward();
		const FVector3 Right = Camera.GetRight();
		const FVector3 Up = Camera.GetUp();

		REQUIRE(Forward.Length() == Approx(1.0f).margin(1.0e-5f));
		REQUIRE(Right.Length() == Approx(1.0f).margin(1.0e-5f));
		REQUIRE(Up.Length() == Approx(1.0f).margin(1.0e-5f));
		REQUIRE(Dot(Forward, Right) == Approx(0.0f).margin(1.0e-5f));
		REQUIRE(Dot(Forward, Up) == Approx(0.0f).margin(1.0e-5f));
		REQUIRE(Dot(Right, Up) == Approx(0.0f).margin(1.0e-5f));
	}

	SECTION("The horizon stays level after turning and pitching")
	{
		// No roll term is applied, so the right vector must stay horizontal whatever the aim.
		FPerspectiveCamera Camera;
		Camera.SetRotation(DegreesToRadians(37.0f), DegreesToRadians(-52.0f));
		REQUIRE(Camera.GetRight().Y == Approx(0.0f).margin(1.0e-5f));
	}
}

TEST_CASE("Perspective camera constraints", "[Camera][Perspective]")
{
	FPerspectiveCamera Camera;

	SECTION("Pitch is clamped short of vertical")
	{
		// At exactly 90 degrees the forward vector is parallel to world up and the view rolls
		// unpredictably, so this clamp is a correctness requirement rather than a nicety.
		Camera.SetRotation(0.0f, DegreesToRadians(200.0f));
		REQUIRE(Camera.GetPitch() == Approx(FPerspectiveCamera::MaxPitch));
		REQUIRE(Camera.GetPitch() < HalfPi);

		Camera.SetRotation(0.0f, DegreesToRadians(-200.0f));
		REQUIRE(Camera.GetPitch() == Approx(-FPerspectiveCamera::MaxPitch));
		REQUIRE(Camera.GetPitch() > -HalfPi);
	}

	SECTION("Yaw is wrapped so it stays bounded")
	{
		// Yaw would otherwise grow without limit across a long session and eventually lose precision.
		Camera.SetRotation(DegreesToRadians(3000.0f), 0.0f);
		REQUIRE(Camera.GetYaw() >= -Pi);
		REQUIRE(Camera.GetYaw() <= Pi);

		Camera.SetRotation(DegreesToRadians(-3000.0f), 0.0f);
		REQUIRE(Camera.GetYaw() >= -Pi);
		REQUIRE(Camera.GetYaw() <= Pi);
	}

	SECTION("Wrapping preserves the aim direction")
	{
		// Wrapping must only change the number, never where the camera points.
		FPerspectiveCamera Wrapped;
		Wrapped.SetRotation(DegreesToRadians(45.0f + 720.0f), 0.0f);

		FPerspectiveCamera Direct;
		Direct.SetRotation(DegreesToRadians(45.0f), 0.0f);

		REQUIRE(IsNearlyEqual(Wrapped.GetForward(), Direct.GetForward(), 1.0e-4f));
	}

	SECTION("Field of view is clamped away from the degenerate ends")
	{
		// At or beyond 180 degrees the projection matrix collapses and nothing is drawn.
		Camera.SetFieldOfView(DegreesToRadians(400.0f));
		REQUIRE(Camera.GetFieldOfView() == Approx(FPerspectiveCamera::MaxFieldOfView));

		Camera.SetFieldOfView(-1.0f);
		REQUIRE(Camera.GetFieldOfView() == Approx(FPerspectiveCamera::MinFieldOfView));
	}

	SECTION("A collapsed viewport does not corrupt the aspect ratio")
	{
		// A minimized or zero sized viewport reports height zero, which would divide by zero inside the
		// projection. The last good value has to survive instead.
		const float Original = Camera.GetAspectRatio();
		Camera.SetAspectRatio(0.0f);
		REQUIRE(Camera.GetAspectRatio() == Approx(Original));

		Camera.SetAspectRatio(-2.0f);
		REQUIRE(Camera.GetAspectRatio() == Approx(Original));

		Camera.SetAspectRatio(2.0f);
		REQUIRE(Camera.GetAspectRatio() == Approx(2.0f));
	}

	SECTION("Clip planes are corrected rather than trusted")
	{
		// A bad configuration file must not leave a black viewport with no explanation.
		Camera.SetClipPlanes(0.0f, 100.0f);
		REQUIRE(Camera.GetNearPlane() > 0.0f);

		Camera.SetClipPlanes(10.0f, 1.0f);
		REQUIRE(Camera.GetFarPlane() > Camera.GetNearPlane());
	}
}

TEST_CASE("Perspective camera matrices", "[Camera][Perspective]")
{
	SECTION("The view matrix brings the camera to the origin")
	{
		FPerspectiveCamera Camera;
		Camera.SetPosition({ 0.0f, 0.0f, -5.0f });
		Camera.SetRotation(0.0f, 0.0f);

		// A point at the world origin sits 5 units ahead along the view axis.
		const FVector3 Transformed = Camera.GetViewMatrix().TransformPosition(FVector3::Zero());
		REQUIRE(Transformed.X == Approx(0.0f).margin(1.0e-4f));
		REQUIRE(Transformed.Y == Approx(0.0f).margin(1.0e-4f));
		REQUIRE(Transformed.Z == Approx(5.0f).margin(1.0e-4f));
	}

	SECTION("The world matrix and the view matrix are inverses")
	{
		FPerspectiveCamera Camera;
		Camera.SetPosition({ 3.0f, -2.0f, 7.0f });
		Camera.SetRotation(DegreesToRadians(40.0f), DegreesToRadians(-15.0f));

		const FMatrix4x4 Product = Multiply(Camera.GetViewMatrix(), Camera.GetWorldMatrix());
		REQUIRE(IsNearlyEqual(Product, FMatrix4x4::Identity(), 1.0e-3f));
	}

	SECTION("A point ahead of the camera lands inside the clip volume")
	{
		// The strongest end to end check of the projection: depth must come out in [0, 1], which is what
		// both backends expect, and the point must be centred.
		FPerspectiveCamera Camera;
		Camera.SetPosition(FVector3::Zero());
		Camera.SetRotation(0.0f, 0.0f);
		Camera.SetClipPlanes(0.1f, 100.0f);

		const FVector4 Clip = Camera.GetViewProjectionMatrix().TransformVector4(FVector4({ 0.0f, 0.0f, 10.0f }, 1.0f));
		REQUIRE(Clip.W > 0.0f);

		const float Depth = Clip.Z / Clip.W;
		REQUIRE(Depth > 0.0f);
		REQUIRE(Depth < 1.0f);
		REQUIRE(Clip.X / Clip.W == Approx(0.0f).margin(1.0e-5f));
		REQUIRE(Clip.Y / Clip.W == Approx(0.0f).margin(1.0e-5f));
	}

	SECTION("The near plane maps to 0 and the far plane to 1")
	{
		FPerspectiveCamera Camera;
		Camera.SetPosition(FVector3::Zero());
		Camera.SetRotation(0.0f, 0.0f);
		Camera.SetClipPlanes(1.0f, 50.0f);

		const FMatrix4x4 ViewProjection = Camera.GetViewProjectionMatrix();

		const FVector4 Near = ViewProjection.TransformVector4(FVector4({ 0.0f, 0.0f, 1.0f }, 1.0f));
		REQUIRE(Near.Z / Near.W == Approx(0.0f).margin(1.0e-4f));

		const FVector4 Far = ViewProjection.TransformVector4(FVector4({ 0.0f, 0.0f, 50.0f }, 1.0f));
		REQUIRE(Far.Z / Far.W == Approx(1.0f).margin(1.0e-4f));
	}

	SECTION("A point behind the camera falls outside the clip volume")
	{
		FPerspectiveCamera Camera;
		Camera.SetPosition(FVector3::Zero());
		Camera.SetRotation(0.0f, 0.0f);

		const FVector4 Clip = Camera.GetViewProjectionMatrix().TransformVector4(FVector4({ 0.0f, 0.0f, -10.0f }, 1.0f));
		REQUIRE(Clip.W < 0.0f);
	}
}

TEST_CASE("Aiming the perspective camera", "[Camera][Perspective]")
{
	SECTION("LookAt points at the target")
	{
		FPerspectiveCamera Camera;
		Camera.SetPosition({ 0.0f, 0.0f, -10.0f });
		Camera.LookAt(FVector3::Zero());

		REQUIRE(IsNearlyEqual(Camera.GetForward(), FVector3::UnitZ(), 1.0e-4f));
	}

	SECTION("LookAt handles a target off every axis")
	{
		FPerspectiveCamera Camera;
		Camera.SetPosition(FVector3::Zero());
		Camera.LookAt({ 5.0f, 5.0f, 5.0f });

		REQUIRE(IsNearlyEqual(Camera.GetForward(), FVector3{ 5.0f, 5.0f, 5.0f }.GetNormalized(), 1.0e-4f));
	}

	SECTION("LookAt at the camera's own position leaves the aim unchanged")
	{
		// The direction would be undefined; keeping the current angles avoids producing NaNs.
		FPerspectiveCamera Camera;
		Camera.SetPosition({ 1.0f, 2.0f, 3.0f });
		Camera.SetRotation(0.5f, 0.25f);
		Camera.LookAt(Camera.GetPosition());

		REQUIRE(Camera.GetYaw() == Approx(0.5f));
		REQUIRE(Camera.GetPitch() == Approx(0.25f));
	}

	SECTION("LookAt straight down does not exceed the pitch limit")
	{
		// A target directly below implies 90 degrees of pitch, which the clamp has to catch.
		FPerspectiveCamera Camera;
		Camera.SetPosition({ 0.0f, 10.0f, 0.0f });
		Camera.LookAt(FVector3::Zero());

		REQUIRE(Camera.GetPitch() <= FPerspectiveCamera::MaxPitch);
		REQUIRE(Camera.GetPitch() >= -FPerspectiveCamera::MaxPitch);
	}

	SECTION("FrameSphere puts the whole sphere in front of the camera")
	{
		FPerspectiveCamera Camera;
		Camera.FrameSphere({ 1.0f, 2.0f, 3.0f }, 4.0f, FVector3::UnitZ());

		const FVector3 Center{ 1.0f, 2.0f, 3.0f };
		const FVector3 ToCenter = Center - Camera.GetPosition();

		// The camera must sit back from the sphere and face it.
		REQUIRE(ToCenter.Length() > 4.0f);
		REQUIRE(Dot(Camera.GetForward(), ToCenter.GetNormalized()) == Approx(1.0f).margin(1.0e-3f));

		SECTION("Clip planes reach past the sphere")
		{
			// A large model would otherwise be clipped away entirely.
			REQUIRE(Camera.GetFarPlane() > ToCenter.Length() + 4.0f);
			REQUIRE(Camera.GetNearPlane() > 0.0f);
			REQUIRE(Camera.GetNearPlane() < ToCenter.Length() - 4.0f);
		}
	}

	SECTION("FrameSphere scales the distance with the radius")
	{
		// Framing has to work for both a coin and a cathedral without any per model tuning.
		FPerspectiveCamera Small;
		Small.FrameSphere(FVector3::Zero(), 1.0f, FVector3::UnitZ());

		FPerspectiveCamera Large;
		Large.FrameSphere(FVector3::Zero(), 100.0f, FVector3::UnitZ());

		REQUIRE(Large.GetPosition().Length() > Small.GetPosition().Length() * 50.0f);
	}

	SECTION("FrameSphere tolerates a degenerate radius and direction")
	{
		// An empty scene reports zero bounds, and that must not produce NaNs.
		FPerspectiveCamera Camera;
		Camera.FrameSphere(FVector3::Zero(), 0.0f, FVector3::Zero());

		REQUIRE(Camera.GetPosition().Length() > 0.0f);
		REQUIRE(Camera.GetFarPlane() > Camera.GetNearPlane());
	}
}

TEST_CASE("The camera is usable through its interface", "[Camera][Interface]")
{
	// The point of ICamera is that render code can hold a base pointer and stay unchanged when another
	// projection is added later, so the base interface has to be sufficient on its own.
	const std::unique_ptr<FPerspectiveCamera> Owned = std::make_unique<FPerspectiveCamera>();
	Owned->SetPosition({ 1.0f, 2.0f, 3.0f });
	Owned->SetRotation(DegreesToRadians(30.0f), 0.0f);
	Owned->SetAspectRatio(1.5f);

	const ICamera& Camera = *Owned;

	REQUIRE(Camera.GetProjectionType() == ECameraProjection::Perspective);
	REQUIRE(IsNearlyEqual(Camera.GetPosition(), FVector3{ 1.0f, 2.0f, 3.0f }));
	REQUIRE(Camera.GetAspectRatio() == Approx(1.5f));
	REQUIRE(Camera.GetForward().Length() == Approx(1.0f).margin(1.0e-5f));
	REQUIRE(Camera.GetFarPlane() > Camera.GetNearPlane());

	SECTION("The default view projection matches the parts it is built from")
	{
		const FMatrix4x4 Expected = Multiply(Camera.GetProjectionMatrix(), Camera.GetViewMatrix());
		REQUIRE(IsNearlyEqual(Camera.GetViewProjectionMatrix(), Expected));
	}

	SECTION("Destruction through the base pointer is safe")
	{
		// A virtual destructor is what makes owning a camera by base pointer viable at all.
		std::unique_ptr<ICamera> Base = std::make_unique<FPerspectiveCamera>();
		REQUIRE(Base != nullptr);
		Base.reset();
		REQUIRE(Base == nullptr);
	}
}
