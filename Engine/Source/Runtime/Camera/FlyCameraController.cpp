#include "Camera/FlyCameraController.h"

#include "Camera/PerspectiveCamera.h"
#include "Platform/Input.h"
#include "Platform/Window.h"

#include <cmath>

namespace Lime
{
	namespace
	{
		// Basis vectors for a yaw and pitch pair, matching what FPerspectiveCamera derives from the same
		// angles. Computed here rather than through a temporary camera so Step stays a pure function of its
		// arguments.
		FVector3 GetForwardFor(float Yaw, float Pitch)
		{
			return FQuat::FromYawPitchRoll(Yaw, Pitch, 0.0f).RotateVector(FVector3::UnitZ());
		}

		FVector3 GetRightFor(float Yaw, float Pitch)
		{
			return FQuat::FromYawPitchRoll(Yaw, Pitch, 0.0f).RotateVector(FVector3::UnitX());
		}
	} // namespace

	FFlyCameraMotion Step(const FFlyCameraMotion& Motion, const FFlyCameraInput& Input, const FFlyCameraSettings& Settings)
	{
		FFlyCameraMotion Result = Motion;

		// Starting requires permission, continuing does not: once the button is held the camera keeps
		// control even if the cursor passes over a panel, which is what makes dragging across the whole
		// window feel right.
		if (Input.bRightButtonDown)
		{
			Result.bIsFlying = Motion.bIsFlying || Input.bCanStartFlying;
		}
		else
		{
			Result.bIsFlying = false;
		}

		if (!Result.bIsFlying)
		{
			return Result;
		}

		// Speed changes persist after the camera stops flying, so a chosen speed is not lost between drags.
		if (Input.ScrollDelta != 0.0f)
		{
			Result.MoveSpeed = Clamp(Motion.MoveSpeed * std::pow(Settings.SpeedScrollFactor, Input.ScrollDelta),
			                         Settings.MinMoveSpeed, Settings.MaxMoveSpeed);
		}

		// Look is driven by pixels, not by time: the view should follow the hand one to one regardless of
		// frame rate. Scaling this by delta time is a common mistake that makes aiming feel unpredictable.
		Result.Yaw = Motion.Yaw + Input.MouseDelta.X * Settings.LookSensitivity;
		// Screen Y grows downward, so moving the mouse up has to lower the pitch angle.
		Result.Pitch = Clamp(Motion.Pitch + Input.MouseDelta.Y * Settings.LookSensitivity, -FPerspectiveCamera::MaxPitch,
		                     FPerspectiveCamera::MaxPitch);

		// Yaw is wrapped to keep it bounded during a long session; the value is otherwise free to grow
		// without limit and would eventually lose precision.
		if (Result.Yaw > Pi || Result.Yaw < -Pi)
		{
			Result.Yaw -= TwoPi * std::floor((Result.Yaw + Pi) / TwoPi);
		}

		FVector3 Direction = FVector3::Zero();
		const FVector3 Forward = GetForwardFor(Result.Yaw, Result.Pitch);
		const FVector3 Right = GetRightFor(Result.Yaw, Result.Pitch);

		if (Input.bForward)
		{
			Direction = Direction + Forward;
		}
		if (Input.bBackward)
		{
			Direction = Direction - Forward;
		}
		if (Input.bRight)
		{
			Direction = Direction + Right;
		}
		if (Input.bLeft)
		{
			Direction = Direction - Right;
		}
		// Vertical movement follows the world axis, not the camera's own up. Unity does the same, and it is
		// what makes QE predictable while looking up or down.
		if (Input.bUp)
		{
			Direction = Direction + FVector3::UnitY();
		}
		if (Input.bDown)
		{
			Direction = Direction - FVector3::UnitY();
		}

		const float DirectionLength = Direction.Length();
		if (DirectionLength > SmallNumber)
		{
			// Normalized so that pressing two keys does not travel faster than one.
			const float Speed = Result.MoveSpeed * (Input.bSprint ? Settings.SprintMultiplier : 1.0f);
			Result.Position = Motion.Position + Direction * (Speed * Input.DeltaSeconds / DirectionLength);
		}

		return Result;
	}

	void FFlyCameraController::SyncFromCamera(const FPerspectiveCamera& Camera)
	{
		Motion.Position = Camera.GetPosition();
		Motion.Yaw = Camera.GetYaw();
		Motion.Pitch = Camera.GetPitch();
	}

	void FFlyCameraController::Update(FPerspectiveCamera& Camera, FWindow& Window, float DeltaSeconds, bool bCanStartFlying)
	{
		const FInput& Input = FInput::Get();

		FFlyCameraInput Frame;
		Frame.bCanStartFlying = bCanStartFlying;
		Frame.bRightButtonDown = Input.IsMouseButtonDown(EMouseButton::Right);
		Frame.bForward = Input.IsKeyDown(EKey::W);
		Frame.bBackward = Input.IsKeyDown(EKey::S);
		Frame.bLeft = Input.IsKeyDown(EKey::A);
		Frame.bRight = Input.IsKeyDown(EKey::D);
		Frame.bUp = Input.IsKeyDown(EKey::E);
		Frame.bDown = Input.IsKeyDown(EKey::Q);
		Frame.bSprint = Input.IsKeyDown(EKey::LeftShift);
		Frame.MouseDelta = Input.GetMouseDelta();
		Frame.ScrollDelta = Input.GetScrollDelta();
		Frame.DeltaSeconds = DeltaSeconds;

		const bool bWasFlying = Motion.bIsFlying;
		Motion = Step(Motion, Frame, Settings);

		// Written back every frame, not only while flying, so that a camera moved by something else stays
		// consistent with what the controller believes.
		Camera.SetPosition(Motion.Position);
		Camera.SetRotation(Motion.Yaw, Motion.Pitch);

		if (Motion.bIsFlying != bWasFlying)
		{
			// SetCursorMode reseeds the cached cursor position, so the warp GLFW performs on capture does not
			// appear as a large delta and the view does not snap on the transition frame.
			Window.SetCursorMode(Motion.bIsFlying ? ECursorMode::Captured : ECursorMode::Normal);
		}
	}

	void FFlyCameraController::Release(FWindow& Window)
	{
		Motion.bIsFlying = false;
		// Unconditional, because the cursor must never be left hidden: SetCursorMode already ignores a mode
		// that is already active.
		Window.SetCursorMode(ECursorMode::Normal);
	}
} // namespace Lime
