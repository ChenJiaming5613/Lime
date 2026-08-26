// Where to draw each pass, computed rather than stored.
//
// Sugiyama style layered layout, in three of its usual four stages: assign layers, order within each
// layer to reduce crossings, then assign coordinates. The fourth stage, routing edges through dummy
// nodes so long edges bend cleanly, is skipped: the node widget draws edges as bezier curves anyway, so
// dummy nodes would change how the curves look without changing what the reader can follow, and a full
// Brandes-Kopf coordinate pass is several times the code.
//
// Determinism is a requirement, not a nicety. The file deliberately holds no layout, so this runs on
// every load; if it could return two different answers for one graph, the same file would look different
// each time it was opened and the difference would look like corruption. Every ordering step therefore
// breaks ties by name, and nothing depends on iteration order of an unordered container.

#pragma once

#include "RenderGraph/RenderGraphDesc.h"

#include <string>
#include <vector>

namespace Lime
{
	struct FRenderGraphNodePlacement
	{
		std::string PassName;
		float X = 0.0f;
		float Y = 0.0f;
		// Which layer the pass landed in. Also shown in the inspector, where it reads as the execution
		// stage: everything in layer 0 can run before anything in layer 1.
		int32 Layer = 0;
	};

	struct FRenderGraphLayoutSettings
	{
		// Gap left between one layer's widest node and the start of the next layer.
		//
		// Spacing is a gap rather than a stride: a stride has to be wider than the widest node that will
		// ever appear, and a node is as wide as its longest resource name, which is not known here. Adding
		// a gap to the measured width instead keeps neighbours apart whatever they contain.
		float LayerSpacing = 90.0f;
		// Vertical gap between the bottom of one node and the top of the next in the same layer.
		float NodeSpacing = 40.0f;
		// Used for a pass whose node has not been measured yet, which is every pass on the frame a graph is
		// loaded: the widget only knows a node's size once it has drawn it. Close to what a small node
		// measures, so the first layout is not far off and the one after it is exact.
		float FallbackNodeWidth = 180.0f;
		float FallbackNodeHeight = 90.0f;
		// Up and down sweeps of the barycentre ordering. Four is where the crossing count stops improving
		// noticeably on graphs this size; more iterations cost nothing but buy nothing either.
		int32 OrderingIterations = 4;
	};

	// What a node measures on screen, so the layout can leave room for it.
	//
	// Supplied by the caller because only the widget that drew a node knows how big it turned out. Passes
	// missing from this list fall back to the sizes in the settings.
	struct FRenderGraphNodeSize
	{
		std::string PassName;
		float Width = 0.0f;
		float Height = 0.0f;
	};

	// Placements for every pass, in the graph's own pass order so a caller can pair them up by index.
	//
	// Layers come from the edges: a pass sits one layer after the latest of the passes it reads from.
	//
	// NodeSizes lets the result account for how big the nodes actually are. Without it the layout has to
	// assume a size, and a node wider than that assumption overlaps its neighbour.
	std::vector<FRenderGraphNodePlacement> ComputeRenderGraphLayout(const FRenderGraphDesc& Graph,
	                                                              const std::vector<FRenderGraphNodeSize>& NodeSizes = {},
	                                                              const FRenderGraphLayoutSettings& Settings = {});
} // namespace Lime
