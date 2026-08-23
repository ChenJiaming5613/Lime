#include "Camera/PerspectiveCamera.h"

#include <cmath>

namespace Lime
{
	FQuat FPerspectiveCamera::GetRotation() const
	{
		// No roll term, which is what keeps the horizon level however far the camera has turned.
		return FQuat::FromYawPitchRoll(Yaw, Pitch, 0.0f);
	}

	FVector3 FPerspectiveCamera::GetForward() const
	{
		// Left handed, so +Z is forward before any rotation is applied.
		return GetRotation().RotateVector(FVector3::UnitZ());
	}

	FVector3 FPerspectiveCamera::GetRight() const
	{
		return GetRotation().RotateVector(FVector3::UnitX());
	}

	FVector3 FPerspectiveCamera::GetUp() const
	{
		return GetRotation().RotateVector(FVector3::UnitY());
	}

	FMatrix4x4 FPerspectiveCamera::GetViewMatrix() const
	{
		// LookAtLH rather than inverting the world matrix: it is both cheaper and exact, where a general
		// inverse accumulates error.
		return FMatrix4x4::LookAtLH(Position, Position + GetForward(), FVector3::UnitY());
	}

	FMatrix4x4 FPerspectiveCamera::GetProjectionMatrix() const
	{
		return FMatrix4x4::PerspectiveFovLH(FovYRadians, AspectRatio, NearPlane, FarPlane);
	}

	FMatrix4x4 FPerspectiveCamera::GetWorldMatrix() const
	{
		return MakeTransform(Position, GetRotation(), FVector3::One());
	}

	void FPerspectiveCamera::SetAspectRatio(float InAspectRatio)
	{
		// A zero or negative ratio would divide by zero inside the projection. This happens in practice
		// when a viewport is collapsed to nothing, so it is ignored rather than treated as an error.
		if (InAspectRatio > SmallNumber)
		{
			AspectRatio = InAspectRatio;
		}
	}

	void FPerspectiveCamera::SetFieldOfView(float InFovYRadians)
	{
		// At or beyond 180 degrees the projection matrix degenerates and nothing is drawn, so the value is
		// clamped rather than trusted.
		FovYRadians = Clamp(InFovYRadians, MinFieldOfView, MaxFieldOfView);
	}

	void FPerspectiveCamera::SetClipPlanes(float InNearPlane, float InFarPlane)
	{
		// A near plane at zero makes the perspective divide undefined; far behind near inverts the depth
		// range and hides everything. Both are silently corrected because a bad configuration file must not
		// leave a black viewport with no explanation.
		NearPlane = InNearPlane > SmallNumber ? InNearPlane : SmallNumber;
		FarPlane = InFarPlane > NearPlane ? InFarPlane : NearPlane + 1.0f;
	}

	void FPerspectiveCamera::SetRotation(float InYawRadians, float InPitchRadians)
	{
		Yaw = InYawRadians;
		Pitch = Clamp(InPitchRadians, -MaxPitch, MaxPitch);

		// Yaw is wrapped so it stays bounded over a long session; it is otherwise free to grow without
		// limit and would eventually lose precision.
		if (Yaw > Pi || Yaw < -Pi)
		{
			Yaw -= TwoPi * std::floor((Yaw + Pi) / TwoPi);
		}
	}

	void FPerspectiveCamera::LookAt(const FVector3& Target)
	{
		const FVector3 Direction = Target - Position;
		const float Length = Direction.Length();
		if (Length <= SmallNumber)
		{
			// The target coincides with the camera, so any angle would be arbitrary; the current aim is kept
			// instead of producing NaNs.
			return;
		}

		const FVector3 Forward = Direction * (1.0f / Length);

		// Derived from the same convention GetForward() uses, so reading these angles back and applying them
		// reproduces this direction exactly.
		SetRotation(std::atan2(Forward.X, Forward.Z), -std::asin(Clamp(Forward.Y, -1.0f, 1.0f)));
	}

	void FPerspectiveCamera::FrameSphere(const FVector3& Center, float Radius, const FVector3& Direction)
	{
		const FVector3 Unit = Direction.GetNormalized();
		const FVector3 Offset = Unit == FVector3::Zero() ? FVector3::UnitZ() : Unit;

		// Distance at which a sphere of this radius exactly fills the vertical field of view, with a margin
		// so the model does not touch the edges of the frame.
		const float SafeRadius = Radius > SmallNumber ? Radius : 1.0f;
		const float Distance = SafeRadius / std::tan(FovYRadians * 0.5f) * 1.5f;

		Position = Center - Offset * Distance;
		LookAt(Center);

		// The far plane has to reach past the model, or a large one would be clipped away. Near is scaled
		// down with it to keep the depth precision ratio reasonable.
		SetClipPlanes(Distance * 0.001f, Distance + SafeRadius * 4.0f);
	}
} // namespace Lime
