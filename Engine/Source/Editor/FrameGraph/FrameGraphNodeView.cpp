#include "Editor/FrameGraph/FrameGraphNodeView.h"

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

		// Colours chosen for meaning rather than decoration: a graph output has to stand out at a glance,
		// since a graph without one produces nothing, and execution pins have to read as different in kind
		// from the resource pins beside them.
		constexpr ImU32 OutputPinColour = IM_COL32(120, 190, 255, 255);
		constexpr ImU32 InputPinColour = IM_COL32(160, 160, 170, 255);
		constexpr ImU32 GraphOutputColour = IM_COL32(143, 212, 89, 255);
		constexpr ImU32 ExecutionPinColour = IM_COL32(230, 180, 90, 255);
		constexpr ImU32 SelectedTitleColour = IM_COL32(255, 220, 120, 255);
		constexpr ImU32 TitleColour = IM_COL32(230, 230, 235, 255);
		constexpr ImU32 TypeColour = IM_COL32(150, 150, 160, 255);

		// A filled circle drawn where the widget expects the pin's anchor. The node editor positions the
		// link endpoint at the pin's rectangle, so the marker has to occupy that rectangle rather than being
		// drawn beside it.
		void DrawPinMarker(ImU32 Colour, bool bFilled)
		{
			const float Radius = 4.5f;
			const ImVec2 Cursor = ImGui::GetCursorScreenPos();
			const ImVec2 Centre(Cursor.x + Radius, Cursor.y + ImGui::GetTextLineHeight() * 0.5f);

			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			if (bFilled)
			{
				DrawList->AddCircleFilled(Centre, Radius, Colour);
			}
			else
			{
				DrawList->AddCircle(Centre, Radius, Colour, 0, 1.6f);
			}

			ImGui::Dummy(ImVec2(Radius * 2.0f + 4.0f, ImGui::GetTextLineHeight()));
		}
	} // namespace

	void FFrameGraphIdMap::Reset()
	{
		Next = 1;
		NodeIds.clear();
		PinIds.clear();
		ExecutionInputIds.clear();
		ExecutionOutputIds.clear();
		PassByNode.clear();
		ResourceByPin.clear();
		PassByExecutionPin.clear();
	}

	void FFrameGraphIdMap::Build(const FFrameGraphDesc& Graph, const FFramePassTypeRegistry& Types)
	{
		Reset();

		// Walked in the graph's own pass order, so the ids depend only on the file and not on when anything
		// was added. That is what lets a reload produce the same picture.
		for (const FFramePassInstance& Pass : Graph.GetPasses())
		{
			const int32 NodeId = Next++;
			NodeIds.emplace(Pass.Name, NodeId);
			PassByNode.emplace(NodeId, Pass.Name);

			const int32 ExecutionIn = Next++;
			ExecutionInputIds.emplace(Pass.Name, ExecutionIn);
			PassByExecutionPin.emplace(ExecutionIn, Pass.Name);

			const int32 ExecutionOut = Next++;
			ExecutionOutputIds.emplace(Pass.Name, ExecutionOut);
			PassByExecutionPin.emplace(ExecutionOut, Pass.Name);

			const FFramePassTypeDesc* Type = Types.Find(Pass.TypeName);
			if (Type == nullptr)
			{
				// A pass whose type is unknown still gets a node, so the graph is not silently short a box;
				// it simply has no resource pins to offer.
				continue;
			}

			for (const FFrameResourceDesc& Input : Type->Inputs)
			{
				const int32 PinId = Next++;
				PinIds.emplace(MakePinKey(Pass.Name, Input.Name), PinId);
				ResourceByPin.emplace(PinId, FFrameGraphResourceRef{ Pass.Name, Input.Name });
			}

			for (const FFrameResourceDesc& Output : Type->Outputs)
			{
				const int32 PinId = Next++;
				PinIds.emplace(MakePinKey(Pass.Name, Output.Name), PinId);
				ResourceByPin.emplace(PinId, FFrameGraphResourceRef{ Pass.Name, Output.Name });
			}
		}
	}

	int32 FFrameGraphIdMap::GetNodeId(const std::string& PassName) const
	{
		const auto Found = NodeIds.find(PassName);
		return Found != NodeIds.end() ? Found->second : 0;
	}

	int32 FFrameGraphIdMap::GetPinId(const FFrameGraphResourceRef& Ref) const
	{
		const auto Found = PinIds.find(MakePinKey(Ref.PassName, Ref.ResourceName));
		return Found != PinIds.end() ? Found->second : 0;
	}

	int32 FFrameGraphIdMap::GetExecutionInputId(const std::string& PassName) const
	{
		const auto Found = ExecutionInputIds.find(PassName);
		return Found != ExecutionInputIds.end() ? Found->second : 0;
	}

	int32 FFrameGraphIdMap::GetExecutionOutputId(const std::string& PassName) const
	{
		const auto Found = ExecutionOutputIds.find(PassName);
		return Found != ExecutionOutputIds.end() ? Found->second : 0;
	}

	const std::string* FFrameGraphIdMap::FindPassByNodeId(int32 NodeId) const
	{
		const auto Found = PassByNode.find(NodeId);
		return Found != PassByNode.end() ? &Found->second : nullptr;
	}

	const FFrameGraphResourceRef* FFrameGraphIdMap::FindResourceByPinId(int32 PinId) const
	{
		const auto Found = ResourceByPin.find(PinId);
		return Found != ResourceByPin.end() ? &Found->second : nullptr;
	}

	bool FFrameGraphIdMap::IsExecutionPin(int32 PinId) const
	{
		return PassByExecutionPin.find(PinId) != PassByExecutionPin.end();
	}

	const std::string* FFrameGraphIdMap::FindPassByExecutionPinId(int32 PinId) const
	{
		const auto Found = PassByExecutionPin.find(PinId);
		return Found != PassByExecutionPin.end() ? &Found->second : nullptr;
	}

	void DrawFrameGraphNodes(const FFrameGraphDesc& Graph, const FFramePassTypeRegistry& Types, const FFrameGraphIdMap& Ids,
	                         const std::vector<FFrameGraphNodePlacement>& Placements, bool bApplyPositions, const std::string& SelectedPass,
	                         FFrameGraphHoverState& OutHover)
	{
		OutHover = {};

		for (const FFramePassInstance& Pass : Graph.GetPasses())
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
				const auto Placement = std::find_if(Placements.begin(), Placements.end(), [&Pass](const FFrameGraphNodePlacement& Candidate)
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

			const FFramePassTypeDesc* Type = Types.Find(Pass.TypeName);

			// Two columns via groups: inputs on the left, outputs on the right, which is the direction the
			// graph reads.
			ImGui::BeginGroup();

			// The unlabelled execution input, listed first so it sits at the top edge where Falcor puts it.
			ed::BeginPin(ed::PinId(Ids.GetExecutionInputId(Pass.Name)), ed::PinKind::Input);
			DrawPinMarker(ExecutionPinColour, false);
			ed::EndPin();

			if (Type != nullptr)
			{
				for (const FFrameResourceDesc& Input : Type->Inputs)
				{
					const FFrameGraphResourceRef Ref{ Pass.Name, Input.Name };
					const int32 PinId = Ids.GetPinId(Ref);
					if (PinId == 0)
					{
						continue;
					}

					ed::BeginPin(ed::PinId(PinId), ed::PinKind::Input);
					DrawPinMarker(InputPinColour, true);
					ImGui::SameLine();
					ImGui::TextUnformatted(Input.Name.c_str());
					ed::EndPin();
				}
			}

			ImGui::EndGroup();
			ImGui::SameLine();
			ImGui::BeginGroup();

			ed::BeginPin(ed::PinId(Ids.GetExecutionOutputId(Pass.Name)), ed::PinKind::Output);
			DrawPinMarker(ExecutionPinColour, false);
			ed::EndPin();

			if (Type != nullptr)
			{
				for (const FFrameResourceDesc& Output : Type->Outputs)
				{
					const FFrameGraphResourceRef Ref{ Pass.Name, Output.Name };
					const int32 PinId = Ids.GetPinId(Ref);
					if (PinId == 0)
					{
						continue;
					}

					const bool bIsGraphOutput = Graph.IsGraphOutput(Ref);

					ed::BeginPin(ed::PinId(PinId), ed::PinKind::Output);
					ImGui::TextUnformatted(Output.Name.c_str());
					ImGui::SameLine();
					DrawPinMarker(bIsGraphOutput ? GraphOutputColour : OutputPinColour, true);
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
