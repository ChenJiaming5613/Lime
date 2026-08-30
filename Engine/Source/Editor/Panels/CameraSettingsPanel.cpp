#include "Editor/Panels/CameraSettingsPanel.h"

#include "Camera/PerspectiveCamera.h"
#include "Core/Math/MathUtils.h"

#include <imgui.h>

namespace Lime
{
	void FCameraSettingsPanel::OnDrawUI(const FEditorContext& Context)
	{
		if (!ImGui::Begin(GetName(), GetVisiblePtr()))
		{
			ImGui::End();
			return;
		}

		ICamera* Camera = Context.Renderer != nullptr ? Context.Renderer->GetCamera() : nullptr;
		if (Camera == nullptr || Camera->GetProjectionType() != ECameraProjection::Perspective)
		{
			// An orthographic or cinematic camera is a different ICamera implementation with no field of
			// view, so there is nothing this panel can edit.
			ImGui::TextDisabled("No perspective camera");
			ImGui::End();
			return;
		}

		FPerspectiveCamera* Perspective = static_cast<FPerspectiveCamera*>(Camera);

		// Edited in degrees because that is how the value is hand written in ProjectSettings.json.
		float FovDegrees = RadiansToDegrees(Perspective->GetFieldOfView());
		if (ImGui::SliderFloat("Field of View", &FovDegrees, 1.0f, 179.0f, "%.1f deg"))
		{
			Perspective->SetFieldOfView(DegreesToRadians(FovDegrees));
		}

		ImGui::Separator();

		// Near and far are one contract (far must stay beyond near), so they are read together and both
		// written together; SetClipPlanes clamps whichever side would otherwise break the projection.
		float NearPlane = Perspective->GetNearPlane();
		float FarPlane = Perspective->GetFarPlane();
		ImGui::SetNextItemWidth(-1.0f);
		const bool bNearChanged = ImGui::DragFloat("Near Plane", &NearPlane, 0.01f, 0.001f, 100.0f, "%.3f");
		ImGui::SetNextItemWidth(-1.0f);
		const bool bFarChanged = ImGui::DragFloat("Far Plane", &FarPlane, 10.0f, 1.0f, 100000.0f, "%.1f");
		if (bNearChanged || bFarChanged)
		{
			Perspective->SetClipPlanes(NearPlane, FarPlane);
		}

		ImGui::End();
	}
} // namespace Lime
