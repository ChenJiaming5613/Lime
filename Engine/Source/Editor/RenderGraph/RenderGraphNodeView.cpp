#include "Editor/RenderGraph/RenderGraphNodeView.h"

#include <imgui_node_editor.h>

#include <algorithm>

namespace ed = ax::NodeEditor;

namespace Lime
{
	namespace
	{
		// Key for a pin lookup. The separator cannot appear in a pass name, so "A|out" is unambiguous even
		// if a resource is called "out" on several passes.
		std::string MakePinKey(const std::string& PassName, const std::string& ResourceName)
		{
			return PassName + "|" + ResourceName;
		}

		// An id derived from a name rather than handed out by a counter.
		//
		// The node widget remembers a node's position against its id, and nothing reads those positions
		// back, so an id has to mean the same pass for as long as the panel is open. A counter walked in
		// pass order does not: deleting a pass shifts every later pass down one, and each of them then
		// inherits the position of the pass that used to hold its id, which looks like the graph
		// rearranging itself on delete.
		//
		// FNV-1a, folded into the positive int32 range because the widget treats 0 as "no id". Ids for
		// different kinds of item are salted, so a node and its own execution pin cannot collide.
		int32 MakeStableId(const std::string& Name, uint32 Salt)
		{
			uint32 Hash = 2166136261u ^ Salt;
			for (const char Character : Name)
			{
				Hash ^= static_cast<uint8>(Character);
				Hash *= 16777619u;
			}

			// Clear the sign bit and avoid 0, which the widget reserves.
			const int32 Result = static_cast<int32>(Hash & 0x7fffffffu);
			return Result == 0 ? 1 : Result;
		}

		constexpr uint32 NodeIdSalt = 0x9e3779b9u;
		constexpr uint32 ResourcePinSalt = 0x27d4eb2fu;

		// Colours chosen for meaning rather than decoration: a graph output has to stand out at a glance,
		// Colours chosen for meaning rather than decoration: a graph output has to stand out at a glance,
		// since a graph without one produces nothing.
		constexpr ImU32 OutputPinColour = IM_COL32(120, 190, 255, 255);
		constexpr ImU32 InputPinColour = IM_COL32(160, 160, 170, 255);
		constexpr ImU32 GraphOutputColour = IM_COL32(143, 212, 89, 255);
		constexpr ImU32 SelectedTitleColour = IM_COL32(255, 220, 120, 255);
		constexpr ImU32 TitleColour = IM_COL32(230, 230, 235, 255);
		constexpr ImU32 TypeColour = IM_COL32(150, 150, 160, 255);

		// A filled circle, drawn at the cursor and advancing past itself so a label can follow on the same
		// line.
		//
		// Reports the circle's centre so the caller can hand it to ed::PinPivotRect. The widget anchors a
		// link at the centre of the whole pin rectangle, and that rectangle covers the label too, so
		// without the pivot the line would meet the middle of the text instead of the dot.
		//
		// The pivot is reported as a single point rather than as the circle's bounds. A link endpoint is
		// the point on the pivot rectangle closest to the other end, not the rectangle's centre, so a
		// pivot with area lets the endpoint slide along the dot's edge and jump as a node is dragged past
		// it. Collapsing the pivot to the centre pins the endpoint there. This matches the widget's own
		// default of a zero sized pivot.
		//
		// An ImVec2 out parameter rather than an ImRect: that type lives in imgui_internal.h, and one
		// rectangle is not reason enough for this file to depend on the internal header.
		void DrawPinMarker(ImU32 Colour, ImVec2& OutPivot)
		{
			const float Radius = 4.5f;
			const ImVec2 Cursor = ImGui::GetCursorScreenPos();
			const ImVec2 Centre(Cursor.x + Radius, Cursor.y + ImGui::GetTextLineHeight() * 0.5f);

			ImGui::GetWindowDrawList()->AddCircleFilled(Centre, Radius, Colour);

			ImGui::Dummy(ImVec2(Radius * 2.0f + 4.0f, ImGui::GetTextLineHeight()));

			OutPivot = Centre;
		}
	} // namespace

	void FRenderGraphIdMap::Reset()
	{
		UsedIds.clear();
		NodeIds.clear();
		PinIds.clear();
		PassByNode.clear();
		ResourceByPin.clear();
	}

	void FRenderGraphIdMap::Build(const FRenderGraphDesc& Graph, const FRenderGraphPassTypeRegistry& Types)
	{
		Reset();

		// Ids are derived from names, so they survive a pass being removed: the widget keeps each node's
		// position against its id, and an id that moved to another pass would carry the position with it.
		//
		// A hash can collide, and two items sharing an id would be treated as one by the widget, so each
		// id is claimed and a collision steps to the next free value. Claiming happens in the graph's own
		// pass order, which keeps the outcome the same for the same file.
		for (const FRenderGraphPassInstance& Pass : Graph.GetPasses())
		{
			const int32 NodeId = ClaimId(MakeStableId(Pass.Name, NodeIdSalt));
			NodeIds.emplace(Pass.Name, NodeId);
			PassByNode.emplace(NodeId, Pass.Name);

			const FRenderGraphPassTypeDesc* Type = Types.Find(Pass.TypeName);
			if (Type == nullptr)
			{
				// A pass whose type is unknown still gets a node, so the graph is not silently short a box;
				// it simply has no resource pins to offer.
				continue;
			}

			for (const FRenderGraphResourceDesc& Input : Type->Inputs)
			{
				const std::string Key = MakePinKey(Pass.Name, Input.Name);
				const int32 PinId = ClaimId(MakeStableId(Key, ResourcePinSalt));
				PinIds.emplace(Key, PinId);
				ResourceByPin.emplace(PinId, FRenderGraphResourceRef{ Pass.Name, Input.Name });
			}

			for (const FRenderGraphResourceDesc& Output : Type->Outputs)
			{
				const std::string Key = MakePinKey(Pass.Name, Output.Name);
				const int32 PinId = ClaimId(MakeStableId(Key, ResourcePinSalt));
				PinIds.emplace(Key, PinId);
				ResourceByPin.emplace(PinId, FRenderGraphResourceRef{ Pass.Name, Output.Name });
			}
		}
	}

	int32 FRenderGraphIdMap::ClaimId(int32 Preferred)
	{
		// Link ids live above LinkIdBase, so an item id must stay below it or a link and an item could end
		// up sharing a number.
		int32 Candidate = Preferred % LinkIdBase;
		if (Candidate == 0)
		{
			Candidate = 1;
		}

		while (!UsedIds.insert(Candidate).second)
		{
			++Candidate;
			if (Candidate >= LinkIdBase)
			{
				Candidate = 1;
			}
		}

		return Candidate;
	}

	int32 FRenderGraphIdMap::GetNodeId(const std::string& PassName) const
	{
		const auto Found = NodeIds.find(PassName);
		return Found != NodeIds.end() ? Found->second : 0;
	}

	int32 FRenderGraphIdMap::GetPinId(const FRenderGraphResourceRef& Ref) const
	{
		const auto Found = PinIds.find(MakePinKey(Ref.PassName, Ref.ResourceName));
		return Found != PinIds.end() ? Found->second : 0;
	}

	const std::string* FRenderGraphIdMap::FindPassByNodeId(int32 NodeId) const
	{
		const auto Found = PassByNode.find(NodeId);
		return Found != PassByNode.end() ? &Found->second : nullptr;
	}

	const FRenderGraphResourceRef* FRenderGraphIdMap::FindResourceByPinId(int32 PinId) const
	{
		const auto Found = ResourceByPin.find(PinId);
		return Found != ResourceByPin.end() ? &Found->second : nullptr;
	}

	void DrawRenderGraphNodes(const FRenderGraphDesc& Graph, const FRenderGraphPassTypeRegistry& Types, const FRenderGraphIdMap& Ids,
	                         const std::vector<FRenderGraphNodePlacement>& Placements, bool bApplyPositions, const std::string& SelectedPass,
	                         FRenderGraphHoverState& OutHover)
	{
		OutHover = {};

		for (const FRenderGraphPassInstance& Pass : Graph.GetPasses())
		{
			const int32 NodeId = Ids.GetNodeId(Pass.Name);
			if (NodeId == 0)
			{
				continue;
			}

			// Positions are applied on the frame after a load or an explicit relayout, and not afterwards:
			// setting them every frame would undo a drag the moment the user let go.
			if (bApplyPositions)
			{
				const auto Placement = std::find_if(Placements.begin(), Placements.end(), [&Pass](const FRenderGraphNodePlacement& Candidate)
				                                    { return Candidate.PassName == Pass.Name; });
				if (Placement != Placements.end())
				{
					ed::SetNodePosition(ed::NodeId(NodeId), ImVec2(Placement->X, Placement->Y));
				}
			}

			ed::BeginNode(ed::NodeId(NodeId));

			const bool bSelected = Pass.Name == SelectedPass;
			ImGui::PushStyleColor(ImGuiCol_Text, bSelected ? SelectedTitleColour : TitleColour);
			ImGui::TextUnformatted(Pass.Name.c_str());
			ImGui::PopStyleColor();

			// The type under the name, because two instances of one type are told apart by name while what
			// they do comes from the type.
			ImGui::PushStyleColor(ImGuiCol_Text, TypeColour);
			ImGui::TextUnformatted(Pass.TypeName.c_str());
			ImGui::PopStyleColor();

			const FRenderGraphPassTypeDesc* Type = Types.Find(Pass.TypeName);

			// Two columns via groups: inputs on the left, outputs on the right, which is the direction the
			// graph reads.
			ImGui::BeginGroup();

			if (Type != nullptr)
			{
				for (const FRenderGraphResourceDesc& Input : Type->Inputs)
				{
					const FRenderGraphResourceRef Ref{ Pass.Name, Input.Name };
					const int32 PinId = Ids.GetPinId(Ref);
					if (PinId == 0)
					{
						continue;
					}

					ed::BeginPin(ed::PinId(PinId), ed::PinKind::Input);
					ImVec2 Pivot;
					DrawPinMarker(InputPinColour, Pivot);
					ed::PinPivotRect(Pivot, Pivot);
					ImGui::SameLine();
					ImGui::TextUnformatted(Input.Name.c_str());
					ed::EndPin();
				}
			}

			ImGui::EndGroup();
			ImGui::SameLine();
			ImGui::BeginGroup();

			if (Type != nullptr)
			{
				for (const FRenderGraphResourceDesc& Output : Type->Outputs)
				{
					const FRenderGraphResourceRef Ref{ Pass.Name, Output.Name };
					const int32 PinId = Ids.GetPinId(Ref);
					if (PinId == 0)
					{
						continue;
					}

					const int32 OutputSlot = Graph.FindGraphOutputSlot(Ref);
					const bool bIsGraphOutput = OutputSlot >= 0;

					ed::BeginPin(ed::PinId(PinId), ed::PinKind::Output);

					// A marked output is what the graph produces, so it is called out in the node rather
					// than only in the issue list.
					//
					// The slot number is shown because a graph may mark several outputs, and they are told
					// apart by slot: the plan is for each to drive its own viewport, so "out 0" is what a
					// reader needs rather than a mark saying only "this one leaves the graph".
					if (bIsGraphOutput)
					{
						ImGui::PushStyleColor(ImGuiCol_Text, GraphOutputColour);
						ImGui::Text("[out %d]", OutputSlot);
						ImGui::PopStyleColor();
						ImGui::SameLine();
					}

					ImGui::TextUnformatted(Output.Name.c_str());
					ImGui::SameLine();
					ImVec2 Pivot;
					DrawPinMarker(bIsGraphOutput ? GraphOutputColour : OutputPinColour, Pivot);
					ed::PinPivotRect(Pivot, Pivot);
					ed::EndPin();

					// Recorded rather than acted on here: the toggle needs a popup, and popups cannot be
					// opened inside a node without suspending the canvas first. The panel does that.
					if (ImGui::IsItemHovered())
					{
						OutHover.HoveredOutput = Ref;
						OutHover.bValid = true;
					}
				}
			}

			ImGui::EndGroup();

			ed::EndNode();
		}
	}
} // namespace Lime
