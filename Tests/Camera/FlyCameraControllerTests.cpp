// Fly camera controller tests.
//
// Step() is a pure function, so every control rule is checked here by feeding synthetic frames. No
// window, no GPU and no ECS, which keeps these runnable in CI alongside the rest of the suite.
//
// The cases target behaviours that are easy to get wrong and hard to notice: look scaled by delta time,
// diagonal movement running faster, pitch flipping past vertical, and the camera being ejected because
// the cursor drifted over a panel mid drag.

#include "Camera/FlyCameraController.h"
#include "Camera/PerspectiveCamera.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace Lime;

namespace
{
	// A frame that is already flying, so each case can focus on one control at a time.
	FFlyCameraInput MakeFlyingFrame(float DeltaSeconds = 1.0f)
	{
		FFlyCameraInput Frame;
		Frame.bCanStartFlying = true;
		Frame.bRightButtonDown = true;
		Frame.DeltaSeconds = DeltaSeconds;
		return Frame;
	}

	FFlyCameraMotion MakeFlyingMotion()
	{
		FFlyCameraMotion Motion;
		Motion.Position = FVector3::Zero();
		Motion.bIsFlying = true;
		return Motion;
	}
} // namespace

TEST_CASE("Flying starts only where it is allowed", "[Camera][FlyCamera]")
{
	const FFlyCameraSettings Settings;

	SECTION("The right button alone does not start it")
	{
		// The editor passes viewport hover state as bCanStartFlying. Without this gate, right clicking a
		// panel would launch the camera and steal the cursor.
		FFlyCameraInput Frame;
		Frame.bRightButtonDown = true;
		Frame.bCanStartFlying = false;

		REQUIRE_FALSE(Step(FFlyCameraMotion{}, Frame, Settings).bIsFlying);
	}

	SECTION("The right button inside the viewport starts it")
	{
		FFlyCameraInput Frame;
		Frame.bRightButtonDown = true;
		Frame.bCanStartFlying = true;

		REQUIRE(Step(FFlyCameraMotion{}, Frame, Settings).bIsFlying);
	}

	SECTION("Once flying, leaving the viewport does not stop it")
	{
		// Dragging across the whole window is normal use; being ejected because the cursor crossed a panel
		// edge would make the camera unusable.
		FFlyCameraInput Frame;
		Frame.bRightButtonDown = true;
		Frame.bCanStartFlying = false;

		REQUIRE(Step(MakeFlyingMotion(), Frame, Settings).bIsFlying);
	}

	SECTION("Releasing the button stops it immediately")
	{
		FFlyCameraInput Frame;
		Frame.bRightButtonDown = false;
		Frame.bCanStartFlying = true;

		REQUIRE_FALSE(Step(MakeFlyingMotion(), Frame, Settings).bIsFlying);
	}

	SECTION("Input is ignored while not flying")
	{
		FFlyCameraInput Frame;
		Frame.bRightButtonDown = false;
		Frame.bForward = true;
		Frame.MouseDelta = { 100.0f, 100.0f };
		Frame.DeltaSeconds = 1.0f;

		const FFlyCameraMotion Result = Step(FFlyCameraMotion{}, Frame, Settings);
		REQUIRE(IsNearlyEqual(Result.Position, FFlyCameraMotion{}.Position));
		REQUIRE(Result.Yaw == Approx(0.0f));
		REQUIRE(Result.Pitch == Approx(0.0f));
	}
}

TEST_CASE("Movement keys travel along the expected axes", "[Camera][FlyCamera]")
{
	const FFlyCameraSettings Settings;
	const FFlyCameraMotion Motion = MakeFlyingMotion();

	SECTION("W moves forward by exactly the speed over one second")
	{
		FFlyCameraInput Frame = MakeFlyingFrame(1.0f);
		Frame.bForward = true;

		// Default yaw and pitch look down +Z in this left handed system.
		REQUIRE(IsNearlyEqual(Step(Motion, Frame, Settings).Position, FVector3{ 0.0f, 0.0f, Motion.MoveSpeed }, 1.0e-4f));
	}

	SECTION("S moves backward")
	{
		FFlyCameraInput Frame = MakeFlyingFrame(1.0f);
		Frame.bBackward = true;
		REQUIRE(Step(Motion, Frame, Settings).Position.Z == Approx(-Motion.MoveSpeed).margin(1.0e-4f));
	}

	SECTION("D moves right and A moves left")
	{
		FFlyCameraInput Right = MakeFlyingFrame(1.0f);
		Right.bRight = true;
		REQUIRE(Step(Motion, Right, Settings).Position.X == Approx(Motion.MoveSpeed).margin(1.0e-4f));

		FFlyCameraInput Left = MakeFlyingFrame(1.0f);
		Left.bLeft = true;
		REQUIRE(Step(Motion, Left, Settings).Position.X == Approx(-Motion.MoveSpeed).margin(1.0e-4f));
	}

	SECTION("E rises and Q descends")
	{
		FFlyCameraInput Up = MakeFlyingFrame(1.0f);
		Up.bUp = true;
		REQUIRE(Step(Motion, Up, Settings).Position.Y == Approx(Motion.MoveSpeed).margin(1.0e-4f));

		FFlyCameraInput Down = MakeFlyingFrame(1.0f);
		Down.bDown = true;
		REQUIRE(Step(Motion, Down, Settings).Position.Y == Approx(-Motion.MoveSpeed).margin(1.0e-4f));
	}

	SECTION("Vertical movement follows the world axis, not the camera's")
	{
		// Unity behaves this way, and it is what makes QE predictable while looking up or down. Using the
		// camera's own up would make it drift forward as it rises.
		FFlyCameraMotion Pitched = Motion;
		Pitched.Pitch = DegreesToRadians(45.0f);

		FFlyCameraInput Frame = MakeFlyingFrame(1.0f);
		Frame.bUp = true;

		const FFlyCameraMotion Result = Step(Pitched, Frame, Settings);
		REQUIRE(Result.Position.Y == Approx(Motion.MoveSpeed).margin(1.0e-4f));
		REQUIRE(Result.Position.X == Approx(0.0f).margin(1.0e-4f));
		REQUIRE(Result.Position.Z == Approx(0.0f).margin(1.0e-4f));
	}

	SECTION("Opposite keys cancel out")
	{
		FFlyCameraInput Frame = MakeFlyingFrame(1.0f);
		Frame.bForward = true;
		Frame.bBackward = true;

		REQUIRE(IsNearlyEqual(Step(Motion, Frame, Settings).Position, FVector3::Zero(), 1.0e-5f));
	}

	SECTION("Diagonal movement is not faster than straight movement")
	{
		// Summing two unit vectors without normalising would travel 1.41 times as fast diagonally, which is
		// a classic bug in hand written camera code.
		FFlyCameraInput Frame = MakeFlyingFrame(1.0f);
		Frame.bForward = true;
		Frame.bRight = true;

		REQUIRE(Step(Motion, Frame, Settings).Position.Length() == Approx(Motion.MoveSpeed).margin(1.0e-4f));
	}

	SECTION("Distance scales with delta time")
	{
		FFlyCameraInput Frame = MakeFlyingFrame(0.5f);
		Frame.bForward = true;
		REQUIRE(Step(Motion, Frame, Settings).Position.Z == Approx(Motion.MoveSpeed * 0.5f).margin(1.0e-4f));
	}

	SECTION("Shift multiplies the speed")
	{
		FFlyCameraInput Frame = MakeFlyingFrame(1.0f);
		Frame.bForward = true;
		Frame.bSprint = true;

		REQUIRE(Step(Motion, Frame, Settings).Position.Z ==
		        Approx(Motion.MoveSpeed * Settings.SprintMultiplier).margin(1.0e-3f));
	}

	SECTION("Movement follows where the camera is aimed")
	{
		// A quarter turn to the right should send W along +X rather than +Z.
		FFlyCameraMotion Turned = Motion;
		Turned.Yaw = HalfPi;

		FFlyCameraInput Frame = MakeFlyingFrame(1.0f);
		Frame.bForward = true;

		const FFlyCameraMotion Result = Step(Turned, Frame, Settings);
		REQUIRE(Result.Position.X == Approx(Motion.MoveSpeed).margin(1.0e-3f));
		REQUIRE(Result.Position.Z == Approx(0.0f).margin(1.0e-3f));
	}
}

TEST_CASE("Mouse look", "[Camera][FlyCamera]")
{
	const FFlyCameraSettings Settings;
	const FFlyCameraMotion Motion = MakeFlyingMotion();

	SECTION("Horizontal movement turns by sensitivity times pixels")
	{
		FFlyCameraInput Frame = MakeFlyingFrame();
		Frame.MouseDelta = { 100.0f, 0.0f };
		REQUIRE(Step(Motion, Frame, Settings).Yaw == Approx(100.0f * Settings.LookSensitivity));
	}

	SECTION("Look does not depend on frame rate")
	{
		// Scaling look by delta time is a common mistake: aiming would then respond differently at 30 and
		// 144 frames per second, and the same hand movement would land somewhere else.
		FFlyCameraInput Slow = MakeFlyingFrame(1.0f / 30.0f);
		Slow.MouseDelta = { 50.0f, 0.0f };

		FFlyCameraInput Fast = MakeFlyingFrame(1.0f / 144.0f);
		Fast.MouseDelta = { 50.0f, 0.0f };

		REQUIRE(Step(Motion, Slow, Settings).Yaw == Approx(Step(Motion, Fast, Settings).Yaw));
	}

	SECTION("Moving the mouse up raises the view")
	{
		// Screen Y grows downward, so an upward movement arrives as a negative delta and has to reduce the
		// pitch angle. Getting the sign wrong gives inverted look.
		FFlyCameraInput Frame = MakeFlyingFrame();
		Frame.MouseDelta = { 0.0f, -100.0f };

		const FFlyCameraMotion Result = Step(Motion, Frame, Settings);
		REQUIRE(Result.Pitch < 0.0f);

		// Confirmed against the camera, so the controller and the camera cannot disagree about which way
		// this points.
		FPerspectiveCamera Camera;
		Camera.SetRotation(Result.Yaw, Result.Pitch);
		REQUIRE(Camera.GetForward().Y > 0.0f);
	}

	SECTION("Pitch is clamped to the camera's own limit")
	{
		// The controller has to use the same limit the camera enforces, or the direction it moves along
		// would not match the direction the camera ends up facing.
		FFlyCameraInput Frame = MakeFlyingFrame();
		Frame.MouseDelta = { 0.0f, 100000.0f };
		REQUIRE(Step(Motion, Frame, Settings).Pitch == Approx(FPerspectiveCamera::MaxPitch));

		Frame.MouseDelta = { 0.0f, -100000.0f };
		REQUIRE(Step(Motion, Frame, Settings).Pitch == Approx(-FPerspectiveCamera::MaxPitch));
	}

	SECTION("Yaw stays bounded over a long session")
	{
		FFlyCameraMotion Result = Motion;
		FFlyCameraInput Frame = MakeFlyingFrame();
		Frame.MouseDelta = { 1000.0f, 0.0f };

		for (int32 Index = 0; Index < 200; ++Index)
		{
			Result = Step(Result, Frame, Settings);
		}

		REQUIRE(Result.Yaw >= -Pi);
		REQUIRE(Result.Yaw <= Pi);
	}
}

TEST_CASE("Scrolling changes the movement speed", "[Camera][FlyCamera]")
{
	const FFlyCameraSettings Settings;
	const FFlyCameraMotion Motion = MakeFlyingMotion();

	SECTION("Scrolling up multiplies the speed")
	{
		// Multiplicative rather than additive, so the steps stay usable at both ends of the range: a fixed
		// increment is far too coarse at low speeds and far too fine at high ones.
		FFlyCameraInput Frame = MakeFlyingFrame();
		Frame.ScrollDelta = 1.0f;
		REQUIRE(Step(Motion, Frame, Settings).MoveSpeed == Approx(Motion.MoveSpeed * Settings.SpeedScrollFactor));
	}

	SECTION("Scrolling down divides it")
	{
		FFlyCameraInput Frame = MakeFlyingFrame();
		Frame.ScrollDelta = -1.0f;
		REQUIRE(Step(Motion, Frame, Settings).MoveSpeed == Approx(Motion.MoveSpeed / Settings.SpeedScrollFactor));
	}

	SECTION("Speed is clamped at both ends")
	{
		FFlyCameraInput Up = MakeFlyingFrame();
		Up.ScrollDelta = 1000.0f;
		REQUIRE(Step(Motion, Up, Settings).MoveSpeed == Approx(Settings.MaxMoveSpeed));

		FFlyCameraInput Down = MakeFlyingFrame();
		Down.ScrollDelta = -1000.0f;
		REQUIRE(Step(Motion, Down, Settings).MoveSpeed == Approx(Settings.MinMoveSpeed));
	}

	SECTION("Speed never becomes zero or negative")
	{
		// A non positive speed would freeze the camera with no way to recover through the same control.
		FFlyCameraMotion Result = Motion;
		FFlyCameraInput Frame = MakeFlyingFrame();
		Frame.ScrollDelta = -10.0f;

		for (int32 Index = 0; Index < 50; ++Index)
		{
			Result = Step(Result, Frame, Settings);
		}

		REQUIRE(Result.MoveSpeed >= Settings.MinMoveSpeed);
	}

	SECTION("The chosen speed survives releasing the button")
	{
		FFlyCameraInput Scroll = MakeFlyingFrame();
		Scroll.ScrollDelta = 5.0f;
		const FFlyCameraMotion Faster = Step(Motion, Scroll, Settings);

		FFlyCameraInput Release = MakeFlyingFrame();
		Release.bRightButtonDown = false;
		const FFlyCameraMotion Stopped = Step(Faster, Release, Settings);

		REQUIRE_FALSE(Stopped.bIsFlying);
		REQUIRE(Stopped.MoveSpeed == Approx(Faster.MoveSpeed));
	}
}

TEST_CASE("The controller and the camera agree on the pose", "[Camera][FlyCamera]")
{
	// The controller now drives a separate camera, so the two must not be able to disagree.
	SECTION("SyncFromCamera adopts the camera's pose")
	{
		// Called after a model has been framed, so the first drag continues from where the camera is
		// rather than snapping back to a stale pose.
		FPerspectiveCamera Camera;
		Camera.SetPosition({ 4.0f, 5.0f, 6.0f });
		Camera.SetRotation(DegreesToRadians(25.0f), DegreesToRadians(-10.0f));

		FFlyCameraController Controller;
		Controller.SyncFromCamera(Camera);

		REQUIRE(IsNearlyEqual(Controller.GetMotion().Position, Camera.GetPosition()));
		REQUIRE(Controller.GetMotion().Yaw == Approx(Camera.GetYaw()));
		REQUIRE(Controller.GetMotion().Pitch == Approx(Camera.GetPitch()));
	}

	SECTION("A synced pose then stepped stays consistent with the camera")
	{
		// Applying the stepped angles to the camera must not change them: if the camera clamped further
		// than the controller, movement would head somewhere other than where the view points.
		FPerspectiveCamera Camera;
		FFlyCameraController Controller;
		Controller.SyncFromCamera(Camera);

		FFlyCameraInput Frame = MakeFlyingFrame();
		Frame.MouseDelta = { 0.0f, 100000.0f };

		const FFlyCameraMotion Stepped = Step(Controller.GetMotion(), Frame, FFlyCameraSettings{});
		Camera.SetRotation(Stepped.Yaw, Stepped.Pitch);

		REQUIRE(Camera.GetPitch() == Approx(Stepped.Pitch));
		REQUIRE(Camera.GetYaw() == Approx(Stepped.Yaw));
	}
}
