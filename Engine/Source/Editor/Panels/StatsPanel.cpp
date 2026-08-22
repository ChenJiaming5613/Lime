#include "Editor/Panels/StatsPanel.h"

#include <imgui.h>

namespace Lime
{
	void FStatsPanel::OnDrawUI(const FEditorContext& Context)
	{
		if (!ImGui::Begin(GetName(), GetVisiblePtr()))
		{
			ImGui::End();
			return;
		}

		const float FrameTimeMs = Context.DeltaSeconds * 1000.0f;
		FrameTimeHistory[HistoryOffset] = FrameTimeMs;
		HistoryOffset = (HistoryOffset + 1) % HistorySize;

		ImGui::SeparatorText("Device");
		ImGui::Text("Backend  %s", Context.BackendName);
		ImGui::TextWrapped("Adapter  %s", Context.AdapterName.empty() ? "Unknown" : Context.AdapterName.c_str());
		ImGui::Text("Viewport %ux%u", Context.ViewportWidth, Context.ViewportHeight);

		ImGui::SeparatorText("Frame");
		ImGui::Text("%.1f FPS (%.3f ms)", Context.FramesPerSecond, FrameTimeMs);
		ImGui::PlotLines("##FrameTime", FrameTimeHistory, HistorySize, HistoryOffset, nullptr, 0.0f, 33.3f, ImVec2(0.0f, 48.0f));

		ImGui::End();
	}
} // namespace Lime
