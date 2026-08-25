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

#include "FrameGraph/FrameGraphDesc.h"

#include <vector>

namespace Lime
{
	struct FFrameGraphNodePlacement
	{
		std::string PassName;
		float X = 0.0f;
		float Y = 0.0f;
		// Which layer the pass landed in. Also shown in the inspector, where it reads as the execution
		// stage: everything in layer 0 can run before anything in layer 1.
		int32 Layer = 0;
	};

	struct FFrameGraphLayoutSettings
	{
		// Horizontal distance between layers.
		//
		// A compromise the canvas forces: it scales the whole graph to fit the visible area, so generous
		// spacing turns into a smaller zoom and less readable text, while spacing tight enough to maximise
		// zoom leaves adjacent nodes touching. This is wide enough to separate them and no wider.
		float LayerSpacing = 240.0f;
		// Vertical distance between nodes in the same layer. Enough to keep the edges into a node distinct
		// without stretching the graph past the height of the canvas.
		float NodeSpacing = 130.0f;
		// Up and down sweeps of the barycentre ordering. Four is where the crossing count stops improving
		// noticeably on graphs this size; more iterations cost nothing but buy nothing either.
		int32 OrderingIterations = 4;
	};

	// Placements for every pass, in the graph's own pass order so a caller can pair them up by index.
	//
	// Only data edges decide layers. Execution edges constrain order but not data flow, and letting them
	// push layers apart would stretch the graph for a dependency the reader cannot see in the connections.
	// They still influence ordering within a layer, which is where they help.
	std::vector<FFrameGraphNodePlacement> ComputeFrameGraphLayout(const FFrameGraphDesc& Graph,
	                                                              const FFrameGraphLayoutSettings& Settings = {});
} // namespace Lime
