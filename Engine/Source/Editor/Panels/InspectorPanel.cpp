#include "Editor/Panels/InspectorPanel.h"

#include <imgui.h>

namespace Lime
{
	void FInspectorPanel::OnDrawUI(const FEditorContext& Context)
	{
		if (!bVisible)
		{
			return;
		}

		if (!ImGui::Begin(GetName(), &bVisible))
		{
			ImGui::End();
			return;
		}

		if (ImGui::CollapsingHeader("Frame", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Backend    %s", Context.BackendName);
			ImGui::TextWrapped("Adapter    %s", Context.AdapterName.c_str());
			ImGui::Text("Resolution %u x %u", Context.ViewportWidth, Context.ViewportHeight);
			ImGui::Text("Frame rate %.1f FPS", Context.FramesPerSecond);
			ImGui::Text("Frame time %.3f ms", Context.DeltaSeconds * 1000.0f);
		}

		if (DrawDelegate)
		{
			ImGui::Spacing();
			DrawDelegate();
		}

		ImGui::End();
	}
} // namespace Lime
