#include "FrameGraph/FrameGraphLayout.h"

#include <algorithm>
#include <map>
#include <numeric>

namespace Lime
{
	namespace
	{
		// Adjacency by pass index, built once so the stages below do not rescan the edge list.
		struct FAdjacency
		{
			// Data edge successors and predecessors, which is what layering and ordering read.
			std::vector<std::vector<SizeType>> DataSuccessors;
			std::vector<std::vector<SizeType>> DataPredecessors;
			// Execution edges kept apart: they take part in ordering but must not affect layering.
			std::vector<std::vector<SizeType>> OrderSuccessors;
			std::vector<std::vector<SizeType>> OrderPredecessors;
		};

		// Sorted and deduplicated, so two graphs differing only in edge order produce identical adjacency
		// and therefore identical layout.
		void Normalize(std::vector<std::vector<SizeType>>& Lists)
		{
			for (std::vector<SizeType>& List : Lists)
			{
				std::sort(List.begin(), List.end());
				List.erase(std::unique(List.begin(), List.end()), List.end());
			}
		}

		FAdjacency BuildAdjacency(const FFrameGraphDesc& Graph, const std::map<std::string, SizeType>& IndexByName)
		{
			const SizeType Count = Graph.GetPasses().size();

			FAdjacency Adjacency;
			Adjacency.DataSuccessors.resize(Count);
			Adjacency.DataPredecessors.resize(Count);
			Adjacency.OrderSuccessors.resize(Count);
			Adjacency.OrderPredecessors.resize(Count);

			for (const FFrameGraphEdge& Edge : Graph.GetEdges())
			{
				const auto From = IndexByName.find(Edge.From.PassName);
				const auto To = IndexByName.find(Edge.To.PassName);
				// An edge naming a missing pass is dropped here rather than asserted: the loader already
				// reports those, and the layout should still place whatever passes do exist.
				if (From == IndexByName.end() || To == IndexByName.end())
				{
					continue;
				}

				if (Edge.Kind == EFrameEdgeKind::Data)
				{
					Adjacency.DataSuccessors[From->second].push_back(To->second);
					Adjacency.DataPredecessors[To->second].push_back(From->second);
				}

				// Both kinds order passes relative to each other, so both feed the ordering stage.
				Adjacency.OrderSuccessors[From->second].push_back(To->second);
				Adjacency.OrderPredecessors[To->second].push_back(From->second);
			}

			Normalize(Adjacency.DataSuccessors);
			Normalize(Adjacency.DataPredecessors);
			Normalize(Adjacency.OrderSuccessors);
			Normalize(Adjacency.OrderPredecessors);
			return Adjacency;
		}

		// Longest path layering: a pass sits one layer after the latest of its producers.
		//
		// Computed by relaxation over a topological order rather than by recursion, so a cycle that slipped
		// through cannot become an unbounded descent. The graph is supposed to be acyclic; this makes the
		// consequence of it not being so a slightly wrong picture instead of a crash.
		std::vector<int32> AssignLayers(const FAdjacency& Adjacency, SizeType Count)
		{
			std::vector<int32> Layers(Count, 0);
			std::vector<SizeType> Remaining(Count);
			for (SizeType Index = 0; Index < Count; ++Index)
			{
				Remaining[Index] = Adjacency.DataPredecessors[Index].size();
			}

			// Sources in index order, which is the order passes appear in the file.
			std::vector<SizeType> Ready;
			for (SizeType Index = 0; Index < Count; ++Index)
			{
				if (Remaining[Index] == 0)
				{
					Ready.push_back(Index);
				}
			}

			SizeType Processed = 0;
			while (!Ready.empty())
			{
				// Smallest index first: a queue keyed on insertion order would make the result depend on the
				// order edges happened to be listed in.
				const auto Next = std::min_element(Ready.begin(), Ready.end());
				const SizeType Current = *Next;
				Ready.erase(Next);
				++Processed;

				for (const SizeType Successor : Adjacency.DataSuccessors[Current])
				{
					Layers[Successor] = std::max(Layers[Successor], Layers[Current] + 1);
					if (--Remaining[Successor] == 0)
					{
						Ready.push_back(Successor);
					}
				}
			}

			// Whatever a cycle left unvisited is placed after its producers as far as they were resolved,
			// which keeps every node on screen.
			if (Processed != Count)
			{
				for (SizeType Index = 0; Index < Count; ++Index)
				{
					if (Remaining[Index] == 0)
					{
						continue;
					}
					for (const SizeType Predecessor : Adjacency.DataPredecessors[Index])
					{
						Layers[Index] = std::max(Layers[Index], Layers[Predecessor] + 1);
					}
				}
			}

			return Layers;
		}

		// Mean position of a node's neighbours in the adjacent layer, which is what the ordering sweep sorts
		// on. Returns a negative value when there are no neighbours, meaning "no opinion".
		float Barycentre(const std::vector<SizeType>& Neighbours, const std::vector<SizeType>& PositionInLayer)
		{
			if (Neighbours.empty())
			{
				return -1.0f;
			}

			SizeType Sum = 0;
			for (const SizeType Neighbour : Neighbours)
			{
				Sum += PositionInLayer[Neighbour];
			}
			return static_cast<float>(Sum) / static_cast<float>(Neighbours.size());
		}

		// Orders each layer to reduce edge crossings, alternating sweeps down and up.
		//
		// A node with no neighbours in the layer being referenced keeps its current position, so the sweeps
		// only move nodes that have a reason to move. Ties break on name, which is what makes repeated runs
		// on the same graph identical.
		void OrderWithinLayers(const FFrameGraphDesc& Graph, const FAdjacency& Adjacency, int32 Iterations,
		                       std::vector<std::vector<SizeType>>& OutLayerContents)
		{
			const std::vector<FFramePassInstance>& Passes = Graph.GetPasses();
			const SizeType Count = Passes.size();

			std::vector<SizeType> PositionInLayer(Count, 0);
			const auto RefreshPositions = [&]
			{
				for (const std::vector<SizeType>& Layer : OutLayerContents)
				{
					for (SizeType Slot = 0; Slot < Layer.size(); ++Slot)
					{
						PositionInLayer[Layer[Slot]] = Slot;
					}
				}
			};
			RefreshPositions();

			for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
			{
				const bool bDownward = Iteration % 2 == 0;

				// Downward sweeps order a layer by where its producers sit; upward sweeps by its consumers.
				// Doing both is what lets a node settle between the two, rather than only aligning with one side.
				for (SizeType LayerIndex = 0; LayerIndex < OutLayerContents.size(); ++LayerIndex)
				{
					const SizeType Actual = bDownward ? LayerIndex : OutLayerContents.size() - 1 - LayerIndex;
					std::vector<SizeType>& Layer = OutLayerContents[Actual];

					std::vector<std::pair<float, SizeType>> Keys;
					Keys.reserve(Layer.size());
					for (SizeType Slot = 0; Slot < Layer.size(); ++Slot)
					{
						const SizeType Node = Layer[Slot];
						const std::vector<SizeType>& Neighbours =
						    bDownward ? Adjacency.OrderPredecessors[Node] : Adjacency.OrderSuccessors[Node];
						float Key = Barycentre(Neighbours, PositionInLayer);
						if (Key < 0.0f)
						{
							// No neighbours to align with, so it stays where it is.
							Key = static_cast<float>(Slot);
						}
						Keys.emplace_back(Key, Node);
					}

					std::stable_sort(Keys.begin(), Keys.end(),
					                 [&Passes](const std::pair<float, SizeType>& Left, const std::pair<float, SizeType>& Right)
					                 {
						                 if (Left.first != Right.first)
						                 {
							                 return Left.first < Right.first;
						                 }
						                 // Equal barycentres are ordered by name, which is stable across runs in a
						                 // way that the previous slot order is not once other layers have moved.
						                 return Passes[Left.second].Name < Passes[Right.second].Name;
					                 });

					for (SizeType Slot = 0; Slot < Layer.size(); ++Slot)
					{
						Layer[Slot] = Keys[Slot].second;
					}

					RefreshPositions();
				}
			}
		}
	} // namespace

	std::vector<FFrameGraphNodePlacement> ComputeFrameGraphLayout(const FFrameGraphDesc& Graph, const FFrameGraphLayoutSettings& Settings)
	{
		const std::vector<FFramePassInstance>& Passes = Graph.GetPasses();
		std::vector<FFrameGraphNodePlacement> Placements;
		Placements.reserve(Passes.size());

		if (Passes.empty())
		{
			return Placements;
		}

		// std::map rather than unordered_map: nothing here iterates it, but keeping every container ordered
		// removes a whole class of "why did the layout change" question.
		std::map<std::string, SizeType> IndexByName;
		for (SizeType Index = 0; Index < Passes.size(); ++Index)
		{
			IndexByName.emplace(Passes[Index].Name, Index);
		}

		const FAdjacency Adjacency = BuildAdjacency(Graph, IndexByName);
		const std::vector<int32> Layers = AssignLayers(Adjacency, Passes.size());

		const int32 MaxLayer = *std::max_element(Layers.begin(), Layers.end());
		std::vector<std::vector<SizeType>> LayerContents(static_cast<SizeType>(MaxLayer) + 1);
		for (SizeType Index = 0; Index < Passes.size(); ++Index)
		{
			LayerContents[static_cast<SizeType>(Layers[Index])].push_back(Index);
		}

		// Seeded by name so the starting order is already deterministic, before any sweep runs.
		for (std::vector<SizeType>& Layer : LayerContents)
		{
			std::sort(Layer.begin(), Layer.end(),
			          [&Passes](SizeType Left, SizeType Right) { return Passes[Left].Name < Passes[Right].Name; });
		}

		OrderWithinLayers(Graph, Adjacency, Settings.OrderingIterations, LayerContents);

		// Each layer is centred vertically about zero, so a graph with layers of different sizes reads as a
		// band rather than hanging from the top edge.
		Placements.resize(Passes.size());
		for (SizeType Index = 0; Index < Passes.size(); ++Index)
		{
			Placements[Index].PassName = Passes[Index].Name;
			Placements[Index].Layer = Layers[Index];
		}

		for (SizeType LayerIndex = 0; LayerIndex < LayerContents.size(); ++LayerIndex)
		{
			const std::vector<SizeType>& Layer = LayerContents[LayerIndex];
			const float Height = static_cast<float>(Layer.size() - 1) * Settings.NodeSpacing;
			const float Top = -Height * 0.5f;

			for (SizeType Slot = 0; Slot < Layer.size(); ++Slot)
			{
				FFrameGraphNodePlacement& Placement = Placements[Layer[Slot]];
				Placement.X = static_cast<float>(LayerIndex) * Settings.LayerSpacing;
				Placement.Y = Top + static_cast<float>(Slot) * Settings.NodeSpacing;
			}
		}

		return Placements;
	}
} // namespace Lime
