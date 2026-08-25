#include "Editor/FrameGraph/FrameGraphPanel.h"

#include "Core/Logging/LogManager.h"
#include "Platform/PlatformPaths.h"

#include <imgui_node_editor.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ed = ax::NodeEditor;

namespace Lime
{
	namespace
	{
		constexpr ImU32 ErrorColour = IM_COL32(240, 120, 110, 255);
		constexpr ImU32 WarningColour = IM_COL32(235, 195, 100, 255);

		void SetTextBuffer(char* Buffer, SizeType Capacity, const std::string& Value)
		{
			const SizeType Length = std::min(Value.size(), Capacity - 1);
			std::memcpy(Buffer, Value.data(), Length);
			Buffer[Length] = '\0';
		}

		// A name that is not taken yet, so adding a second pass of one type does not fail on the name.
		std::string MakeUniqueName(const FFrameGraphDesc& Graph, const std::string& TypeName)
		{
			if (Graph.FindPass(TypeName) == nullptr)
			{
				return TypeName;
			}

			for (int32 Suffix = 2; Suffix < 1000; ++Suffix)
			{
				const std::string Candidate = TypeName + std::to_string(Suffix);
				if (Graph.FindPass(Candidate) == nullptr)
				{
					return Candidate;
				}
			}

			return TypeName;
		}
	} // namespace

	FFrameGraphPanel::FFrameGraphPanel()
	{
		SetTextBuffer(PathBuffer, sizeof(PathBuffer), (GetDefaultLoadDirectory() / "DeferredExample.json").string());
	}

	FFrameGraphPanel::~FFrameGraphPanel()
	{
		if (EditorContext != nullptr)
		{
			ed::DestroyEditor(EditorContext);
			EditorContext = nullptr;
		}
	}

	std::filesystem::path FFrameGraphPanel::GetDefaultLoadDirectory()
	{
		// Content, because that is where the shipped examples land and it is copied next to the executable.
		return FPlatformPaths::GetContentDirectory() / "FrameGraph";
	}

	std::filesystem::path FFrameGraphPanel::GetDefaultSaveDirectory()
	{
		// Saved rather than Content: a build copies Content out again, so writing there would look like the
		// save had silently reverted.
		return FPlatformPaths::GetSavedDirectory() / "FrameGraph";
	}

	void FFrameGraphPanel::EnsureEditorContext()
	{
		if (EditorContext != nullptr)
		{
			return;
		}

		ed::Config Config;
		// No settings file: the layout is computed from the graph every time it loads. Letting the widget
		// persist positions would create a second record that disagrees with the computed one, with no way
		// to tell which was right.
		Config.SettingsFile = nullptr;
		EditorContext = ed::CreateEditor(&Config);
	}

	bool FFrameGraphPanel::LoadFromFile(const std::filesystem::path& Path)
	{
		const FFrameGraphLoadResult Result = FFrameGraphJson::LoadFromFile(Path, PassTypes, Graph);
		Issues = Result.Issues;

		if (!Result.bSucceeded)
		{
			StatusMessage = "Could not load " + Path.filename().string();
			bStatusIsError = true;
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_EDITOR, "Frame graph load failed: '{}'", Path.string());
			return false;
		}

		SelectedPass.clear();
		Ids.Build(Graph, PassTypes);
		RequestRelayout();
		// Fit after the layout lands, so the view frames the graph rather than wherever it was last.
		bPendingFit = true;
		RefreshIssues();

		const SizeType Dropped = Result.Issues.size();
		StatusMessage = "Loaded " + Path.filename().string() + " (" + std::to_string(Graph.GetPasses().size()) + " passes";
		if (Dropped > 0)
		{
			// Reported on the status line as well as in the list: a graph that opened with elements missing
			// looks like a graph that was authored that way.
			StatusMessage += ", " + std::to_string(Dropped) + " issue(s)";
		}
		StatusMessage += ")";
		bStatusIsError = false;

		SetTextBuffer(PathBuffer, sizeof(PathBuffer), Path.string());
		LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "Loaded frame graph '{}' with {} pass(es) and {} edge(s)", Path.string(),
		              Graph.GetPasses().size(), Graph.GetEdges().size());
		return true;
	}

	bool FFrameGraphPanel::SaveToFile(const std::filesystem::path& Path)
	{
		if (!FFrameGraphJson::SaveToFile(Path, Graph))
		{
			StatusMessage = "Could not write " + Path.filename().string();
			bStatusIsError = true;
			return false;
		}

		StatusMessage = "Saved " + Path.filename().string();
		bStatusIsError = false;
		LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "Saved frame graph to '{}'", Path.string());
		return true;
	}

	void FFrameGraphPanel::RequestRelayout()
	{
		Placements = ComputeFrameGraphLayout(Graph);
		bApplyPositions = true;
	}

	void FFrameGraphPanel::RefreshIssues()
	{
		// Load issues describe what the file lost; validation describes the graph as it now stands. Both are
		// shown, so the list is rebuilt from validation and the load issues are kept ahead of it.
		std::vector<FFrameGraphIssue> Validation = Graph.Validate(PassTypes);
		Issues.insert(Issues.end(), std::make_move_iterator(Validation.begin()), std::make_move_iterator(Validation.end()));
	}

	void FFrameGraphPanel::OnDrawUI(const FEditorContext& Context)
	{
		LIME_UNUSED(Context);

		// A graph needs room, and the canvas is asked to fill whatever space it gets. A freshly opened panel
		// would otherwise be a strip too small to show a single node.
		ImGui::SetNextWindowSize(ImVec2(1100.0f, 640.0f), ImGuiCond_FirstUseEver);

		if (!ImGui::Begin(GetName(), GetVisiblePtr()))
		{
			ImGui::End();
			return;
		}

		EnsureEditorContext();

		DrawToolbar();
		ImGui::Separator();

		// The side columns are a fraction of the width rather than a fixed size, and collapsible.
		//
		// Fixed widths were the reason the graph appeared shrunk: SetNextWindowSize is ignored once the
		// panel is docked, so two 240 pixel columns inside a narrower window left the canvas with a few
		// hundred pixels. The canvas asks the widget to fit the graph into whatever space it has, so a
		// narrow canvas is not a small window with a normal graph in it but a normal window with a graph
		// scaled down to fit. Sizing the columns relative to the panel keeps the canvas dominant at any
		// width, and collapsing them gives it everything.
		const float TotalWidth = ImGui::GetContentRegionAvail().x;
		const float Spacing = ImGui::GetStyle().ItemSpacing.x;
		const float ColumnWidth = std::clamp(TotalWidth * 0.18f, 150.0f, 260.0f);

		if (bShowSideColumns)
		{
			ImGui::BeginChild("##PassTypes", ImVec2(ColumnWidth, 0.0f), true);
			DrawPassTypeList();
			ImGui::EndChild();
			ImGui::SameLine();
		}

		const float CanvasWidth = bShowSideColumns ? TotalWidth - 2.0f * (ColumnWidth + Spacing) : 0.0f;
		ImGui::BeginChild("##Canvas", ImVec2(CanvasWidth, 0.0f), false);
		DrawGraphCanvas();
		ImGui::EndChild();

		if (bShowSideColumns)
		{
			ImGui::SameLine();
			ImGui::BeginChild("##Details", ImVec2(0.0f, 0.0f), true);
			DrawSelectionDetails();
			ImGui::Separator();
			DrawIssueList();
			ImGui::EndChild();
		}

		// After the canvas is finished with, because an edit changes the id map the canvas is drawing from.
		ApplyPendingEdits();

		ImGui::End();
	}

	void FFrameGraphPanel::DrawToolbar()
	{
		ImGui::SetNextItemWidth(-260.0f);
		ImGui::InputTextWithHint("##Path", "path to a frame graph .json", PathBuffer, sizeof(PathBuffer));

		ImGui::SameLine();
		if (ImGui::Button("Load"))
		{
			LoadFromFile(std::filesystem::path(PathBuffer));
		}

		ImGui::SameLine();
		if (ImGui::Button("Save"))
		{
			SaveToFile(std::filesystem::path(PathBuffer));
		}

		ImGui::SameLine();
		if (ImGui::Button("Relayout"))
		{
			RequestRelayout();
		}

		ImGui::SameLine();
		if (ImGui::Button("Fit"))
		{
			bPendingFit = true;
		}

		ImGui::SameLine();
		// Gives the whole panel to the canvas, which is the quickest way to read a wide graph.
		ImGui::Checkbox("Panels", &bShowSideColumns);

		if (!StatusMessage.empty())
		{
			if (bStatusIsError)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ErrorColour);
				ImGui::TextWrapped("%s", StatusMessage.c_str());
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::TextDisabled("%s", StatusMessage.c_str());
			}
		}
		else
		{
			ImGui::TextDisabled("%s: %zu pass(es), %zu edge(s)", Graph.GetName().c_str(), Graph.GetPasses().size(),
			                    Graph.GetEdges().size());
		}
	}

	void FFrameGraphPanel::DrawPassTypeList()
	{
		ImGui::TextUnformatted("Pass Types");
		ImGui::Separator();

		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##NewPassName", "name (optional)", NewPassNameBuffer, sizeof(NewPassNameBuffer));

		for (const FFramePassTypeDesc& Type : PassTypes.GetAll())
		{
			if (ImGui::Button(Type.Name.c_str(), ImVec2(-1.0f, 0.0f)))
			{
				TypeToAdd = Type.Name;
			}

			if (ImGui::IsItemHovered() && !Type.Description.empty())
			{
				ImGui::SetTooltip("%s", Type.Description.c_str());
			}
		}

		ImGui::Separator();
		ImGui::TextDisabled("Click a type to add it");
	}

	void FFrameGraphPanel::DrawGraphCanvas()
	{
		// Recorded before the canvas begins, so the fit below can tell a laid out window from one still
		// reporting zero.
		CanvasSize = ImGui::GetContentRegionAvail();

		ed::SetCurrentEditor(EditorContext);
		ed::Begin("FrameGraphCanvas", ImVec2(0.0f, 0.0f));

		FFrameGraphHoverState Hover;
		DrawFrameGraphNodes(Graph, PassTypes, Ids, Placements, bApplyPositions, SelectedPass, Hover);

		// Edges are drawn after the nodes so both endpoints exist as far as the widget is concerned.
		const std::vector<FFrameGraphEdge>& Edges = Graph.GetEdges();
		for (SizeType Index = 0; Index < Edges.size(); ++Index)
		{
			const FFrameGraphEdge& Edge = Edges[Index];

			int32 FromPin = 0;
			int32 ToPin = 0;
			if (Edge.Kind == EFrameEdgeKind::Data)
			{
				FromPin = Ids.GetPinId(Edge.From);
				ToPin = Ids.GetPinId(Edge.To);
			}
			else
			{
				FromPin = Ids.GetExecutionOutputId(Edge.From.PassName);
				ToPin = Ids.GetExecutionInputId(Edge.To.PassName);
			}

			if (FromPin == 0 || ToPin == 0)
			{
				continue;
			}

			// Link ids are offset past the pin ids so they cannot collide with them. The widget keeps links
			// in the same id space as everything else.
			const int32 LinkId = 1000000 + static_cast<int32>(Index);
			const ImVec4 Colour =
			    Edge.Kind == EFrameEdgeKind::Execution ? ImVec4(0.90f, 0.71f, 0.35f, 1.0f) : ImVec4(0.55f, 0.75f, 1.0f, 1.0f);
			ed::Link(ed::LinkId(LinkId), ed::PinId(FromPin), ed::PinId(ToPin), Colour,
			         Edge.Kind == EFrameEdgeKind::Execution ? 1.5f : 2.0f);
		}

		HandleLinkCreation();
		HandleDeletions();

		// The graph output toggle needs a popup, and a popup cannot be opened while the canvas owns the
		// draw state; Suspend is what makes it legal.
		if (Hover.bValid && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			OutputToToggle = Hover.HoveredOutput;
			bToggleOutputRequested = true;
		}

		// Selection follows the widget rather than being tracked separately, so clicking a node in the
		// canvas and reading it in the details panel cannot disagree.
		if (ed::HasSelectionChanged())
		{
			SelectedPass.clear();
			ed::NodeId Selected[1];
			if (ed::GetSelectedNodes(Selected, 1) > 0)
			{
				if (const std::string* Pass = Ids.FindPassByNodeId(static_cast<int32>(Selected[0].Get())))
				{
					SelectedPass = *Pass;
				}
			}
		}

		ed::End();

		// After End, and with the editor still current: navigation reads the context, and calling it once
		// SetCurrentEditor(nullptr) has run dereferences a null pointer.
		//
		// Deferred until the canvas has had a few frames at a real size. The fit is computed from the
		// visible region, so running it while the window is still being laid out settles on a zoom of
		// roughly a tenth and leaves the nodes a few pixels wide. Counting only frames where the canvas is
		// actually sized is what makes the wait mean what it says.
		const bool bCanvasHasSize = CanvasSize.x > 64.0f && CanvasSize.y > 64.0f;
		if (bCanvasHasSize)
		{
			++FramesSinceFirstDraw;
			if (bPendingFit && FramesSinceFirstDraw > 2)
			{
				ed::NavigateToContent(0.0f);
				bPendingFit = false;
			}
		}

		ed::SetCurrentEditor(nullptr);

		// Cleared after a full frame with positions applied, so the widget owns them from here and a drag is
		// not undone next frame.
		bApplyPositions = false;
	}

	void FFrameGraphPanel::HandleLinkCreation()
	{
		if (!ed::BeginCreate())
		{
			ed::EndCreate();
			return;
		}

		ed::PinId StartPinId;
		ed::PinId EndPinId;
		if (ed::QueryNewLink(&StartPinId, &EndPinId) && StartPinId && EndPinId)
		{
			const int32 Start = static_cast<int32>(StartPinId.Get());
			const int32 End = static_cast<int32>(EndPinId.Get());

			FFrameGraphEdge Candidate;
			bool bWellFormed = false;

			if (Ids.IsExecutionPin(Start) && Ids.IsExecutionPin(End))
			{
				const std::string* FromPass = Ids.FindPassByExecutionPinId(Start);
				const std::string* ToPass = Ids.FindPassByExecutionPinId(End);
				if (FromPass != nullptr && ToPass != nullptr)
				{
					Candidate.Kind = EFrameEdgeKind::Execution;
					Candidate.From = FFrameGraphResourceRef{ *FromPass, "" };
					Candidate.To = FFrameGraphResourceRef{ *ToPass, "" };
					bWellFormed = true;
				}
			}
			else if (!Ids.IsExecutionPin(Start) && !Ids.IsExecutionPin(End))
			{
				const FFrameGraphResourceRef* From = Ids.FindResourceByPinId(Start);
				const FFrameGraphResourceRef* To = Ids.FindResourceByPinId(End);
				if (From != nullptr && To != nullptr)
				{
					Candidate.Kind = EFrameEdgeKind::Data;
					Candidate.From = *From;
					Candidate.To = *To;
					bWellFormed = true;
				}
			}
			// Mixing a resource pin with an execution pin is left rejected: an execution edge carries no
			// resource, so one end naming one would be meaningless.

			if (!bWellFormed)
			{
				ed::RejectNewItem(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), 2.0f);
			}
			else
			{
				// Asked of the model rather than judged here, so the canvas and a hand edited file are held
				// to exactly the same rules.
				FFrameGraphDesc Trial = Graph;
				FFrameGraphIssue Issue;
				const bool bWouldSucceed = Trial.AddEdge(Candidate, PassTypes, Issue);

				if (!bWouldSucceed)
				{
					ed::RejectNewItem(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), 2.0f);
					// Shown immediately: a rejected drag with no explanation reads as the editor being broken.
					StatusMessage = Issue.Message;
					bStatusIsError = true;
				}
				else if (ed::AcceptNewItem(ImVec4(0.56f, 0.83f, 0.35f, 1.0f), 2.5f))
				{
					Graph = std::move(Trial);
					Issues.clear();
					RefreshIssues();
					StatusMessage = "Connected " + Candidate.From.ToString() + " to " + Candidate.To.ToString();
					bStatusIsError = false;
				}
			}
		}

		ed::EndCreate();
	}

	void FFrameGraphPanel::HandleDeletions()
	{
		if (!ed::BeginDelete())
		{
			ed::EndDelete();
			return;
		}

		ed::LinkId DeletedLink;
		while (ed::QueryDeletedLink(&DeletedLink))
		{
			if (ed::AcceptDeletedItem())
			{
				const int32 Index = static_cast<int32>(DeletedLink.Get()) - 1000000;
				if (Index >= 0 && static_cast<SizeType>(Index) < Graph.GetEdges().size())
				{
					// Queued rather than erased now: removing an edge while the canvas is iterating the ones
					// it drew would invalidate the indices it is holding.
					EdgesToRemove.push_back(static_cast<SizeType>(Index));
				}
			}
		}

		ed::NodeId DeletedNode;
		while (ed::QueryDeletedNode(&DeletedNode))
		{
			if (ed::AcceptDeletedItem())
			{
				if (const std::string* Pass = Ids.FindPassByNodeId(static_cast<int32>(DeletedNode.Get())))
				{
					PassToRemove = *Pass;
				}
			}
		}

		ed::EndDelete();
	}

	void FFrameGraphPanel::ApplyPendingEdits()
	{
		bool bChanged = false;

		if (!TypeToAdd.empty())
		{
			// The typed name if there is one, otherwise a unique name derived from the type, so clicking a
			// type twice does not fail on a duplicate.
			std::string Name = NewPassNameBuffer[0] != '\0' ? std::string(NewPassNameBuffer) : MakeUniqueName(Graph, TypeToAdd);

			FFrameGraphIssue Issue;
			if (Graph.AddPass(Name, TypeToAdd, PassTypes, Issue))
			{
				SelectedPass = Name;
				StatusMessage = "Added " + Name;
				bStatusIsError = false;
				NewPassNameBuffer[0] = '\0';
				bChanged = true;
			}
			else
			{
				StatusMessage = Issue.Message;
				bStatusIsError = true;
			}

			TypeToAdd.clear();
		}

		if (!EdgesToRemove.empty())
		{
			// Descending, so each erase cannot shift an index still to be removed.
			std::sort(EdgesToRemove.begin(), EdgesToRemove.end(), std::greater<SizeType>());
			EdgesToRemove.erase(std::unique(EdgesToRemove.begin(), EdgesToRemove.end()), EdgesToRemove.end());
			for (const SizeType Index : EdgesToRemove)
			{
				Graph.RemoveEdge(Index);
			}
			EdgesToRemove.clear();
			bChanged = true;
		}

		if (!PassToRemove.empty())
		{
			if (Graph.RemovePass(PassToRemove))
			{
				if (SelectedPass == PassToRemove)
				{
					SelectedPass.clear();
				}
				StatusMessage = "Removed " + PassToRemove;
				bStatusIsError = false;
				bChanged = true;
			}
			PassToRemove.clear();
		}

		if (bToggleOutputRequested)
		{
			FFrameGraphIssue Issue;
			const bool bNowMarked = Graph.ToggleGraphOutput(OutputToToggle, PassTypes, Issue);
			if (!Issue.Message.empty())
			{
				StatusMessage = Issue.Message;
				bStatusIsError = true;
			}
			else
			{
				StatusMessage = OutputToToggle.ToString() + (bNowMarked ? " is now a graph output" : " is no longer a graph output");
				bStatusIsError = false;
			}
			bToggleOutputRequested = false;
			bChanged = true;
		}

		if (bChanged)
		{
			// The id map is rebuilt because a pass gained or lost its pins, and the issue list because the
			// graph is a different graph now.
			Ids.Build(Graph, PassTypes);
			Issues.clear();
			RefreshIssues();
		}
	}

	void FFrameGraphPanel::DrawSelectionDetails()
	{
		ImGui::TextUnformatted("Selected Pass");
		ImGui::Separator();

		if (SelectedPass.empty())
		{
			ImGui::TextDisabled("Nothing selected");
			return;
		}

		const FFramePassInstance* Pass = Graph.FindPass(SelectedPass);
		if (Pass == nullptr)
		{
			ImGui::TextDisabled("Nothing selected");
			return;
		}

		ImGui::Text("%s", Pass->Name.c_str());
		ImGui::TextDisabled("%s", Pass->TypeName.c_str());

		const FFramePassTypeDesc* Type = PassTypes.Find(Pass->TypeName);
		if (Type != nullptr)
		{
			if (!Type->Description.empty())
			{
				ImGui::TextWrapped("%s", Type->Description.c_str());
			}

			// The layer doubles as the execution stage: everything in one layer can run before the next.
			const auto Placement = std::find_if(Placements.begin(), Placements.end(), [this](const FFrameGraphNodePlacement& Candidate)
			                                    { return Candidate.PassName == SelectedPass; });
			if (Placement != Placements.end())
			{
				ImGui::Text("Stage %d", Placement->Layer);
			}

			ImGui::Separator();
			ImGui::TextDisabled("Inputs");
			for (const FFrameResourceDesc& Input : Type->Inputs)
			{
				const FFrameGraphResourceRef Ref{ Pass->Name, Input.Name };
				const auto Producer = std::find_if(Graph.GetEdges().begin(), Graph.GetEdges().end(), [&Ref](const FFrameGraphEdge& Edge)
				                                   { return Edge.Kind == EFrameEdgeKind::Data && Edge.To == Ref; });
				if (Producer != Graph.GetEdges().end())
				{
					ImGui::BulletText("%s <- %s", Input.Name.c_str(), Producer->From.ToString().c_str());
				}
				else
				{
					ImGui::PushStyleColor(ImGuiCol_Text, WarningColour);
					ImGui::BulletText("%s (unconnected)", Input.Name.c_str());
					ImGui::PopStyleColor();
				}
			}

			ImGui::TextDisabled("Outputs");
			for (const FFrameResourceDesc& Output : Type->Outputs)
			{
				const FFrameGraphResourceRef Ref{ Pass->Name, Output.Name };
				const bool bMarked = Graph.IsGraphOutput(Ref);
				ImGui::BulletText("%s%s", Output.Name.c_str(), bMarked ? "  [graph output]" : "");
				if (!Output.Format.empty())
				{
					ImGui::SameLine();
					ImGui::TextDisabled("(%s)", Output.Format.c_str());
				}
			}
		}

		ImGui::Separator();
		if (ImGui::Button("Delete Pass", ImVec2(-1.0f, 0.0f)))
		{
			PassToRemove = SelectedPass;
		}
	}

	void FFrameGraphPanel::DrawIssueList()
	{
		ImGui::TextUnformatted("Issues");
		ImGui::Separator();

		if (Issues.empty())
		{
			ImGui::TextDisabled("None");
			return;
		}

		for (const FFrameGraphIssue& Issue : Issues)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Issue.IsError() ? ErrorColour : WarningColour);
			ImGui::TextWrapped("%s", Issue.Message.c_str());
			ImGui::PopStyleColor();
		}
	}
} // namespace Lime
