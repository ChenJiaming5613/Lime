// Unity style fly camera controller.
//
// Split into two parts on purpose:
//
//   FFlyCameraMotion / Step()  is pure value logic. It takes a snapshot of input plus the current pose
//                              and returns the new pose, so every rule below is covered by unit tests
//                              with no window, no GPU and no ECS.
//   FFlyCameraController       binds that logic to FInput, FWindow and a camera, which is the part that
//                              cannot be tested without a real window.
//
// The controller drives an FPerspectiveCamera rather than owning the pose itself. Keeping control
// separate from the camera is what allows a different scheme (orbit, turntable, scripted path) to drive
// the same camera later without duplicating any of it.
//
// Controls match Unity's scene view so the muscle memory carries over: hold the right mouse button to
// fly, then the mouse aims, WASD moves on the view plane, QE moves vertically, the scroll wheel changes
// speed and shift accelerates. Releasing the button hands the cursor straight back.

#pragma once

#include "Core/CoreTypes.h"
#include "Core/Math/MathUtils.h"
#include "Core/Math/Vector.h"

namespace Lime
{
	class FPerspectiveCamera;
	class FWindow;

	struct FFlyCameraSettings
	{
		// Units per second at the default speed step.
		float MoveSpeed = 3.0f;
		// Multiplier applied while shift is held.
		float SprintMultiplier = 3.0f;
		// Radians of rotation per pixel of mouse movement.
		float LookSensitivity = 0.003f;
		// Each scroll notch multiplies the speed by this, which gives even steps across the whole range
		// instead of the coarse ones a fixed increment produces at low speeds.
		float SpeedScrollFactor = 1.15f;
		float MinMoveSpeed = 0.05f;
		float MaxMoveSpeed = 500.0f;
	};

	// The part of the camera the controller reads and writes, plus the state it carries between frames.
	// Separated from the camera itself so the movement rules can be exercised as a pure function.
	struct FFlyCameraMotion
	{
		FVector3 Position{ 0.0f, 0.0f, -5.0f };
		float Yaw = 0.0f;
		float Pitch = 0.0f;
		float MoveSpeed = 3.0f;
		// True while the right button is held and the camera is flying.
		bool bIsFlying = false;
	};

	// One frame of input, as far as the camera is concerned. Decoupling this from FInput is what lets the
	// rules be tested by feeding synthetic frames.
	struct FFlyCameraInput
	{
		// Whether flying is allowed to start this frame. The editor passes viewport hover state here so a
		// right click on a panel does not launch the camera.
		bool bCanStartFlying = false;
		bool bRightButtonDown = false;

		bool bForward = false;
		bool bBackward = false;
		bool bLeft = false;
		bool bRight = false;
		bool bUp = false;
		bool bDown = false;
		bool bSprint = false;

		FVector2 MouseDelta;
		float ScrollDelta = 0.0f;
		float DeltaSeconds = 0.0f;
	};

	// Advances the motion by one frame. Pure: the same input and state always give the same result.
	//
	// Pitch is clamped to FPerspectiveCamera::MaxPitch rather than to a limit of its own, so that the
	// movement direction computed here matches the orientation the camera will actually adopt. Duplicating
	// the constant would let the two drift apart.
	FFlyCameraMotion Step(const FFlyCameraMotion& Motion, const FFlyCameraInput& Input, const FFlyCameraSettings& Settings);

	class FFlyCameraController
	{
	public:
		void SetSettings(const FFlyCameraSettings& InSettings) { Settings = InSettings; }
		const FFlyCameraSettings& GetSettings() const { return Settings; }

		const FFlyCameraMotion& GetMotion() const { return Motion; }
		bool IsFlying() const { return Motion.bIsFlying; }
		float GetMoveSpeed() const { return Motion.MoveSpeed; }

		// Adopts the camera's current pose as the starting point. Called after the camera has been placed,
		// so the first drag continues from where it is rather than jumping back to a stale pose.
		void SyncFromCamera(const FPerspectiveCamera& Camera);

		// Reads FInput, advances the camera and captures or releases the cursor to match.
		//
		// bCanStartFlying gates only the transition into flying, not staying in it: once the button is held,
		// moving the cursor over a panel must not eject the camera.
		void Update(FPerspectiveCamera& Camera, FWindow& Window, float DeltaSeconds, bool bCanStartFlying);

		// Releases the cursor if it is still captured. Called when the window loses focus or on shutdown, so
		// the cursor can never be left hidden.
		void Release(FWindow& Window);

	private:
		FFlyCameraSettings Settings;
		FFlyCameraMotion Motion;
	};
} // namespace Lime
