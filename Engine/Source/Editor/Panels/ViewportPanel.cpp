#include "Editor/Panels/ViewportPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace Lime
{
	namespace
	{
		// Centres a loading message over the viewport, with a bar when the phase can be counted.
		//
		// Drawn as an overlay rather than in place of the image so it works whether or not a scene texture
		// exists yet: the first load has nothing to draw, while a later one still shows the previous scene.
		//
		// Two forms, because the phases differ in what they can report. Textures and meshes are loops the
		// importer owns, so they give a real count and get a bar. Parsing is one call into tinygltf that
		// returns only when finished, so it can offer nothing but elapsed time; showing a bar there would
		// mean animating a number that is made up.
		void DrawLoadingOverlay(const FEditorContext& Context, const ImVec2& TopLeft, const ImVec2& Size)
		{
			const char* const Name = Context.SceneLoadFileName.empty() ? "scene" : Context.SceneLoadFileName.c_str();
			const bool bCountable = Context.SceneLoadTotal > 0;

			char Title[256];
			std::snprintf(Title, sizeof(Title), "Loading %s", Name);

			char Detail[128];
			if (bCountable)
			{
				std::snprintf(Detail, sizeof(Detail), "%s  %u / %u", Context.SceneLoadPhase, Context.SceneLoadDone, Context.SceneLoadTotal);
			}
			else
			{
				std::snprintf(Detail, sizeof(Detail), "%s  %.1fs", Context.SceneLoadPhase, Context.SceneLoadSeconds);
			}

			const ImVec2 TitleSize = ImGui::CalcTextSize(Title);
			const ImVec2 DetailSize = ImGui::CalcTextSize(Detail);
			const float BarHeight = bCountable ? 6.0f : 0.0f;
			const float Spacing = 6.0f;

			const float BlockWidth = std::max(160.0f, std::max(TitleSize.x, DetailSize.x));
			const float BlockHeight = TitleSize.y + Spacing + DetailSize.y + (bCountable ? Spacing + BarHeight : 0.0f);

			const ImVec2 Origin(TopLeft.x + (Size.x - BlockWidth) * 0.5f, TopLeft.y + (Size.y - BlockHeight) * 0.5f);

			ImDrawList* DrawList = ImGui::GetWindowDrawList();

			// A backing rectangle, because white text over a bright model can be unreadable and the message
			// is the one thing on screen that has to be legible.
			const ImVec2 Padding(16.0f, 12.0f);
			DrawList->AddRectFilled(ImVec2(Origin.x - Padding.x, Origin.y - Padding.y),
			                        ImVec2(Origin.x + BlockWidth + Padding.x, Origin.y + BlockHeight + Padding.y), IM_COL32(0, 0, 0, 170),
			                        4.0f);

			DrawList->AddText(ImVec2(Origin.x + (BlockWidth - TitleSize.x) * 0.5f, Origin.y), IM_COL32(255, 255, 255, 235), Title);

			const float DetailY = Origin.y + TitleSize.y + Spacing;
			DrawList->AddText(ImVec2(Origin.x + (BlockWidth - DetailSize.x) * 0.5f, DetailY), IM_COL32(200, 200, 200, 220), Detail);

			if (bCountable)
			{
				const float BarY = DetailY + DetailSize.y + Spacing;
				const float Fraction = static_cast<float>(Context.SceneLoadDone) / static_cast<float>(Context.SceneLoadTotal);

				DrawList->AddRectFilled(ImVec2(Origin.x, BarY), ImVec2(Origin.x + BlockWidth, BarY + BarHeight),
				                        IM_COL32(255, 255, 255, 40), 2.0f);
				DrawList->AddRectFilled(ImVec2(Origin.x, BarY), ImVec2(Origin.x + BlockWidth * Fraction, BarY + BarHeight),
				                        IM_COL32(143, 212, 89, 230), 2.0f);
			}
		}
	} // namespace

	uint32 FViewportPanel::ViewportCount = 0;

	FViewportPanel::FViewportPanel()
	{
		Name = std::format("Viewport{}", ViewportCount);
		ViewportIndex = ViewportCount;
		ViewportCount++;
	}

	void FViewportPanel::OnDrawUI(const FEditorContext& Context)
	{
		// No padding, so the image lines up with the panel edges.
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		// The panel is a fixed image, not a scrollable surface. Without disabling the scrollbar, a
		// secondary viewport whose aspect-scaled image is taller than the panel pushes content past the
		// bottom edge, which makes ImGui show a vertical scrollbar. That scrollbar narrows the available
		// width, which shrinks the width-scaled image height and can bring it back inside the panel,
		// hiding the scrollbar again — a feedback loop that reads as a jittering scrollbar.
		constexpr ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_NoScrollbar;
		const bool bOpen = ImGui::Begin(GetName(), GetVisiblePtr(), WindowFlags);
		ImGui::PopStyleVar();

		if (!bOpen)
		{
			ImGui::End();
			return;
		}

		const ImVec2 ContentTopLeft = ImGui::GetCursorScreenPos();
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
			// The main viewport fills the panel: its texture is recreated to match the panel size, so any
			// mismatch is the one frame a resize takes to apply. Secondary viewports only display their
			// output, so they keep the texture's aspect ratio instead of stretching: the width fills the
			// panel and the height follows the ratio, centred vertically so a shorter image letterboxes and
			// a taller one crops evenly top and bottom.
			if (ViewportIndex == 0 || TextureWidth == 0 || TextureHeight == 0)
			{
				ImGui::Image(TextureId, Available);
			}
			else
			{
				const float DisplayHeight = Available.x * static_cast<float>(TextureHeight) / static_cast<float>(TextureWidth);
				const float OffsetY = (Available.y - DisplayHeight) * 0.5f;
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() + OffsetY);
				ImGui::Image(TextureId, ImVec2(Available.x, DisplayHeight));
			}
		}
		else
		{
			ImGui::TextDisabled("No scene texture");
		}

		bHovered = ImGui::IsItemHovered();

		// After the image, so the message sits on top of it.
		//
		// Reports the phase and, where the importer can count them, how many items it has finished. That is
		// what distinguishes loading from hung, which is the question a blank viewport left unanswered.
		if (Context.bSceneLoading && Available.x >= 1.0f && Available.y >= 1.0f)
		{
			DrawLoadingOverlay(Context, ContentTopLeft, Available);
		}

		ImGui::End();
	}
} // namespace Lime
