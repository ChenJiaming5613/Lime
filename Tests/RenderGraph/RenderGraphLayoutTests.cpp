// Render graph layout tests.
//
// Determinism is the property under test, more than aesthetics. The file holds no layout, so this runs on
// every load; if it could return two answers for one graph, the same file would look different each time
// it was opened and that would read as corruption rather than as a layout choice.
//
// Layer correctness is the other half: a consumer must never be drawn at or before its producer, since the
// left to right reading of the graph is the only thing that conveys execution order.

#include "RenderGraph/RenderGraphLayout.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>

using namespace Lime;

namespace
{
	FRenderGraphPassTypeRegistry MakeTestRegistry()
	{
		FRenderGraphPassTypeRegistry Registry;
		Registry.Clear();

		FRenderGraphPassTypeDesc Source;
		Source.Name = "Source";
		Source.Outputs.push_back(FRenderGraphResourceDesc{ "out", EFrameResourceKind::Texture, "RGBA8_UNORM" });
		Registry.Register(std::move(Source));

		FRenderGraphPassTypeDesc Filter;
		Filter.Name = "Filter";
		Filter.Inputs.push_back(FRenderGraphResourceDesc{ "in", EFrameResourceKind::Texture, "RGBA8_UNORM" });
		Filter.Outputs.push_back(FRenderGraphResourceDesc{ "out", EFrameResourceKind::Texture, "RGBA8_UNORM" });
		Registry.Register(std::move(Filter));

		FRenderGraphPassTypeDesc Merge;
		Merge.Name = "Merge";
		Merge.Inputs.push_back(FRenderGraphResourceDesc{ "a", EFrameResourceKind::Texture, "RGBA8_UNORM" });
		Merge.Inputs.push_back(FRenderGraphResourceDesc{ "b", EFrameResourceKind::Texture, "RGBA8_UNORM" });
		Merge.Outputs.push_back(FRenderGraphResourceDesc{ "out", EFrameResourceKind::Texture, "RGBA8_UNORM" });
		Registry.Register(std::move(Merge));

		return Registry;
	}

	FRenderGraphEdge MakeDataEdge(std::string FromPass, std::string FromResource, std::string ToPass, std::string ToResource)
	{
		FRenderGraphEdge Edge;
		Edge.From = FRenderGraphResourceRef{ std::move(FromPass), std::move(FromResource) };
		Edge.To = FRenderGraphResourceRef{ std::move(ToPass), std::move(ToResource) };
		return Edge;
	}

	std::map<std::string, FRenderGraphNodePlacement> ByName(const std::vector<FRenderGraphNodePlacement>& Placements)
	{
		std::map<std::string, FRenderGraphNodePlacement> Result;
		for (const FRenderGraphNodePlacement& Placement : Placements)
		{
			Result.emplace(Placement.PassName, Placement);
		}
		return Result;
	}
} // namespace

TEST_CASE("Layers follow the data flow", "[RenderGraph][Layout]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("B", "Filter", Types, Issue));
	REQUIRE(Graph.AddPass("C", "Filter", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("A", "out", "B", "in"), Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("B", "out", "C", "in"), Types, Issue));

	const std::vector<FRenderGraphNodePlacement> Placements = ComputeRenderGraphLayout(Graph);
	REQUIRE(Placements.size() == 3);

	const auto Named = ByName(Placements);

	SECTION("Every edge points forward")
	{
		// The left to right order is what tells the reader which pass runs first, so a consumer sitting at
		// or before its producer would state the opposite of the truth.
		for (const FRenderGraphEdge& Edge : Graph.GetEdges())
		{
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

TEST_CASE("A pass waits for its latest producer", "[RenderGraph][Layout]")
{
	// Longest path layering: Merge reads from a source and from the end of a two step chain, so it belongs
	// after the chain rather than one step after the source.
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("Src", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("Mid", "Filter", Types, Issue));
	REQUIRE(Graph.AddPass("Late", "Filter", Types, Issue));
	REQUIRE(Graph.AddPass("Join", "Merge", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("Src", "out", "Mid", "in"), Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("Mid", "out", "Late", "in"), Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("Late", "out", "Join", "a"), Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("Src", "out", "Join", "b"), Types, Issue));

	const auto Named = ByName(ComputeRenderGraphLayout(Graph));
	REQUIRE(Named.at("Src").Layer == 0);
	REQUIRE(Named.at("Mid").Layer == 1);
	REQUIRE(Named.at("Late").Layer == 2);
	REQUIRE(Named.at("Join").Layer == 3);
}

TEST_CASE("The layout is the same every time", "[RenderGraph][Layout]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("B", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("C", "Merge", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("A", "out", "C", "a"), Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("B", "out", "C", "b"), Types, Issue));

	// Two runs of the same graph, compared coordinate by coordinate. Nothing here may depend on the
	// iteration order of a hash container or on where a node happened to start.
	const std::vector<FRenderGraphNodePlacement> First = ComputeRenderGraphLayout(Graph);
	const std::vector<FRenderGraphNodePlacement> Second = ComputeRenderGraphLayout(Graph);

	REQUIRE(First.size() == Second.size());
	for (SizeType Index = 0; Index < First.size(); ++Index)
	{
		REQUIRE(First[Index].PassName == Second[Index].PassName);
		REQUIRE(First[Index].Layer == Second[Index].Layer);
		REQUIRE(First[Index].X == Second[Index].X);
		REQUIRE(First[Index].Y == Second[Index].Y);
	}
}

TEST_CASE("Nodes in one layer do not overlap", "[RenderGraph][Layout]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	// Four sources, so one layer has several nodes to separate.
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("B", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("C", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("D", "Source", Types, Issue));

	const std::vector<FRenderGraphNodePlacement> Placements = ComputeRenderGraphLayout(Graph);

	std::vector<float> Ys;
	for (const FRenderGraphNodePlacement& Placement : Placements)
	{
		REQUIRE(Placement.Layer == 0);
		Ys.push_back(Placement.Y);
	}

	std::sort(Ys.begin(), Ys.end());
	REQUIRE(std::adjacent_find(Ys.begin(), Ys.end()) == Ys.end());
}

TEST_CASE("Degenerate graphs lay out without failing", "[RenderGraph][Layout]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();

	SECTION("An empty graph")
	{
		const FRenderGraphDesc Graph;
		REQUIRE(ComputeRenderGraphLayout(Graph).empty());
	}

	SECTION("Isolated passes with no edges")
	{
		// A graph being assembled looks like this, so it has to place every node rather than only those
		// reachable from some root.
		FRenderGraphDesc Graph;
		FRenderGraphIssue Issue;
		REQUIRE(Graph.AddPass("Lonely", "Source", Types, Issue));
		REQUIRE(Graph.AddPass("AlsoLonely", "Filter", Types, Issue));

		const std::vector<FRenderGraphNodePlacement> Placements = ComputeRenderGraphLayout(Graph);
		REQUIRE(Placements.size() == 2);
		for (const FRenderGraphNodePlacement& Placement : Placements)
		{
			REQUIRE(Placement.Layer == 0);
		}
	}

	SECTION("Several roots feeding one sink")
	{
		FRenderGraphDesc Graph;
		FRenderGraphIssue Issue;
		REQUIRE(Graph.AddPass("R1", "Source", Types, Issue));
		REQUIRE(Graph.AddPass("R2", "Source", Types, Issue));
		REQUIRE(Graph.AddPass("Join", "Merge", Types, Issue));
		REQUIRE(Graph.AddEdge(MakeDataEdge("R1", "out", "Join", "a"), Types, Issue));
		REQUIRE(Graph.AddEdge(MakeDataEdge("R2", "out", "Join", "b"), Types, Issue));

		const auto Named = ByName(ComputeRenderGraphLayout(Graph));
		REQUIRE(Named.at("R1").Layer == 0);
		REQUIRE(Named.at("R2").Layer == 0);
		REQUIRE(Named.at("Join").Layer == 1);
		// Both roots share a layer, so they must be at the same X and different Y.
		REQUIRE(Named.at("R1").X == Named.at("R2").X);
		REQUIRE(Named.at("R1").Y != Named.at("R2").Y);
	}
}

TEST_CASE("Measured node sizes keep neighbours apart", "[RenderGraph][Layout]")
{
	// Spacing is a gap added to the measured size rather than a fixed stride, because a node is as wide as
	// its longest resource name and no stride chosen here could be wide enough for every graph. A node
	// wider than its layer's allowance used to reach into the layer beside it.
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("B", "Filter", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeDataEdge("A", "out", "B", "in"), Types, Issue));

	FRenderGraphLayoutSettings Settings;
	Settings.LayerSpacing = 50.0f;
	Settings.NodeSpacing = 20.0f;

	SECTION("A wide node does not overlap the next layer")
	{
		// A is far wider than any fixed stride would have assumed.
		const std::vector<FRenderGraphNodeSize> Sizes{ FRenderGraphNodeSize{ "A", 900.0f, 80.0f },
			                                         FRenderGraphNodeSize{ "B", 120.0f, 80.0f } };

		const auto Named = ByName(ComputeRenderGraphLayout(Graph, Sizes, Settings));
		// B starts past A's right edge, with the gap in between.
		REQUIRE(Named.at("B").X >= Named.at("A").X + 900.0f + Settings.LayerSpacing);
	}

	SECTION("A tall node does not overlap the one below it in the same layer")
	{
		FRenderGraphDesc Siblings;
		FRenderGraphIssue SiblingIssue;
		REQUIRE(Siblings.AddPass("Tall", "Source", Types, SiblingIssue));
		REQUIRE(Siblings.AddPass("Short", "Source", Types, SiblingIssue));

		const std::vector<FRenderGraphNodeSize> Sizes{ FRenderGraphNodeSize{ "Short", 120.0f, 60.0f },
			                                         FRenderGraphNodeSize{ "Tall", 120.0f, 400.0f } };

		const auto Named = ByName(ComputeRenderGraphLayout(Siblings, Sizes, Settings));
		REQUIRE(Named.at("Short").Layer == Named.at("Tall").Layer);

		// Ordering within a layer is by name, so "Short" precedes "Tall". Whichever is first, the second
		// has to start past the first's bottom edge.
		const float ShortBottom = Named.at("Short").Y + 60.0f;
		REQUIRE(Named.at("Tall").Y >= ShortBottom + Settings.NodeSpacing);
	}
}
