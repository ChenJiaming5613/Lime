// Frame graph layout tests.
//
// Determinism is the property under test, more than aesthetics. The file holds no layout, so this runs on
// every load; if it could return two answers for one graph, the same file would look different each time
// it was opened and that would read as corruption rather than as a layout choice.
//
// Layer correctness is the other half: a consumer must never be drawn at or before its producer, since the
// left to right reading of the graph is the only thing that conveys execution order.

#include "FrameGraph/FrameGraphLayout.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>

using namespace Lime;

namespace
{
	FFramePassTypeRegistry MakeTestRegistry()
	{
		FFramePassTypeRegistry Registry;
		Registry.Clear();

		FFramePassTypeDesc Source;
		Source.Name = "Source";
		Source.Outputs.push_back(FFrameResourceDesc{ "out", EFrameResourceKind::Texture, "RGBA8_UNORM" });
		Registry.Register(std::move(Source));

		FFramePassTypeDesc Filter;
		Filter.Name = "Filter";
		Filter.Inputs.push_back(FFrameResourceDesc{ "in", EFrameResourceKind::Texture, "RGBA8_UNORM" });
		Filter.Outputs.push_back(FFrameResourceDesc{ "out", EFrameResourceKind::Texture, "RGBA8_UNORM" });
		Registry.Register(std::move(Filter));

		FFramePassTypeDesc Merge;
		Merge.Name = "Merge";
		Merge.Inputs.push_back(FFrameResourceDesc{ "a", EFrameResourceKind::Texture, "RGBA8_UNORM" });
		Merge.Inputs.push_back(FFrameResourceDesc{ "b", EFrameResourceKind::Texture, "RGBA8_UNORM" });
		Merge.Outputs.push_back(FFrameResourceDesc{ "out", EFrameResourceKind::Texture, "RGBA8_UNORM" });
		Registry.Register(std::move(Merge));

		return Registry;
	}

	FFrameGraphEdge MakeDataEdge(std::string FromPass, std::string FromResource, std::string ToPass, std::string ToResource)
	{
		FFrameGraphEdge Edge;
		Edge.Kind = EFrameEdgeKind::Data;
		Edge.From = FFrameGraphResourceRef{ std::move(FromPass), std::move(FromResource) };
		Edge.To = FFrameGraphResourceRef{ std::move(ToPass), std::move(ToResource) };
		return Edge;
	}

	std::map<std::string, FFrameGraphNodePlacement> ByName(const std::vector<FFrameGraphNodePlacement>& Placements)
	{
		std::map<std::string, FFrameGraphNodePlacement> Result;
		for (const FFrameGraphNodePlacement& Placement : Placements)
		{
			Result.emplace(Placement.PassName, Placement);
		}
		return Result;
	}
} // namespace

TEST_CASE("Layers follow the data flow", "[FrameGraph][Layout]")
{
	const FFramePassTypeRegistry Types = MakeTestRegistry();
	FFrameGraphDesc Graph;
	FFrameGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("B", "Filter", Types, Issue));
	REQUIRE(Graph.AddPass("C", "Filter", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("A", "out", "B", "in"), Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("B", "out", "C", "in"), Types, Issue));

	const std::vector<FFrameGraphNodePlacement> Placements = ComputeFrameGraphLayout(Graph);
	REQUIRE(Placements.size() == 3);

	const auto Named = ByName(Placements);

	SECTION("Every data edge points forward")
	{
		// The left to right order is what tells the reader which pass runs first, so a consumer sitting at
		// or before its producer would state the opposite of the truth.
		for (const FFrameGraphEdge& Edge : Graph.GetEdges())
		{
			if (Edge.Kind != EFrameEdgeKind::Data)
			{
				continue;
			}
			REQUIRE(Named.at(Edge.To.PassName).Layer > Named.at(Edge.From.PassName).Layer);
			REQUIRE(Named.at(Edge.To.PassName).X > Named.at(Edge.From.PassName).X);
		}
	}

	SECTION("A chain occupies consecutive layers")
	{
		REQUIRE(Named.at("A").Layer == 0);
		REQUIRE(Named.at("B").Layer == 1);
		REQUIRE(Named.at("C").Layer == 2);
	}
}

TEST_CASE("A pass waits for its latest producer", "[FrameGraph][Layout]")
{
	// Longest path layering: Merge reads from a source and from the end of a two step chain, so it belongs
	// after the chain rather than one step after the source.
	const FFramePassTypeRegistry Types = MakeTestRegistry();
	FFrameGraphDesc Graph;
	FFrameGraphIssue Issue;
	REQUIRE(Graph.AddPass("Src", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("Mid", "Filter", Types, Issue));
	REQUIRE(Graph.AddPass("Late", "Filter", Types, Issue));
	REQUIRE(Graph.AddPass("Join", "Merge", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("Src", "out", "Mid", "in"), Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("Mid", "out", "Late", "in"), Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("Late", "out", "Join", "a"), Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("Src", "out", "Join", "b"), Types, Issue));

	const auto Named = ByName(ComputeFrameGraphLayout(Graph));
	REQUIRE(Named.at("Src").Layer == 0);
	REQUIRE(Named.at("Mid").Layer == 1);
	REQUIRE(Named.at("Late").Layer == 2);
	REQUIRE(Named.at("Join").Layer == 3);
}

TEST_CASE("The layout is the same every time", "[FrameGraph][Layout]")
{
	const FFramePassTypeRegistry Types = MakeTestRegistry();
	FFrameGraphDesc Graph;
	FFrameGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("B", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("C", "Merge", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("A", "out", "C", "a"), Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("B", "out", "C", "b"), Types, Issue));

	// Two runs of the same graph, compared coordinate by coordinate. Nothing here may depend on the
	// iteration order of a hash container or on where a node happened to start.
	const std::vector<FFrameGraphNodePlacement> First = ComputeFrameGraphLayout(Graph);
	const std::vector<FFrameGraphNodePlacement> Second = ComputeFrameGraphLayout(Graph);

	REQUIRE(First.size() == Second.size());
	for (SizeType Index = 0; Index < First.size(); ++Index)
	{
		REQUIRE(First[Index].PassName == Second[Index].PassName);
		REQUIRE(First[Index].Layer == Second[Index].Layer);
		REQUIRE(First[Index].X == Second[Index].X);
		REQUIRE(First[Index].Y == Second[Index].Y);
	}
}

TEST_CASE("Nodes in one layer do not overlap", "[FrameGraph][Layout]")
{
	const FFramePassTypeRegistry Types = MakeTestRegistry();
	FFrameGraphDesc Graph;
	FFrameGraphIssue Issue;
	// Four sources, so one layer has several nodes to separate.
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("B", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("C", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("D", "Source", Types, Issue));

	const std::vector<FFrameGraphNodePlacement> Placements = ComputeFrameGraphLayout(Graph);

	std::vector<float> Ys;
	for (const FFrameGraphNodePlacement& Placement : Placements)
	{
		REQUIRE(Placement.Layer == 0);
		Ys.push_back(Placement.Y);
	}

	std::sort(Ys.begin(), Ys.end());
	REQUIRE(std::adjacent_find(Ys.begin(), Ys.end()) == Ys.end());
}

TEST_CASE("Degenerate graphs lay out without failing", "[FrameGraph][Layout]")
{
	const FFramePassTypeRegistry Types = MakeTestRegistry();

	SECTION("An empty graph")
	{
		const FFrameGraphDesc Graph;
		REQUIRE(ComputeFrameGraphLayout(Graph).empty());
	}

	SECTION("Isolated passes with no edges")
	{
		// A graph being assembled looks like this, so it has to place every node rather than only those
		// reachable from some root.
		FFrameGraphDesc Graph;
		FFrameGraphIssue Issue;
		REQUIRE(Graph.AddPass("Lonely", "Source", Types, Issue));
		REQUIRE(Graph.AddPass("AlsoLonely", "Filter", Types, Issue));

		const std::vector<FFrameGraphNodePlacement> Placements = ComputeFrameGraphLayout(Graph);
		REQUIRE(Placements.size() == 2);
		for (const FFrameGraphNodePlacement& Placement : Placements)
		{
			REQUIRE(Placement.Layer == 0);
		}
	}

	SECTION("Several roots feeding one sink")
	{
		FFrameGraphDesc Graph;
		FFrameGraphIssue Issue;
		REQUIRE(Graph.AddPass("R1", "Source", Types, Issue));
		REQUIRE(Graph.AddPass("R2", "Source", Types, Issue));
		REQUIRE(Graph.AddPass("Join", "Merge", Types, Issue));
		REQUIRE(Graph.AddEdge(MakeDataEdge("R1", "out", "Join", "a"), Types, Issue));
		REQUIRE(Graph.AddEdge(MakeDataEdge("R2", "out", "Join", "b"), Types, Issue));

		const auto Named = ByName(ComputeFrameGraphLayout(Graph));
		REQUIRE(Named.at("R1").Layer == 0);
		REQUIRE(Named.at("R2").Layer == 0);
		REQUIRE(Named.at("Join").Layer == 1);
		// Both roots share a layer, so they must be at the same X and different Y.
		REQUIRE(Named.at("R1").X == Named.at("R2").X);
		REQUIRE(Named.at("R1").Y != Named.at("R2").Y);
	}
}

TEST_CASE("Execution edges order without stretching layers", "[FrameGraph][Layout]")
{
	// An execution edge constrains order but carries no data, so letting it push layers apart would
	// stretch the graph for a dependency the reader cannot see in the connections.
	const FFramePassTypeRegistry Types = MakeTestRegistry();
	FFrameGraphDesc Graph;
	FFrameGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("B", "Source", Types, Issue));

	FFrameGraphEdge Ordering;
	Ordering.Kind = EFrameEdgeKind::Execution;
	Ordering.From = FFrameGraphResourceRef{ "A", "" };
	Ordering.To = FFrameGraphResourceRef{ "B", "" };
	REQUIRE(Graph.AddEdge(Ordering, Types, Issue));

	const auto Named = ByName(ComputeFrameGraphLayout(Graph));
	REQUIRE(Named.at("A").Layer == 0);
	REQUIRE(Named.at("B").Layer == 0);
}
