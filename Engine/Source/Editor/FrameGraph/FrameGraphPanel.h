// Frame graph inspector.
//
// Loads a graph from JSON, lays it out, draws it and writes edits back. Modelled on Falcor's render graph
// editor: a pass per node, a labelled pin per declared resource, and edges connecting pins.
//
// The graph in FFrameGraphDesc is the only record. The layout is derived from it on load and after an
// explicit relayout, and the node widget owns positions in between so a drag survives. Nothing about the
// display is saved, which is why there is no path by which the file and the picture can disagree.

#pragma once

#include "Editor/FrameGraph/FrameGraphNodeView.h"
#include "Editor/Panels/EditorPanel.h"

#include "FrameGraph/FrameGraphJson.h"

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
	class FFrameGraphPanel final : public IEditorPanel
	{
	public:
		FFrameGraphPanel();
		~FFrameGraphPanel() override;

		LIME_NON_COPYABLE(FFrameGraphPanel);
		LIME_NON_MOVABLE(FFrameGraphPanel);

		// Stable across runs, since it doubles as the ImGui window title used for layout persistence.
		const char* GetName() const override { return "Frame Graph"; }
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

		const FFrameGraphDesc& GetGraph() const { return Graph; }
		const FFramePassTypeRegistry& GetPassTypes() const { return PassTypes; }
		const std::vector<FFrameGraphIssue>& GetIssues() const { return Issues; }
		const std::string& GetLastLoadedPath() const { return PathBuffer; }

		// Default locations, exposed so the automation commands and the UI agree on where graphs live.
		static std::filesystem::path GetDefaultLoadDirectory();
		static std::filesystem::path GetDefaultSaveDirectory();

	private:
		void EnsureEditorContext();
		void DrawToolbar();
		void DrawPassTypeList();
		void DrawGraphCanvas();
		void DrawSelectionDetails();
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

		FFrameGraphDesc Graph;
		FFramePassTypeRegistry PassTypes;
		std::vector<FFrameGraphNodePlacement> Placements;
		std::vector<FFrameGraphIssue> Issues;
		FFrameGraphIdMap Ids;

		// Owned rather than borrowed from the demo panel: two canvases sharing one context would share
		// selection and view state.
		ax::NodeEditor::EditorContext* EditorContext = nullptr;

		// Selection is local. FEditorSelection holds an entt::entity, which a pass is not.
		std::string SelectedPass;

		// Set for one frame after a load or a relayout. Positions are pushed to the widget only then.
		bool bApplyPositions = false;
		// Frames the canvas has been drawn at a usable size. The initial fit waits on this rather than on
		// frame count alone, since a panel that is not yet laid out reports no space and the fit would
		// settle on a zoom that makes every node a few pixels wide.
		int32 FramesSinceFirstDraw = 0;
		ImVec2 CanvasSize{ 0.0f, 0.0f };
		bool bPendingFit = false;

		// Pending edits, drained by ApplyPendingEdits.
		std::string PassToRemove;
		std::string TypeToAdd;
		std::vector<SizeType> EdgesToRemove;
		FFrameGraphResourceRef OutputToToggle;
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
