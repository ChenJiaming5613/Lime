// Owns the ImGui context, the dock layout and the panel list.

#pragma once

#include "Editor/EditorContext.h"
#include "Editor/Panels/ConsolePanel.h"
#include "Editor/Panels/InspectorPanel.h"

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
		bool Initialize(FWindow& Window, FRenderer& Renderer);
		void Shutdown();

		void BeginFrame();
		// Builds the dock space, the menu bar and every visible panel.
		void DrawUI(const FEditorContext& Context);
		// Finalizes the draw data; the ImGui render pass consumes it later in the frame.
		void EndFrame();

		FConsolePanel& GetConsolePanel() { return *ConsolePanel; }
		FInspectorPanel& GetInspectorPanel() { return *InspectorPanel; }

	private:
		void ApplyDarkTheme();
		void DrawDockSpace();
		void DrawMenuBar();

		std::vector<std::shared_ptr<IEditorPanel>> Panels;
		std::shared_ptr<FConsolePanel> ConsolePanel;
		std::shared_ptr<FInspectorPanel> InspectorPanel;
		std::string LayoutFilePath;
		bool bInitialized = false;
		// True when an ini file already existed, so the default layout must not overwrite it.
		bool bHasSavedLayout = false;
		bool bLayoutBuilt = false;
		bool bShowDemoWindow = false;
	};
} // namespace Lime
