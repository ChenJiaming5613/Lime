// Frame graph model tests.
//
// The properties here are the ones the rest of the system relies on rather than merely observable
// behaviour: that a dangling reference cannot be created, that a cycle cannot be introduced, and that
// removing a pass leaves nothing pointing at it. Each of those is an assumption the layout algorithm and
// the panel are written against, so a break here would surface much later as a hang or a crash.

#include "FrameGraph/FrameGraphDesc.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace Lime;

namespace
{
	// A registry with just enough shape for the cases below: a producer, a consumer that also produces,
	// and a sink. Deliberately not the built-in set, so these tests do not break when the built-ins change.
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

		FFramePassTypeDesc Sink;
		Sink.Name = "Sink";
		Sink.Inputs.push_back(FFrameResourceDesc{ "in", EFrameResourceKind::Texture, "RGBA8_UNORM" });
		Registry.Register(std::move(Sink));

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

	// Builds Source -> Filter -> Sink, the shape most cases start from.
	FFrameGraphDesc MakeChain(const FFramePassTypeRegistry& Types)
	{
		FFrameGraphDesc Graph;
		FFrameGraphIssue Issue;
		REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
		REQUIRE(Graph.AddPass("B", "Filter", Types, Issue));
		REQUIRE(Graph.AddPass("C", "Sink", Types, Issue));
		REQUIRE(Graph.AddEdge(MakeDataEdge("A", "out", "B", "in"), Types, Issue));
		REQUIRE(Graph.AddEdge(MakeDataEdge("B", "out", "C", "in"), Types, Issue));
		return Graph;
	}
} // namespace

TEST_CASE("Pass instances are identified by name", "[FrameGraph]")
{
	const FFramePassTypeRegistry Types = MakeTestRegistry();
	FFrameGraphDesc Graph;
	FFrameGraphIssue Issue;

	SECTION("A pass of a known type is added")
	{
		REQUIRE(Graph.AddPass("First", "Source", Types, Issue));
		REQUIRE(Graph.GetPasses().size() == 1);
		REQUIRE(Graph.FindPass("First") != nullptr);
	}

	SECTION("A duplicate name is refused")
	{
		REQUIRE(Graph.AddPass("First", "Source", Types, Issue));
		// Names are how edges refer to passes, so two passes sharing one would make an edge ambiguous.
		REQUIRE_FALSE(Graph.AddPass("First", "Filter", Types, Issue));
		REQUIRE(Issue.IsError());
		REQUIRE(Graph.GetPasses().size() == 1);
	}

	SECTION("An unknown type is refused")
	{
		// Without a type the inputs and outputs are unknown, so no edge could be validated against it.
		REQUIRE_FALSE(Graph.AddPass("First", "NoSuchType", Types, Issue));
		REQUIRE(Issue.IsError());
		REQUIRE(Graph.GetPasses().empty());
	}

	SECTION("An empty name is refused")
	{
		REQUIRE_FALSE(Graph.AddPass("", "Source", Types, Issue));
		REQUIRE(Issue.IsError());
	}
}

TEST_CASE("Data edges are checked against the declared resources", "[FrameGraph]")
{
	const FFramePassTypeRegistry Types = MakeTestRegistry();
	FFrameGraphDesc Graph;
	FFrameGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("B", "Filter", Types, Issue));

	SECTION("Output to input is accepted")
	{
		REQUIRE(Graph.AddEdge(MakeDataEdge("A", "out", "B", "in"), Types, Issue));
		REQUIRE(Graph.GetEdges().size() == 1);
	}

	SECTION("An edge naming a missing pass is refused")
	{
		REQUIRE_FALSE(Graph.AddEdge(MakeDataEdge("A", "out", "Nope", "in"), Types, Issue));
		REQUIRE(Graph.GetEdges().empty());
	}

	SECTION("An edge naming a resource the type does not declare is refused")
	{
		// The pass exists but has no such resource, which would leave the edge pointing at nothing.
		REQUIRE_FALSE(Graph.AddEdge(MakeDataEdge("A", "nope", "B", "in"), Types, Issue));
		REQUIRE_FALSE(Graph.AddEdge(MakeDataEdge("A", "out", "B", "nope"), Types, Issue));
		REQUIRE(Graph.GetEdges().empty());
	}

	SECTION("A backwards edge is refused")
	{
		// Input to output would read backwards at execution time. Direction comes from the type's
		// declarations rather than from which end the user dragged first.
		REQUIRE_FALSE(Graph.AddEdge(MakeDataEdge("B", "in", "A", "out"), Types, Issue));
		REQUIRE(Graph.GetEdges().empty());
	}

	SECTION("A second producer for one input is refused")
	{
		REQUIRE(Graph.AddPass("A2", "Source", Types, Issue));
		REQUIRE(Graph.AddEdge(MakeDataEdge("A", "out", "B", "in"), Types, Issue));
		// Two writers into one input has no defined meaning, and keeping the last would look like the
		// first edge silently vanished.
		REQUIRE_FALSE(Graph.AddEdge(MakeDataEdge("A2", "out", "B", "in"), Types, Issue));
		REQUIRE(Graph.GetEdges().size() == 1);
	}

	SECTION("A duplicate edge is refused")
	{
		REQUIRE(Graph.AddEdge(MakeDataEdge("A", "out", "B", "in"), Types, Issue));
		REQUIRE_FALSE(Graph.AddEdge(MakeDataEdge("A", "out", "B", "in"), Types, Issue));
		REQUIRE(Graph.GetEdges().size() == 1);
	}

	SECTION("One output may feed several inputs")
	{
		// This is how a resource read by more than one pass is expressed, so it has to be allowed.
		REQUIRE(Graph.AddPass("B2", "Filter", Types, Issue));
		REQUIRE(Graph.AddEdge(MakeDataEdge("A", "out", "B", "in"), Types, Issue));
		REQUIRE(Graph.AddEdge(MakeDataEdge("A", "out", "B2", "in"), Types, Issue));
		REQUIRE(Graph.GetEdges().size() == 2);
	}
}

TEST_CASE("Cycles are refused", "[FrameGraph]")
{
	// Not defensive programming: the layering pass walks the graph assuming it terminates, so a cycle
	// there is an unbounded loop with no diagnostic. It is refused where it would be introduced.
	const FFramePassTypeRegistry Types = MakeTestRegistry();
	FFrameGraphDesc Graph = MakeChain(Types);
	FFrameGraphIssue Issue;

	SECTION("A self edge is refused")
	{
		REQUIRE_FALSE(Graph.AddEdge(MakeDataEdge("B", "out", "B", "in"), Types, Issue));
	}

	SECTION("A back edge closing a multi hop cycle is refused")
	{
		// C already reaches back to A through B, so C to A would close the loop.
		REQUIRE(Graph.AddPass("D", "Filter", Types, Issue));
		REQUIRE(Graph.AddEdge(MakeDataEdge("B", "out", "D", "in"), Types, Issue));

		FFrameGraphEdge Closing;
		Closing.Kind = EFrameEdgeKind::Execution;
		Closing.From = FFrameGraphResourceRef{ "D", "" };
		Closing.To = FFrameGraphResourceRef{ "A", "" };
		REQUIRE(Graph.WouldCreateCycle(Closing));
		REQUIRE_FALSE(Graph.AddEdge(Closing, Types, Issue));
	}

	SECTION("An execution edge in the same direction as the data flow is fine")
	{
		FFrameGraphEdge Ordering;
		Ordering.Kind = EFrameEdgeKind::Execution;
		Ordering.From = FFrameGraphResourceRef{ "A", "" };
		Ordering.To = FFrameGraphResourceRef{ "C", "" };
		REQUIRE_FALSE(Graph.WouldCreateCycle(Ordering));
		REQUIRE(Graph.AddEdge(Ordering, Types, Issue));
	}
}

TEST_CASE("Removing a pass leaves nothing referring to it", "[FrameGraph]")
{
	const FFramePassTypeRegistry Types = MakeTestRegistry();
	FFrameGraphDesc Graph = MakeChain(Types);
	FFrameGraphIssue Issue;
	REQUIRE(Graph.ToggleGraphOutput(FFrameGraphResourceRef{ "B", "out" }, Types, Issue));

	REQUIRE(Graph.RemovePass("B"));

	// Every other part of the model assumes a reference names a pass that exists, so the edges and the
	// graph output that mentioned B have to go with it.
	REQUIRE(Graph.FindPass("B") == nullptr);
	REQUIRE(Graph.GetEdges().empty());
	REQUIRE(Graph.GetGraphOutputs().empty());
	REQUIRE(Graph.GetPasses().size() == 2);
}

TEST_CASE("Graph outputs may only name outputs", "[FrameGraph]")
{
	const FFramePassTypeRegistry Types = MakeTestRegistry();
	FFrameGraphDesc Graph = MakeChain(Types);
	FFrameGraphIssue Issue;

	SECTION("Marking and unmarking an output")
	{
		REQUIRE(Graph.ToggleGraphOutput(FFrameGraphResourceRef{ "B", "out" }, Types, Issue));
		REQUIRE(Graph.IsGraphOutput(FFrameGraphResourceRef{ "B", "out" }));

		// The same call toggles, so a second one clears it.
		REQUIRE_FALSE(Graph.ToggleGraphOutput(FFrameGraphResourceRef{ "B", "out" }, Types, Issue));
		REQUIRE_FALSE(Graph.IsGraphOutput(FFrameGraphResourceRef{ "B", "out" }));
	}

	SECTION("An input cannot be a graph output")
	{
		// Marking an input would claim the graph produces something it only consumes.
		REQUIRE_FALSE(Graph.ToggleGraphOutput(FFrameGraphResourceRef{ "B", "in" }, Types, Issue));
		REQUIRE_FALSE(Issue.Message.empty());
	}
}

TEST_CASE("Validate reports each kind of problem", "[FrameGraph]")
{
	const FFramePassTypeRegistry Types = MakeTestRegistry();

	SECTION("A complete graph with a marked output is clean")
	{
		FFrameGraphDesc Graph = MakeChain(Types);
		FFrameGraphIssue Issue;
		REQUIRE(Graph.ToggleGraphOutput(FFrameGraphResourceRef{ "B", "out" }, Types, Issue));

		const std::vector<FFrameGraphIssue> Issues = Graph.Validate(Types);
		const auto Errors = std::count_if(Issues.begin(), Issues.end(), [](const FFrameGraphIssue& Item) { return Item.IsError(); });
		REQUIRE(Errors == 0);
	}

	SECTION("A graph with no marked output is an error")
	{
		// Every pass would be culled by a real frame graph, so the graph produces nothing.
		const FFrameGraphDesc Graph = MakeChain(Types);
		const std::vector<FFrameGraphIssue> Issues = Graph.Validate(Types);
		REQUIRE(std::any_of(Issues.begin(), Issues.end(), [](const FFrameGraphIssue& Item) { return Item.IsError(); }));
	}

	SECTION("An unconnected input is a warning, not an error")
	{
		// A pass whose input is not yet wired is a graph still being assembled, which is normal while editing.
		FFrameGraphDesc Graph;
		FFrameGraphIssue Issue;
		REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
		REQUIRE(Graph.AddPass("B", "Filter", Types, Issue));
		REQUIRE(Graph.ToggleGraphOutput(FFrameGraphResourceRef{ "B", "out" }, Types, Issue));

		const std::vector<FFrameGraphIssue> Issues = Graph.Validate(Types);
		REQUIRE(std::any_of(Issues.begin(), Issues.end(), [](const FFrameGraphIssue& Item) { return !Item.IsError(); }));
		const auto Errors = std::count_if(Issues.begin(), Issues.end(), [](const FFrameGraphIssue& Item) { return Item.IsError(); });
		REQUIRE(Errors == 0);
	}
}
