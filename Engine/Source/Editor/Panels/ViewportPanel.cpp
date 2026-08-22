#include "Editor/Panels/ViewportPanel.h"

namespace Lime
{
	void FViewportPanel::OnDrawUI(const FEditorContext& Context)
	{
		LIME_UNUSED(Context);

		// No padding, so the image lines up with the panel edges.
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const bool bOpen = ImGui::Begin(GetName(), GetVisiblePtr());
		ImGui::PopStyleVar();

		if (!bOpen)
		{
			ImGui::End();
			return;
		}

		const ImVec2 Available = ImGui::GetContentRegionAvail();
		// A collapsed or tabbed-out panel reports zero; keeping the last size avoids a pointless
		// rebuild when it becomes visible again.
		if (Available.x >= 1.0f && Available.y >= 1.0f)
		{
			DesiredWidth = static_cast<uint32>(Available.x);
			DesiredHeight = static_cast<uint32>(Available.y);
		}

		if (TextureId != ImTextureID_Invalid && Available.x >= 1.0f && Available.y >= 1.0f)
		{
			ImGui::Image(TextureId, Available);
		}
		else
		{
			ImGui::TextDisabled("No scene texture");
		}

		bHovered = ImGui::IsItemHovered();

		ImGui::End();
	}
} // namespace Lime
