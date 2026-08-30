#include "Editor/RenderGraph/RenderGraphPanel.h"

#include "Core/Logging/LogManager.h"
#include "Core/Reflection/JsonArchive.h"
#include "Editor/PropertyDrawer.h"
#include "Engine/ProjectSettings.h"
#include "Platform/PlatformPaths.h"
#include "Renderer/Passes/BuiltinPasses.h"
#include "Renderer/Renderer.h"

#include <imgui_node_editor.h>

// For FormatToString, so the inspector names a format the same way the RHI does.
#include <nvrhi/utils.h>

// SetFontRasterizerDensity lives in the internal header in 1.92. It is the supported way to ask for
// glyphs baked at a different pixel density, and the node editor needs it to keep zoomed text sharp.
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ed = ax::NodeEditor;

namespace Lime
{
	namespace
	{
		constexpr ImU32 ErrorColour = IM_COL32(240, 120, 110, 255);
		constexpr ImU32 WarningColour = IM_COL32(235, 195, 100, 255);

		// A bullet whose text wraps, and whose continuation lines stay under the text rather than sliding
		// back beneath the bullet.
		//
		// ImGui::BulletText does not wrap, so in a narrow column anything long enough was clipped at the
		// panel edge.
		//
		// Bullet() ends with its own SameLine, so the cursor is already past the glyph and its spacing when
		// this reads it. Indenting by that distance leaves the first line exactly where it is and gives the
		// wrapped lines the same left margin, which is what keeps the row reading as one item.
		void DrawWrappedBullet(const std::string& Text)
		{
			const float LineStart = ImGui::GetCursorPosX();
			ImGui::Bullet();
			const float Offset = ImGui::GetCursorPosX() - LineStart;

			ImGui::Indent(Offset);
			ImGui::TextWrapped("%s", Text.c_str());
			ImGui::Unindent(Offset);
		}

		// Density is capped because the atlas grows with its square: a pass name baked for a zoom of 10
		// would cost a hundred times the pixels for detail no one can use. Past this the text is already
		// far larger than the screen.
		constexpr float MaxCanvasRasterizerDensity = 4.0f;

		// Zoom is quantised into steps before it becomes a density, so a drag on the scroll wheel does not
		// bake a fresh set of glyphs for every intermediate value. A quarter step is finer than the eye
		// follows while keeping the number of live bakes small.
		constexpr float CanvasRasterizerDensityStep = 0.25f;

		// The density that keeps canvas text sharp at the current zoom.
		//
		// GetCurrentZoom returns the view's InvScale, so it is 0.5 when the graph is drawn at twice its
		// size: the density wanted is its reciprocal. Zooming out is left alone, since baking below the
		// display density would only throw away detail the atlas already holds.
		float CalcCanvasRasterizerDensity(float BaseDensity)
		{
			const float Zoom = ed::GetCurrentZoom();
			if (!(Zoom > 0.0f))
			{
				return BaseDensity;
			}

			const float Scale = 1.0f / Zoom;
			if (Scale <= 1.0f)
			{
				return BaseDensity;
			}

			const float Quantised = std::ceil(Scale / CanvasRasterizerDensityStep) * CanvasRasterizerDensityStep;
			return BaseDensity * std::min(Quantised, MaxCanvasRasterizerDensity);
		}

		void SetTextBuffer(char* Buffer, SizeType Capacity, const std::string& Value)
		{
			const SizeType Length = std::min(Value.size(), Capacity - 1);
			std::memcpy(Buffer, Value.data(), Length);
			Buffer[Length] = '\0';
		}

		// A name that is not taken yet, so adding a second pass of one type does not fail on the name.
		std::string MakeUniqueName(const FRenderGraphDesc& Graph, const std::string& TypeName)
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

	FRenderGraphPanel::FRenderGraphPanel()
	{
		// Reflected from the passes this build provides, not a list kept here.
		//
		// It has to be the same table the engine compiles against, or the panel would accept a graph the
		// engine then rejects — or refuse one it would have run.
		//
		// Through BuildRenderGraphPassTypes rather than the registry directly, because the editor is
		// initialised before the engine registers the built-ins: reading the registry here would find it
		// empty and every pass in a loaded graph would be reported as an unknown type. That call registers
		// first, and registering twice is harmless.
		PassTypes = BuildRenderGraphPassTypes();

		// A placeholder only, so the field is not empty before Initialize supplies the project's own graph.
		// Nothing is loaded here: the path lives in the project settings, which the constructor cannot see.
		SetTextBuffer(PathBuffer, sizeof(PathBuffer), GetDefaultLoadDirectory().string());
	}

	void FRenderGraphPanel::Initialize(const FProjectSettings& ProjectSettings)
	{
		AuthoringPath = ProjectSettings.ResolveRenderGraphAuthoringPath();
		DeployedPath = ProjectSettings.ResolveRenderGraphDeployedPath();

		// The project's own copy when it has one, so what is on screen is what a save writes back.
		//
		// Otherwise whatever the engine resolved, which for a project that never named a graph is the
		// engine's default. Showing that rather than nothing is the point: it is what the project is
		// actually running, and it is the sensible thing to start editing from.
		const bool bHasOwnCopy = !AuthoringPath.empty() && std::filesystem::exists(AuthoringPath);
		const std::filesystem::path LoadPath = bHasOwnCopy ? AuthoringPath : ProjectSettings.ResolveRenderGraphPath();

		if (LoadPath.empty())
		{
			// Not fatal, and not the panel's business to explain: the engine has already reported why it has
			// no graph to run. Leaving the field pointing somewhere useful is what lets one be loaded by hand.
			if (!AuthoringPath.empty())
			{
				SetTextBuffer(PathBuffer, sizeof(PathBuffer), AuthoringPath.string());
			}
			else if (!ProjectSettings.RenderGraphPath.empty())
			{
				SetTextBuffer(PathBuffer, sizeof(PathBuffer), ProjectSettings.RenderGraphPath);
			}

			StatusMessage = ProjectSettings.RenderGraphPath.empty() ? "No render graph is configured for this project"
			                                                       : "Could not find " + ProjectSettings.RenderGraphPath;
			bStatusIsError = true;
			return;
		}

		if (!LoadFromFile(LoadPath) || bHasOwnCopy || AuthoringPath.empty())
		{
			return;
		}

		// The field names where a save belongs rather than what was just loaded.
		//
		// They differ only here, when the graph came from the engine's content. Saving back there would be
		// wrong twice over: the next build overwrites that directory, and it is shared by every project, so
		// one project's edit would follow the others around. Writing the project's own copy instead is what
		// lets it take the graph over.
		SetTextBuffer(PathBuffer, sizeof(PathBuffer), AuthoringPath.string());
		StatusMessage += ", engine default - Save writes this project's own copy";
	}

	FRenderGraphPanel::~FRenderGraphPanel()
	{
		if (EditorContext != nullptr)
		{
			ed::DestroyEditor(EditorContext);
			EditorContext = nullptr;
		}
	}

	std::filesystem::path FRenderGraphPanel::GetDefaultLoadDirectory()
	{
		// Content, because that is where the shipped examples land and it is copied next to the executable.
		return FPlatformPaths::GetContentDirectory() / "RenderGraph";
	}

	std::filesystem::path FRenderGraphPanel::GetDefaultSaveDirectory()
	{
		// Saved rather than Content: a build copies Content out again, so writing there would look like the
		// save had silently reverted.
		return FPlatformPaths::GetSavedDirectory() / "RenderGraph";
	}

	void FRenderGraphPanel::EnsureEditorContext()
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

	bool FRenderGraphPanel::LoadFromFile(const std::filesystem::path& Path)
	{
		const FRenderGraphLoadResult Result = FRenderGraphJson::LoadFromFile(Path, PassTypes, Graph);
		Issues = Result.Issues;

		if (!Result.bSucceeded)
		{
			StatusMessage = "Could not load " + Path.filename().string();
			bStatusIsError = true;
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_EDITOR, "Render graph load failed: '{}'", Path.string());
			return false;
		}

		SelectedPass.clear();
		Ids.Build(Graph, PassTypes);

		// Dropped rather than kept: these are the previous graph's nodes, and a pass sharing a name across
		// two graphs need not be the same shape in both.
		NodeSizes.clear();

		// Deferred rather than laid out here.
		//
		// A layout needs to know how big each node is, and only the widget that drew a node knows that. At
		// this point nothing of this graph has been drawn, so laying out now would fall back to nominal
		// sizes and a node made wide by a long resource name would overlap the layer beside it. The nodes
		// are measured at the end of the next canvas frame, and the layout runs then.
		//
		// The fit waits with it, since framing a layout that is about to change would settle on the wrong
		// zoom.
		bLayoutPendingMeasurement = true;

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
		LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "Loaded render graph '{}' with {} pass(es) and {} edge(s)", Path.string(),
		              Graph.GetPasses().size(), Graph.GetEdges().size());
		return true;
	}

	void FRenderGraphPanel::SyncSettingsFromRenderer()
	{
		if (Renderer == nullptr)
		{
			return;
		}

		for (const FRenderGraphPassInstance& Pass : Graph.GetPasses())
		{
			IRenderPass* RunningPass = Renderer->FindGraphPass(Pass.Name);
			if (RunningPass == nullptr)
			{
				// Not in the running graph (added but not applied, or a different file); leave its
				// stored settings alone.
				continue;
			}

			const FReflectedRef Ref = RunningPass->GetReflectedSettings();
			if (!Ref.IsValid())
			{
				continue;
			}

			FJson Settings;
			FJsonArchive::SaveFields(Ref.Type, Ref.Instance, Settings);
			Graph.SetPassSettings(Pass.Name, std::move(Settings));
		}
	}

	bool FRenderGraphPanel::SaveToFile(const std::filesystem::path& Path)
	{
		// The running instances carry the tuned values; pull them back so the file records them.
		SyncSettingsFromRenderer();

		if (!FRenderGraphJson::SaveToFile(Path, Graph))
		{
			StatusMessage = "Could not write " + Path.filename().string();
			bStatusIsError = true;
			return false;
		}

		StatusMessage = "Saved " + Path.filename().string();
		bStatusIsError = false;
		LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "Saved render graph to '{}'", Path.string());

		// Only for the project's own copy. An explicit path from a script is a scratch file and has nothing
		// to do with what the engine loads, so mirroring it would be wrong.
		if (!AuthoringPath.empty() && !DeployedPath.empty() && Path == AuthoringPath && DeployedPath != AuthoringPath)
		{
			RefreshDeployedCopy(Path);
		}

		return true;
	}

	void FRenderGraphPanel::RefreshDeployedCopy(const std::filesystem::path& AuthoredPath)
	{
		// The engine loads the deployed copy, so without this a save would take effect only after a rebuild.
		// Mirrors what FProjectSettings::SaveToFile does for the settings file, and for the same reason.
		//
		// This is also what makes a project take over an engine default on the next launch rather than the
		// one after: the copy it writes here sits at the path the engine already resolves to, because
		// project content is deployed over engine content at matching relative paths.
		std::error_code ErrorCode;
		if (DeployedPath.has_parent_path())
		{
			std::filesystem::create_directories(DeployedPath.parent_path(), ErrorCode);
		}

		std::filesystem::copy_file(AuthoredPath, DeployedPath, std::filesystem::copy_options::overwrite_existing, ErrorCode);
		if (ErrorCode)
		{
			// Not fatal: the authored file is already correct and the next build will copy it out.
			LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "Saved the render graph but could not refresh '{}': {}",
			                 DeployedPath.string(), ErrorCode.message());
			StatusMessage += " - rebuild needed to apply";
			return;
		}

		// The graph is compiled once at startup, so an edit cannot reach the running frame.
		StatusMessage += " - restart to apply";
		LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "Refreshed the deployed render graph at '{}'", DeployedPath.string());
	}

	void FRenderGraphPanel::RequestRelayout()
	{
		// Uses the sizes recorded while the canvas was last drawn, so a node made wide by a long resource
		// name gets the room it needs instead of overlapping the layer beside it.
		//
		// Callable directly whenever the graph on screen has already been drawn, which is the case for the
		// toolbar button. A load is the exception and goes through bLayoutPendingMeasurement, since none of
		// its nodes have been measured yet.
		Placements = ComputeRenderGraphLayout(Graph, NodeSizes);
		bApplyPositions = true;
	}

	void FRenderGraphPanel::RefreshIssues()
	{
		// Load issues describe what the file lost; validation describes the graph as it now stands. Both are
		// shown, so the list is rebuilt from validation and the load issues are kept ahead of it.
		std::vector<FRenderGraphIssue> Validation = Graph.Validate(PassTypes);
		Issues.insert(Issues.end(), std::make_move_iterator(Validation.begin()), std::make_move_iterator(Validation.end()));
	}

	void FRenderGraphPanel::OnDrawUI(const FEditorContext& Context)
	{
		// Remembered so a later save can read the running passes' settings; the context is per frame.
		Renderer = Context.Renderer;

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
			DrawSelectionDetails(Context);
			ImGui::Separator();
			DrawIssueList();
			ImGui::EndChild();
		}

		// After the canvas is finished with, because an edit changes the id map the canvas is drawing from.
		ApplyPendingEdits();

		ImGui::End();
	}

	void FRenderGraphPanel::DrawToolbar()
	{
		ImGui::SetNextItemWidth(-260.0f);
		ImGui::InputTextWithHint("##Path", "path to a render graph .json", PathBuffer, sizeof(PathBuffer));

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

	void FRenderGraphPanel::DrawPassTypeList()
	{
		ImGui::TextUnformatted("Pass Types");
		ImGui::Separator();

		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##NewPassName", "name (optional)", NewPassNameBuffer, sizeof(NewPassNameBuffer));

		for (const FRenderGraphPassTypeDesc& Type : PassTypes.GetAll())
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

	void FRenderGraphPanel::DrawGraphCanvas()
	{
		// Recorded before the canvas begins, so the fit below can tell a laid out window from one still
		// reporting zero.
		CanvasSize = ImGui::GetContentRegionAvail();

		ed::SetCurrentEditor(EditorContext);

		// Text inside the canvas is rasterised at the base size and then scaled by the view transform,
		// which resamples the glyph bitmaps and makes them blurry when zoomed in. Raising the rasterizer
		// density asks for glyphs baked at the on-screen pixel count instead. Density does not take part
		// in text measurement, so the layout is unchanged and only the sharpness differs.
		const float PreviousDensity = ImGui::GetFontRasterizerDensity();
		ImGui::SetFontRasterizerDensity(CalcCanvasRasterizerDensity(PreviousDensity));

		ed::Begin("RenderGraphCanvas", ImVec2(0.0f, 0.0f));

		FRenderGraphHoverState Hover;
		// Captured before the nodes are drawn, because a deferred layout later in this frame may set the
		// flag again and the clear at the end must only retire the request this frame actually served.
		const bool bAppliedPositionsThisFrame = bApplyPositions;
		DrawRenderGraphNodes(Graph, PassTypes, Ids, Placements, bAppliedPositionsThisFrame, SelectedPass, Hover);

		// Edges are drawn after the nodes so both endpoints exist as far as the widget is concerned.
		const std::vector<FRenderGraphEdge>& Edges = Graph.GetEdges();
		for (SizeType Index = 0; Index < Edges.size(); ++Index)
		{
			const FRenderGraphEdge& Edge = Edges[Index];

			const int32 FromPin = Ids.GetPinId(Edge.From);
			const int32 ToPin = Ids.GetPinId(Edge.To);
			if (FromPin == 0 || ToPin == 0)
			{
				continue;
			}

			// Link ids are offset past the pin ids so they cannot collide with them. The widget keeps links
			// in the same id space as everything else.
			const int32 LinkId = FRenderGraphIdMap::LinkIdBase + static_cast<int32>(Index);
			ed::Link(ed::LinkId(LinkId), ed::PinId(FromPin), ed::PinId(ToPin), ImVec4(0.55f, 0.75f, 1.0f, 1.0f), 2.0f);
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

		ed::End();

		// Read after End, not before it.
		//
		// A click is turned into a selection inside End, while Begin resets the "changed" comparison for
		// the frame. Asking before End therefore compares a selection against itself and never reports a
		// change, which left the details panel showing whatever was picked before.
		//
		// The editor is still current here, which is what these calls need.
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

		// Recorded every frame so a relayout has real sizes to work with. The widget only knows how big a
		// node is once it has drawn it, and a node is as wide as its longest resource name, so this is the
		// only place the number exists. Sizes are in canvas space, which is what the layout produces.
		NodeSizes.clear();
		NodeSizes.reserve(Graph.GetPasses().size());
		SizeType MeasurableNodes = 0;
		for (const FRenderGraphPassInstance& Pass : Graph.GetPasses())
		{
			const int32 NodeId = Ids.GetNodeId(Pass.Name);
			if (NodeId == 0)
			{
				continue;
			}

			++MeasurableNodes;
			const ImVec2 Size = ed::GetNodeSize(ed::NodeId(NodeId));
			if (Size.x > 0.0f && Size.y > 0.0f)
			{
				NodeSizes.push_back(FRenderGraphNodeSize{ Pass.Name, Size.x, Size.y });
			}
		}

		// The layout a load asked for, run now that the nodes it needs to place have been measured.
		//
		// Held back until every node has a measurement rather than laying out on a partial set: a node still
		// reporting nothing would be placed at a nominal size and would then overlap once it appeared at its
		// real width, which is the problem this defers for in the first place.
		if (bLayoutPendingMeasurement && MeasurableNodes > 0 && NodeSizes.size() == MeasurableNodes)
		{
			bLayoutPendingMeasurement = false;
			RequestRelayout();
			// Framed once the positions this produced have been applied, so the view matches the layout
			// rather than the one it replaced.
			bPendingFit = true;
		}

		// Restored once the canvas has flushed its draw data, so the rest of the editor keeps rendering
		// text at the density of the display rather than at the canvas zoom.
		ImGui::SetFontRasterizerDensity(PreviousDensity);

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
		//
		// Only the request this frame served is retired. The deferred layout above runs after the nodes have
		// been drawn and sets the flag for the next frame; clearing unconditionally would discard it and the
		// graph would stay stacked wherever the widget first put it.
		if (bAppliedPositionsThisFrame)
		{
			bApplyPositions = false;
		}
	}

	void FRenderGraphPanel::HandleLinkCreation()
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

			FRenderGraphEdge Candidate;
			bool bWellFormed = false;

			const FRenderGraphResourceRef* From = Ids.FindResourceByPinId(Start);
			const FRenderGraphResourceRef* To = Ids.FindResourceByPinId(End);
			if (From != nullptr && To != nullptr)
			{
				Candidate.From = *From;
				Candidate.To = *To;
				bWellFormed = true;
			}

			if (!bWellFormed)
			{
				ed::RejectNewItem(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), 2.0f);
			}
			else
			{
				// Asked of the model rather than judged here, so the canvas and a hand edited file are held
				// to exactly the same rules.
				FRenderGraphDesc Trial = Graph;
				FRenderGraphIssue Issue;
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

	void FRenderGraphPanel::HandleDeletions()
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
				const int32 Index = static_cast<int32>(DeletedLink.Get()) - FRenderGraphIdMap::LinkIdBase;
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
					// Appended rather than assigned: a box selection reports every node it covers, and
					// keeping only the last one would delete a single node out of the group.
					PassesToRemove.push_back(*Pass);
				}
			}
		}

		ed::EndDelete();
	}

	void FRenderGraphPanel::ApplyPendingEdits()
	{
		bool bChanged = false;

		if (!TypeToAdd.empty())
		{
			// The typed name if there is one, otherwise a unique name derived from the type, so clicking a
			// type twice does not fail on a duplicate.
			std::string Name = NewPassNameBuffer[0] != '\0' ? std::string(NewPassNameBuffer) : MakeUniqueName(Graph, TypeToAdd);

			FRenderGraphIssue Issue;
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

		if (!PassesToRemove.empty())
		{
			int32 RemovedCount = 0;
			for (const std::string& Pass : PassesToRemove)
			{
				if (Graph.RemovePass(Pass))
				{
					if (SelectedPass == Pass)
					{
						SelectedPass.clear();
					}
					// Dropped from the layout as well, so the positions kept for the remaining passes stay
					// paired with the passes they belong to.
					Placements.erase(std::remove_if(Placements.begin(), Placements.end(),
					                                [&Pass](const FRenderGraphNodePlacement& Candidate)
					                                { return Candidate.PassName == Pass; }),
					                 Placements.end());
					++RemovedCount;
				}
			}

			if (RemovedCount == 1)
			{
				StatusMessage = "Removed " + PassesToRemove.front();
				bStatusIsError = false;
				bChanged = true;
			}
			else if (RemovedCount > 1)
			{
				StatusMessage = "Removed " + std::to_string(RemovedCount) + " passes";
				bStatusIsError = false;
				bChanged = true;
			}

			PassesToRemove.clear();
		}

		if (bToggleOutputRequested)
		{
			FRenderGraphIssue Issue;
			const bool bNowMarked = Graph.ToggleGraphOutput(OutputToToggle, PassTypes, Issue);
			if (!Issue.Message.empty())
			{
				StatusMessage = Issue.Message;
				bStatusIsError = true;
			}
			else
			{
				// The slot is reported because it is what identifies the output from here on, and marking a
				// second one is only useful if you can tell which is which.
				if (bNowMarked)
				{
					const int32 Slot = Graph.FindGraphOutputSlot(OutputToToggle);
					StatusMessage = OutputToToggle.ToString() + " is graph output " + std::to_string(Slot);
				}
				else
				{
					StatusMessage = OutputToToggle.ToString() + " is no longer a graph output";
				}
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

	void FRenderGraphPanel::DrawSelectionDetails(const FEditorContext& Context)
	{
		ImGui::TextUnformatted("Selected Pass");
		ImGui::Separator();

		if (SelectedPass.empty())
		{
			ImGui::TextDisabled("Nothing selected");
			return;
		}

		const FRenderGraphPassInstance* Pass = Graph.FindPass(SelectedPass);
		if (Pass == nullptr)
		{
			ImGui::TextDisabled("Nothing selected");
			return;
		}

		ImGui::Text("%s", Pass->Name.c_str());
		ImGui::TextDisabled("%s", Pass->TypeName.c_str());

		const FRenderGraphPassTypeDesc* Type = PassTypes.Find(Pass->TypeName);
		if (Type != nullptr)
		{
			if (!Type->Description.empty())
			{
				ImGui::TextWrapped("%s", Type->Description.c_str());
			}

			// The layer doubles as the execution stage: everything in one layer can run before the next.
			const auto Placement = std::find_if(Placements.begin(), Placements.end(), [this](const FRenderGraphNodePlacement& Candidate)
			                                    { return Candidate.PassName == SelectedPass; });
			if (Placement != Placements.end())
			{
				ImGui::Text("Stage %d", Placement->Layer);
			}

			ImGui::Separator();
			ImGui::TextDisabled("Inputs");
			for (const FRenderGraphResourceDesc& Input : Type->Inputs)
			{
				const FRenderGraphResourceRef Ref{ Pass->Name, Input.Name };
				const auto Producer = std::find_if(Graph.GetEdges().begin(), Graph.GetEdges().end(),
				                                   [&Ref](const FRenderGraphEdge& Edge) { return Edge.To == Ref; });

				// Wrapped rather than written as a bullet: this column is narrow by design, and a producer
				// named "ShadowCaster.depth" is longer than it. BulletText does not wrap, so the tail was
				// simply clipped away.
				if (Producer != Graph.GetEdges().end())
				{
					DrawWrappedBullet(Input.Name + " <- " + Producer->From.ToString());
				}
				else
				{
					ImGui::PushStyleColor(ImGuiCol_Text, WarningColour);
					DrawWrappedBullet(Input.Name + " (unconnected)");
					ImGui::PopStyleColor();
				}
			}

			ImGui::TextDisabled("Outputs");
			for (const FRenderGraphResourceDesc& Output : Type->Outputs)
			{
				const FRenderGraphResourceRef Ref{ Pass->Name, Output.Name };

				// One string for the whole row, because the format used to be appended with SameLine and a
				// separate call, which cannot wrap and left "(D32)" running off the panel.
				//
				// UNKNOWN is left unwritten rather than shown: it means the pass did not care and the graph
				// decides, so naming it would suggest a format that is not the one actually used.
				std::string Line = Output.Name;
				if (Output.Format != nvrhi::Format::UNKNOWN)
				{
					Line += " (";
					Line += nvrhi::utils::FormatToString(Output.Format);
					Line += ")";
				}

				// The slot is what a viewport would be bound to, so it is named rather than implied.
				const int32 Slot = Graph.FindGraphOutputSlot(Ref);
				if (Slot >= 0)
				{
					Line += "  [out " + std::to_string(Slot) + "]";
				}
				DrawWrappedBullet(Line);
			}
		}

		// Settings for the running instance of this pass, if the graph on screen matches the one the
		// renderer is executing. Editing writes straight into that instance, and two passes of the same
		// type are distinct instances, so their settings stay independent.
		if (Context.Renderer != nullptr)
		{
			if (IRenderPass* RunningPass = Context.Renderer->FindGraphPass(SelectedPass))
			{
				const FReflectedRef Settings = RunningPass->GetReflectedSettings();
				if (Settings.IsValid())
				{
					ImGui::Separator();
					ImGui::TextDisabled("Settings");
					// Unique ID per instance so two passes of the same type never share ImGui id state.
					ImGui::PushID(RunningPass);
					FPropertyDrawer::Draw(Settings);
					ImGui::PopID();
				}
			}
			else
			{
				// On screen but not running: added without applying, or the running graph came from a
				// different file.
				ImGui::Separator();
				ImGui::TextDisabled("Not in the running graph");
			}
		}

		ImGui::Separator();
		if (ImGui::Button("Delete Pass", ImVec2(-1.0f, 0.0f)))
		{
			PassesToRemove.push_back(SelectedPass);
		}
	}

	void FRenderGraphPanel::DrawIssueList()
	{
		ImGui::TextUnformatted("Issues");
		ImGui::Separator();

		if (Issues.empty())
		{
			ImGui::TextDisabled("None");
			return;
		}

		for (const FRenderGraphIssue& Issue : Issues)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Issue.IsError() ? ErrorColour : WarningColour);
			ImGui::TextWrapped("%s", Issue.Message.c_str());
			ImGui::PopStyleColor();
		}
	}
} // namespace Lime
