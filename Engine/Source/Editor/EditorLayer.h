// Owns the ImGui context, the dock layout and the panel list.
//
// Engine panels are created here; project panels come from FEditorPanelRegistry. The default dock
// layout is derived from each panel's declared slot, so a new project panel needs no engine change.

#pragma once

#include "Editor/EditorContext.h"
#include "Editor/EditorSelection.h"
#include "Editor/EditorSettings.h"
#include "Editor/Panels/CameraSettingsPanel.h"
#include "Editor/Panels/EditorPanel.h"
#include "Editor/Panels/SceneHierarchyPanel.h"
#include "Editor/Panels/ViewportPanel.h"

#if LIME_WITH_IMGUI_TEST_ENGINE
#include "Editor/TestEngine/EditorTestEngine.h"
#endif

#if LIME_WITH_NODE_EDITOR
#include "Editor/RenderGraph/RenderGraphPanel.h"
#endif

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
		// Project settings are passed so the project settings panel can edit and persist them; the
		// editor's own appearance is loaded here from EditorSettings.json.
		bool Initialize(FWindow& Window, FRenderer& Renderer, const FProjectSettings& ProjectSettings);
		void Shutdown();

		void BeginFrame();
		// Builds the dock space, the menu bar and every visible panel.
		void DrawUI(const FEditorContext& Context);
		// Finalizes the draw data; the ImGui render pass consumes it later in the frame.
		void EndFrame();

		// Forwards the viewport panel's measured size to the renderer. Called once per frame, after
		// the UI was built.
		void SubmitViewportSize();

		// True while the cursor is over the scene viewport. Forwarded rather than exposing the panel, so
		// FEngine can gate the fly camera without depending on the panel types.
		bool IsViewportHovered() const;

		// Bracket the swap chain present. Forwarded rather than exposed so FEngine does not need the
		// test engine headers, matching how SubmitViewportSize hides the viewport panel.
		void PreSwap();
		void PostSwap();

		// True while the UI automation wants frames as fast as possible, which is the cue to stop
		// waiting for vsync. Always false without the test engine, so callers need no conditional.
		bool IsRequestingMaxAppSpeed() const;

#if LIME_WITH_IMGUI_TEST_ENGINE
		// Null when the editor failed to start the test engine. Used by the automation commands.
		FEditorTestEngine* GetTestEngine() { return TestEngine.IsInitialized() ? &TestEngine : nullptr; }
#endif

		// Looks a panel up by its name, for the rare case a project needs to reach one.
		IEditorPanel* FindPanel(const char* Name) const;
		const std::vector<std::shared_ptr<IEditorPanel>>& GetPanels() const { return Panels; }

#if LIME_WITH_NODE_EDITOR
		// Null when the editor has not created its panels yet. The automation commands need the concrete
		// type to load and save graphs, which FindPanel cannot give them.
		FRenderGraphPanel* GetRenderGraphPanel() const { return RenderGraphPanel.get(); }
#endif

		// Appearance this session is running with. Loaded during Initialize and never changed
		// afterwards, so it always describes what is actually on screen.
		const FEditorSettings& GetSettings() const { return Settings; }

		// Shared by the hierarchy panel and the inspector, and reachable by automation so a test can select
		// an entity without simulating a click.
		FEditorSelection& GetSelection() { return Selection; }
		const FEditorSelection& GetSelection() const { return Selection; }

		// Discards the saved arrangement and rebuilds the default layout on the next frame.
		void RequestLayoutReset()
		{
			bLayoutBuilt = false;
			bHasSavedLayout = false;
		}

	private:
		void ApplyTheme();
		void CreatePanels(const FProjectSettings& ProjectSettings);
		void DrawDockSpace();
		void BuildDefaultLayout(ImGuiID DockSpaceId, const ImVec2& DockSize);
		void DrawMenuBar();
		// Rebinds the scene texture after the viewport target was recreated.
		void RefreshViewportTexture(SizeType Index);

		std::vector<std::shared_ptr<IEditorPanel>> Panels;
		std::vector<std::shared_ptr<FViewportPanel>> ViewportPanels;
#if LIME_WITH_NODE_EDITOR
		// Held by concrete type so the automation commands can load and save through it. Also in Panels,
		// which is what draws it; this is a second reference, not a second panel.
		std::shared_ptr<FRenderGraphPanel> RenderGraphPanel;
#endif
		// Owned here so it survives the per frame rebuild of FEditorContext; panels receive a pointer.
		FEditorSelection Selection;
		FRenderer* Renderer = nullptr;
		// Appearance from EditorSettings.json, applied once during Initialize.
		FEditorSettings Settings;
#if LIME_WITH_IMGUI_TEST_ENGINE
		FEditorTestEngine TestEngine;
#endif
		std::vector<ImTextureID> ViewportTextureIds;
		std::string LayoutFilePath;
		bool bInitialized = false;
		// True when an ini file already existed, so the default layout must not overwrite it.
		bool bHasSavedLayout = false;
		bool bLayoutBuilt = false;
		bool bShowDemoWindow = false;
#if LIME_WITH_IMGUI_TEST_ENGINE
		bool bShowTestEngineWindow = false;
#endif
	};
} // namespace Lime
