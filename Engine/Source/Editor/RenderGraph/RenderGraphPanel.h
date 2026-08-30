// Render graph inspector.
//
// Loads a graph from JSON, lays it out, draws it and writes edits back. Modelled on Falcor's render graph
// editor: a pass per node, a labelled pin per declared resource, and edges connecting pins.
//
// The graph in FRenderGraphDesc is the only record. The layout is derived from it on load and after an
// explicit relayout, and the node widget owns positions in between so a drag survives. Nothing about the
// display is saved, which is why there is no path by which the file and the picture can disagree.

#pragma once

#include "Editor/RenderGraph/RenderGraphNodeView.h"
#include "Editor/Panels/EditorPanel.h"

#include "RenderGraph/RenderGraphJson.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ax::NodeEditor
{
	struct EditorContext;
}

namespace Lime
{
	struct FProjectSettings;
	class FRenderer;

	class FRenderGraphPanel final : public IEditorPanel
	{
	public:
		FRenderGraphPanel();
		~FRenderGraphPanel() override;

		LIME_NON_COPYABLE(FRenderGraphPanel);
		LIME_NON_MOVABLE(FRenderGraphPanel);

		// Opens the graph the project is configured to run, so the panel shows what is actually rendering
		// rather than an unrelated sample.
		//
		// Separate from the constructor because the path comes from the project settings, which the panel
		// has no way to reach on its own.
		void Initialize(const FProjectSettings& ProjectSettings);

		// Stable across runs, since it doubles as the ImGui window title used for layout persistence.
		const char* GetName() const override { return "Render Graph"; }
		// Center: a graph is a workspace, and docked to a side it would be too narrow to follow.
		EEditorDockSlot GetDefaultDockSlot() const override { return EEditorDockSlot::Center; }
		const char* GetMenuCategory() const override { return "Rendering"; }
		// Hidden by default. It inspects the pipeline rather than the scene, so it is not in the way of
		// ordinary work.
		bool IsVisibleByDefault() const override { return false; }

		void OnDrawUI(const FEditorContext& Context) override;

		// Used by the automation commands, so a script can drive the panel without synthesising clicks.
		// Both report what happened rather than only whether it worked, because a load that dropped an edge
		// is neither a success nor a failure.
		bool LoadFromFile(const std::filesystem::path& Path);
		bool SaveToFile(const std::filesystem::path& Path);

		const FRenderGraphDesc& GetGraph() const { return Graph; }

		// Where Save writes, which is not always where the graph was loaded from: a project editing one of
		// the engine's graphs saves its own copy instead of writing back into shared content. Exposed so a
		// test can assert that rather than infer it from the filesystem.
		std::string GetSavePath() const { return PathBuffer; }
		const FRenderGraphPassTypeRegistry& GetPassTypes() const { return PassTypes; }
		const std::vector<FRenderGraphIssue>& GetIssues() const { return Issues; }
		const std::string& GetLastLoadedPath() const { return PathBuffer; }

		// Where the layout put each pass. Exposed so a test can assert on the arrangement: a graph whose
		// nodes all sit at the origin looks identical to one that was never laid out, and nothing else
		// reports the difference.
		const std::vector<FRenderGraphNodePlacement>& GetPlacements() const { return Placements; }

		// Default locations, exposed so the automation commands and the UI agree on where graphs live.
		static std::filesystem::path GetDefaultLoadDirectory();
		static std::filesystem::path GetDefaultSaveDirectory();

	private:
		void EnsureEditorContext();
		void DrawToolbar();
		void DrawPassTypeList();
		void DrawGraphCanvas();
		void DrawSelectionDetails(const FEditorContext& Context);
		void DrawIssueList();

		// Edits requested by the canvas, applied after drawing finishes: adding or removing a pass changes
		// the id map, and doing that mid-draw would leave the widget holding ids that no longer mean
		// anything.
		void ApplyPendingEdits();

		void HandleLinkCreation();
		void HandleDeletions();

		// Recomputes the layout and arranges for it to be applied on the next frame.
		void RequestRelayout();

		// Runs Validate and stores the result for the issue list, so the panel does not revalidate per frame.
		void RefreshIssues();

		// Copies a just saved graph over the one beside the executable, which is what the engine loads.
		// Without it a save would only take effect after a rebuild.
		void RefreshDeployedCopy(const std::filesystem::path& AuthoredPath);

		// Pulls the running instances' current settings back into the description, so a save captures
		// whatever was tuned this session.
		void SyncSettingsFromRenderer();

		FRenderGraphDesc Graph;
		FRenderGraphPassTypeRegistry PassTypes;
		std::vector<FRenderGraphNodePlacement> Placements;
		// Node sizes as the widget last drew them, refreshed every frame and fed back into the layout.
		// Without them the layout has to assume a size, and a node wider than that overlaps its neighbour.
		std::vector<FRenderGraphNodeSize> NodeSizes;
		std::vector<FRenderGraphIssue> Issues;
		FRenderGraphIdMap Ids;

		// Where this project's copy of the graph belongs, and the deployed copy the engine loads. Both come
		// from the project settings and are fixed for the run.
		//
		// Held rather than recomputed per save, because a save has to reach the project even when what is on
		// screen was loaded from the engine's content: writing the project's copy is how it takes over a
		// default it has been running unchanged. Empty in an installed build, where there is no source tree
		// and a save can only go where it is told.
		std::filesystem::path AuthoringPath;
		std::filesystem::path DeployedPath;

		// Owned rather than borrowed from the demo panel: two canvases sharing one context would share
		// selection and view state.
		ax::NodeEditor::EditorContext* EditorContext = nullptr;

		// Selection is local. FEditorSelection holds an entt::entity, which a pass is not.
		std::string SelectedPass;

		// Borrowed from the frame context each draw; used to read running pass settings on save. Null
		// until the panel has been drawn once.
		FRenderer* Renderer = nullptr;

		// Set for one frame after a load or a relayout. Positions are pushed to the widget only then.
		bool bApplyPositions = false;
		// Set when a layout is wanted but the nodes have not been measured yet, which is the case straight
		// after a load: the widget only knows how big a node is once it has drawn it, so laying out before
		// then would use nominal sizes and let a wide node overlap its neighbour. Cleared once the layout
		// has run against real measurements.
		bool bLayoutPendingMeasurement = false;
		// Frames the canvas has been drawn at a usable size. The initial fit waits on this rather than on
		// frame count alone, since a panel that is not yet laid out reports no space and the fit would
		// settle on a zoom that makes every node a few pixels wide.
		int32 FramesSinceFirstDraw = 0;
		ImVec2 CanvasSize{ 0.0f, 0.0f };
		bool bPendingFit = false;

		// Pending edits, drained by ApplyPendingEdits.
		//
		// A list rather than a single name: a box selection deletes every node in it, and the widget
		// reports them one at a time.
		std::vector<std::string> PassesToRemove;
		std::string TypeToAdd;
		std::vector<SizeType> EdgesToRemove;
		FRenderGraphResourceRef OutputToToggle;
		bool bToggleOutputRequested = false;

		// Text fields. Fixed buffers because ImGui::InputText writes into one; sized for a long path.
		char PathBuffer[512] = {};
		char NewPassNameBuffer[128] = {};

		std::string StatusMessage;
		bool bStatusIsError = false;
		// The side columns can be folded away to give the canvas the whole panel.
		bool bShowSideColumns = true;
	};
} // namespace Lime
