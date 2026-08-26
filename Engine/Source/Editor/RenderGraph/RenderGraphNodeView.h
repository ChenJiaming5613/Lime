// Draws one pass as a node.
//
// Separated from the panel because this is the part most likely to change: what a node looks like is a
// presentation decision, while the panel's job is loading, layout and interaction. Keeping them apart
// stops the panel from turning into a wall of drawing calls.
//
// The pin ids a node needs are not stored anywhere. They are derived from the pass name and the resource
// name, so the same graph always produces the same ids within a session and nothing has to be kept in
// sync with the data.

#pragma once

#include "RenderGraph/RenderGraphLayout.h"

#include <imgui.h>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace Lime
{
	// Maps between the names the graph uses and the integer ids the node widget requires.
	//
	// The widget needs one id space shared by nodes and pins, so both come from a single counter. Two
	// counters would eventually hand out the same number for a node and a pin, and the widget would treat
	// one as the other.
	class FRenderGraphIdMap
	{
	public:
		void Reset();

		// Assigns ids for every pass and every pin it has, in the graph's pass order. Deterministic, so a
		// reload of the same file produces the same ids and a saved screenshot still matches.
		void Build(const FRenderGraphDesc& Graph, const FRenderGraphPassTypeRegistry& Types);

		int32 GetNodeId(const std::string& PassName) const;
		int32 GetPinId(const FRenderGraphResourceRef& Ref) const;

		// Reverse lookups, for turning what the widget reports back into graph terms.
		const std::string* FindPassByNodeId(int32 NodeId) const;
		const FRenderGraphResourceRef* FindResourceByPinId(int32 PinId) const;

	private:
		// Claims an id, stepping past a hash collision. Also keeps item ids below LinkIdBase so they cannot
		// meet a link id.
		int32 ClaimId(int32 Preferred);

		std::set<int32> UsedIds;
		std::map<std::string, int32> NodeIds;
		std::map<std::string, int32> PinIds;

		std::map<int32, std::string> PassByNode;
		std::map<int32, FRenderGraphResourceRef> ResourceByPin;

	public:
		// Links share the widget's id space with nodes and pins, so they are numbered from a base that
		// item ids never reach. Public because the panel numbers the links it draws.
		static constexpr int32 LinkIdBase = 1000000;
	};

	// Which resource the mouse is over, so the panel can offer the graph output toggle. Empty pass name
	// means nothing is hovered.
	struct FRenderGraphHoverState
	{
		FRenderGraphResourceRef HoveredOutput;
		bool bValid = false;
	};

	// Draws every pass in the graph. Positions come from the layout, applied only when bApplyPositions is
	// set: after that the widget owns them, so a drag is not undone on the next frame.
	void DrawRenderGraphNodes(const FRenderGraphDesc& Graph, const FRenderGraphPassTypeRegistry& Types, const FRenderGraphIdMap& Ids,
	                         const std::vector<FRenderGraphNodePlacement>& Placements, bool bApplyPositions, const std::string& SelectedPass,
	                         FRenderGraphHoverState& OutHover);
} // namespace Lime
