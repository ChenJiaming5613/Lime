#include "Editor/Panels/InspectorPanel.h"

#include "Editor/PropertyDrawer.h"
#include "Renderer/Renderer.h"

#include <imgui.h>

namespace Lime
{
	void FInspectorPanel::OnDrawUI(const FEditorContext& Context)
	{
		if (!ImGui::Begin(GetName(), GetVisiblePtr()))
		{
			ImGui::End();
			return;
		}

		if (Context.Renderer == nullptr)
		{
			ImGui::TextDisabled("No renderer");
			ImGui::End();
			return;
		}

		int32 DrawnCount = 0;
		for (const std::shared_ptr<IRenderPass>& Pass : Context.Renderer->GetPasses())
		{
			const FReflectedRef Settings = Pass->GetReflectedSettings();
			if (!Settings.IsValid())
			{
				// Passes without reflected settings simply do not appear.
				continue;
			}

			++DrawnCount;
			// Unique ID per pass so two passes with the same header label stay independent.
			ImGui::PushID(Pass.get());
			if (ImGui::CollapsingHeader(Pass->GetName(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				FPropertyDrawer::Draw(Settings);
			}
			ImGui::PopID();
		}

		if (DrawnCount == 0)
		{
			ImGui::TextWrapped("No render pass exposes reflected settings.");
			ImGui::Spacing();
			ImGui::TextDisabled("Add LIME_REFLECT to a settings struct and return it from\nGetReflectedSettings to populate this panel.");
		}

		ImGui::End();
	}
} // namespace Lime
