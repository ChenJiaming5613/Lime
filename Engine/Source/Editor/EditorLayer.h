// Owns the ImGui context, the dock layout and the panel list.
//
// Engine panels are created here; project panels come from FEditorPanelRegistry. The default dock
// layout is derived from each panel's declared slot, so a new project panel needs no engine change.

#pragma once

#include "Editor/EditorContext.h"
#include "Editor/Panels/EditorPanel.h"

#include <imgui.h>

#include <memory>
#include <string>
#include <vector>

namespace Lime
{
	class FRenderer;
	class FWindow;

	class FEditorLayer
	{
	public:
		FEditorLayer() = default;
		~FEditorLayer();

		LIME_NON_COPYABLE(FEditorLayer);
		LIME_NON_MOVABLE(FEditorLayer);

		// Creates the ImGui context and installs the GLFW platform backend. The renderer backend is
		// registered separately as a render pass.
		bool Initialize(FWindow& Window);
		void Shutdown();

		void BeginFrame();
		// Builds the dock space, the menu bar and every visible panel.
		void DrawUI(const FEditorContext& Context);
		// Finalizes the draw data; the ImGui render pass consumes it later in the frame.
		void EndFrame();

		// Looks a panel up by its name, for the rare case a project needs to reach one.
		IEditorPanel* FindPanel(const char* Name) const;

	private:
		void ApplyDarkTheme();
		void CreatePanels();
		void DrawDockSpace();
		void BuildDefaultLayout(ImGuiID DockSpaceId, const ImVec2& DockSize);
		void DrawMenuBar();

		std::vector<std::shared_ptr<IEditorPanel>> Panels;
		std::string LayoutFilePath;
		bool bInitialized = false;
		// True when an ini file already existed, so the default layout must not overwrite it.
		bool bHasSavedLayout = false;
		bool bLayoutBuilt = false;
		bool bShowDemoWindow = false;
	};
} // namespace Lime
