// Camera interface.
//
// Rendering only ever needs four things from a camera: a view matrix, a projection matrix, where it is
// and which way it faces. Everything else is specific to one kind of camera, so it stays out of this
// interface. That is what allows an orthographic, cinematic or split screen camera to be added later
// without touching a single call site.
//
// Deliberately free of any perspective specific member. Aspect ratio and the near and far planes are
// here because every projection needs them, but field of view is not: an orthographic camera has no
// such concept, and putting it here would force every future camera to carry a meaningless value.
//
// Cameras are data, not controllers. Input handling lives in a separate controller type so that any
// control scheme can drive any camera, and so the control rules stay unit testable without a window.

#pragma once

#include "Core/CoreTypes.h"
#include "Core/Math/Matrix.h"
#include "Core/Math/Vector.h"

namespace Lime
{
	enum class ECameraProjection : uint8
	{
		Perspective,
		Orthographic
	};

	class ICamera
	{
	public:
		virtual ~ICamera() = default;

		// Lets a panel or an automation command report what kind of camera it is looking at without
		// resorting to a dynamic_cast.
		virtual ECameraProjection GetProjectionType() const = 0;

		// World to camera space.
		virtual FMatrix4x4 GetViewMatrix() const = 0;
		// Camera space to clip space. Depth range is [0, 1] to match both backends.
		virtual FMatrix4x4 GetProjectionMatrix() const = 0;

		virtual FVector3 GetPosition() const = 0;
		// Unit vector the camera looks along. Needed for specular lighting and for placing objects in
		// front of the view.
		virtual FVector3 GetForward() const = 0;

		// Set from the render target size every frame; the viewport can be resized at any time.
		virtual void SetAspectRatio(float AspectRatio) = 0;
		virtual float GetAspectRatio() const = 0;

		virtual float GetNearPlane() const = 0;
		virtual float GetFarPlane() const = 0;

		// Not virtual: the composition order is a property of the math convention, not of the camera.
		// Reads right to left, so the view is applied before the projection.
		FMatrix4x4 GetViewProjectionMatrix() const { return Multiply(GetProjectionMatrix(), GetViewMatrix()); }

	protected:
		// Copying through a base reference would slice the derived state, so these are available to
		// derived classes but not to callers holding an ICamera.
		ICamera() = default;
		ICamera(const ICamera&) = default;
		ICamera(ICamera&&) = default;
		ICamera& operator=(const ICamera&) = default;
		ICamera& operator=(ICamera&&) = default;
	};
} // namespace Lime
