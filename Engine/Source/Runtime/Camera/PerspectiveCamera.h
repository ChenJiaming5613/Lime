// Perspective camera.
//
// Orientation is stored as yaw and pitch rather than a quaternion or a matrix. That is a deliberate
// restriction: it makes clamping the pitch exact, and it makes roll impossible to accumulate, which is
// what keeps the horizon level. A camera that does need roll would be a separate ICamera implementation
// rather than an extra field here.

#pragma once

#include "Core/Math/MathUtils.h"
#include "Core/Math/Quaternion.h"

#include "Camera/Camera.h"

namespace Lime
{
	class FPerspectiveCamera final : public ICamera
	{
	public:
		FPerspectiveCamera() = default;

		ECameraProjection GetProjectionType() const override { return ECameraProjection::Perspective; }

		FMatrix4x4 GetViewMatrix() const override;
		FMatrix4x4 GetProjectionMatrix() const override;

		FVector3 GetPosition() const override { return Position; }
		FVector3 GetForward() const override;

		void SetAspectRatio(float InAspectRatio) override;
		float GetAspectRatio() const override { return AspectRatio; }

		float GetNearPlane() const override { return NearPlane; }
		float GetFarPlane() const override { return FarPlane; }

		void SetPosition(const FVector3& InPosition) { Position = InPosition; }

		// Vertical field of view. Clamped to a sane range, since a value at or beyond 180 degrees makes the
		// projection matrix degenerate.
		void SetFieldOfView(float FovYRadians);
		float GetFieldOfView() const { return FovYRadians; }

		// Far must stay greater than near, or the projection collapses and nothing is drawn.
		void SetClipPlanes(float InNearPlane, float InFarPlane);

		// Pitch is clamped just short of vertical: at exactly 90 degrees the forward vector becomes
		// parallel to the world up axis and the view rolls unpredictably.
		void SetRotation(float InYawRadians, float InPitchRadians);
		float GetYaw() const { return Yaw; }
		float GetPitch() const { return Pitch; }

		// Rotation implied by yaw and pitch, with no roll term.
		FQuat GetRotation() const;
		FVector3 GetRight() const;
		FVector3 GetUp() const;

		// Camera to world. The inverse of the view matrix, useful for placing something in the camera's
		// own space.
		FMatrix4x4 GetWorldMatrix() const;

		// Aims at a target from the current position, deriving yaw and pitch so that subsequent mouse look
		// continues smoothly from here rather than snapping. Used to frame a freshly loaded model.
		void LookAt(const FVector3& Target);

		// Places the camera so a sphere of the given radius fits in view, then aims at its centre. Radius
		// and field of view together decide the distance, so this works for any model size.
		//
		// ViewDirection is the direction the camera will look along, so the camera ends up on the opposite
		// side of the centre. Naming it after the view rather than after the offset avoids the sign
		// confusion: to look down at a model, pass a direction with a negative Y.
		void FrameSphere(const FVector3& Center, float Radius, const FVector3& ViewDirection);

		static constexpr float MinFieldOfView = DegreesToRadians(1.0f);
		static constexpr float MaxFieldOfView = DegreesToRadians(179.0f);
		static constexpr float MaxPitch = DegreesToRadians(89.0f);

	private:
		FVector3 Position{ 0.0f, 0.0f, -5.0f };
		float Yaw = 0.0f;
		float Pitch = 0.0f;

		float FovYRadians = DegreesToRadians(60.0f);
		float AspectRatio = 16.0f / 9.0f;
		float NearPlane = 0.1f;
		float FarPlane = 1000.0f;
	};
} // namespace Lime
