#include "TrianglePanel.h"

#include "Core/Logging/LogManager.h"
#include "Editor/EditorPanelRegistry.h"

#include "TrianglePass.h"

#include <imgui.h>

namespace HelloTriangle
{
	void FTrianglePanel::OnDrawUI(const Lime::FEditorContext& Context)
	{
		if (!ImGui::Begin(GetName(), GetVisiblePtr()))
		{
			ImGui::End();
			return;
		}

		// Typed lookup, so the panel and the pass have no construction time dependency and either
		// one can be absent.
		FTrianglePass* Pass = Context.FindPass<FTrianglePass>();
		if (Pass == nullptr)
		{
			ImGui::TextDisabled("Triangle pass is not registered");
			ImGui::End();
			return;
		}

		ImGui::SeparatorText("State");
		ImGui::Text("Angle %.2f rad", Pass->GetRotationRadians());
		ImGui::Text("%s", Pass->GetSettings().bPaused ? "Paused" : "Rotating");

		ImGui::Spacing();
		if (ImGui::Button("Reset settings"))
		{
			Pass->GetSettings() = FTriangleSettings{};
			LIME_LOG_INFO(LIME_LOG_CATEGORY_APP, "Triangle settings reset");
		}

		ImGui::Spacing();
		ImGui::TextDisabled("Tunables are in the Inspector panel,\ngenerated from reflection.");

		ImGui::End();
	}
} // namespace HelloTriangle

LIME_REGISTER_EDITOR_PANEL(HelloTriangle::FTrianglePanel);
