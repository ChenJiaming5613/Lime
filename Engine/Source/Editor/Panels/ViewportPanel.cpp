#include "Editor/Panels/ViewportPanel.h"

#include <imgui.h>

#include <cstdio>

namespace Lime
{
	namespace
	{
		// Centres a short message over the viewport.
		//
		// Drawn as an overlay rather than in place of the image so it works whether or not a scene texture
		// exists yet: the first load has nothing to draw, while a later one still shows the previous scene.
		void DrawCentredOverlay(const ImVec2& TopLeft, const ImVec2& Size, const char* Text)
		{
			const ImVec2 TextSize = ImGui::CalcTextSize(Text);
			const ImVec2 Position(TopLeft.x + (Size.x - TextSize.x) * 0.5f, TopLeft.y + (Size.y - TextSize.y) * 0.5f);

			ImDrawList* DrawList = ImGui::GetWindowDrawList();

			// A backing rectangle, because white text over a bright model can be unreadable and the message
			// is the one thing on screen that has to be legible.
			const ImVec2 Padding(12.0f, 8.0f);
			DrawList->AddRectFilled(ImVec2(Position.x - Padding.x, Position.y - Padding.y),
			                        ImVec2(Position.x + TextSize.x + Padding.x, Position.y + TextSize.y + Padding.y),
			                        IM_COL32(0, 0, 0, 160), 4.0f);
			DrawList->AddText(Position, IM_COL32(255, 255, 255, 230), Text);
		}
	} // namespace

	void FViewportPanel::OnDrawUI(const FEditorContext& Context)
	{
		// No padding, so the image lines up with the panel edges.
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		const bool bOpen = ImGui::Begin(GetName(), GetVisiblePtr());
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
			ImGui::Image(TextureId, Available);
		}
		else
		{
			ImGui::TextDisabled("No scene texture");
		}

		bHovered = ImGui::IsItemHovered();

		// After the image, so the message sits on top of it.
		//
		// Says what is happening and for how long, because a large scene takes long enough that a static
		// label leaves the same question a blank viewport did: whether anything is still going on. The
		// elapsed seconds are what distinguish loading from hung.
		if (Context.bSceneLoading && Available.x >= 1.0f && Available.y >= 1.0f)
		{
			char Message[256];
			const char* const Name = Context.SceneLoadFileName.empty() ? "scene" : Context.SceneLoadFileName.c_str();
			std::snprintf(Message, sizeof(Message), "Loading %s...  %.1fs", Name, Context.SceneLoadSeconds);
			DrawCentredOverlay(ContentTopLeft, Available, Message);
		}

		ImGui::End();
	}
} // namespace Lime
