// Owns the ImGui context, the dock layout and the panel list.
//
// Engine panels are created here; project panels come from FEditorPanelRegistry. The default dock
// layout is derived from each panel's declared slot, so a new project panel needs no engine change.

#pragma once

#include "Editor/EditorContext.h"
#include "Editor/Panels/EditorPanel.h"
#include "Editor/Panels/ViewportPanel.h"

#include <imgui.h>

#include <memory>
#include <string>
#include <vector>

namespace Lime
{
	class FRenderer;
	class FWindow;
	struct FProjectSettings;

	class FEditorLayer
	{
	public:
		FEditorLayer() = default;
		~FEditorLayer();

		LIME_NON_COPYABLE(FEditorLayer);
		LIME_NON_MOVABLE(FEditorLayer);

		// Creates the ImGui context and installs the GLFW platform backend. The renderer backend is
		// registered separately as a render pass. Routes the scene into a viewport panel.
		// Settings are passed so the project settings panel can edit and persist them.
		bool Initialize(FWindow& Window, FRenderer& Renderer, const FProjectSettings& Settings);
		void Shutdown();

		void BeginFrame();
		// Builds the dock space, the menu bar and every visible panel.
		void DrawUI(const FEditorContext& Context);
		// Finalizes the draw data; the ImGui render pass consumes it later in the frame.
		void EndFrame();

		// Forwards the viewport panel's measured size to the renderer. Called once per frame, after
		// the UI was built.
		void SubmitViewportSize();

		// Looks a panel up by its name, for the rare case a project needs to reach one.
		IEditorPanel* FindPanel(const char* Name) const;
		const std::vector<std::shared_ptr<IEditorPanel>>& GetPanels() const { return Panels; }

		// Discards the saved arrangement and rebuilds the default layout on the next frame.
		void RequestLayoutReset()
		{
			bLayoutBuilt = false;
			bHasSavedLayout = false;
		}

	private:
		void ApplyDarkTheme();
		void CreatePanels(FWindow& Window, const FProjectSettings& Settings);
		void DrawDockSpace();
		void BuildDefaultLayout(ImGuiID DockSpaceId, const ImVec2& DockSize);
		void DrawMenuBar();
		// Rebinds the scene texture after the viewport target was recreated.
		void RefreshViewportTexture();

		std::vector<std::shared_ptr<IEditorPanel>> Panels;
		std::shared_ptr<FViewportPanel> ViewportPanel;
		FRenderer* Renderer = nullptr;
		ImTextureID ViewportTextureId = ImTextureID_Invalid;
		std::string LayoutFilePath;
		bool bInitialized = false;
		// True when an ini file already existed, so the default layout must not overwrite it.
		bool bHasSavedLayout = false;
		bool bLayoutBuilt = false;
		bool bShowDemoWindow = false;
	};
} // namespace Lime
