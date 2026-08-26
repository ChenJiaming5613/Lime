// Render graph model tests.
//
// The properties here are the ones the rest of the system relies on rather than merely observable
// behaviour: that a dangling reference cannot be created, that a cycle cannot be introduced, and that
// removing a pass leaves nothing pointing at it. Each of those is an assumption the layout algorithm and
// the panel are written against, so a break here would surface much later as a hang or a crash.

#include "RenderGraph/RenderGraphDesc.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace Lime;

namespace
{
	// A registry with just enough shape for the cases below: a producer, a consumer that also produces,
	// and a sink. Deliberately not the built-in set, so these tests do not break when the built-ins change.
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

		FRenderGraphPassTypeDesc Sink;
		Sink.Name = "Sink";
		Sink.Inputs.push_back(FRenderGraphResourceDesc{ "in", EFrameResourceKind::Texture, "RGBA8_UNORM" });
		Registry.Register(std::move(Sink));

		return Registry;
	}

	FRenderGraphEdge MakeDataEdge(std::string FromPass, std::string FromResource, std::string ToPass, std::string ToResource)
	{
		FRenderGraphEdge Edge;
		Edge.From = FRenderGraphResourceRef{ std::move(FromPass), std::move(FromResource) };
		Edge.To = FRenderGraphResourceRef{ std::move(ToPass), std::move(ToResource) };
		return Edge;
	}

	// Builds Source -> Filter -> Sink, the shape most cases start from.
	FRenderGraphDesc MakeChain(const FRenderGraphPassTypeRegistry& Types)
	{
		FRenderGraphDesc Graph;
		FRenderGraphIssue Issue;
		REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
		REQUIRE(Graph.AddPass("B", "Filter", Types, Issue));
		REQUIRE(Graph.AddPass("C", "Sink", Types, Issue));
		REQUIRE(Graph.AddEdge(MakeDataEdge("A", "out", "B", "in"), Types, Issue));
		REQUIRE(Graph.AddEdge(MakeDataEdge("B", "out", "C", "in"), Types, Issue));
		return Graph;
	}
} // namespace

TEST_CASE("Pass instances are identified by name", "[RenderGraph]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;

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

TEST_CASE("Data edges are checked against the declared resources", "[RenderGraph]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
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

TEST_CASE("Cycles are refused", "[RenderGraph]")
{
	// Not defensive programming: the layering pass walks the graph assuming it terminates, so a cycle
	// there is an unbounded loop with no diagnostic. It is refused where it would be introduced.
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph = MakeChain(Types);
	FRenderGraphIssue Issue;

	SECTION("A self edge is refused")
	{
		REQUIRE_FALSE(Graph.AddEdge(MakeDataEdge("B", "out", "B", "in"), Types, Issue));
	}

	SECTION("A back edge closing a multi hop cycle is refused")
	{
		// The chain's own inputs are already taken, so the loop is built on fresh passes: D reads from B,
		// E reads from D, and E back to D is the edge that would close it. Two hops, so this exercises the
		// reachability walk rather than a direct A to B check.
		REQUIRE(Graph.AddPass("D", "Filter", Types, Issue));
		REQUIRE(Graph.AddPass("E", "Filter", Types, Issue));
		REQUIRE(Graph.AddEdge(MakeDataEdge("D", "out", "E", "in"), Types, Issue));

		const FRenderGraphEdge Closing = MakeDataEdge("E", "out", "D", "in");
		REQUIRE(Graph.WouldCreateCycle(Closing));
		REQUIRE_FALSE(Graph.AddEdge(Closing, Types, Issue));
	}
}

TEST_CASE("Removing a pass leaves nothing referring to it", "[RenderGraph]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph = MakeChain(Types);
	FRenderGraphIssue Issue;
	REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "B", "out" }, Types, Issue));

	REQUIRE(Graph.RemovePass("B"));

	// Every other part of the model assumes a reference names a pass that exists, so the edges and the
	// graph output that mentioned B have to go with it.
	REQUIRE(Graph.FindPass("B") == nullptr);
	REQUIRE(Graph.GetEdges().empty());
	REQUIRE(Graph.GetGraphOutputs().empty());
	REQUIRE(Graph.GetPasses().size() == 2);
}

TEST_CASE("Graph outputs may only name outputs", "[RenderGraph]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph = MakeChain(Types);
	FRenderGraphIssue Issue;

	SECTION("Marking and unmarking an output")
	{
		REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "B", "out" }, Types, Issue));
		REQUIRE(Graph.IsGraphOutput(FRenderGraphResourceRef{ "B", "out" }));

		// The same call toggles, so a second one clears it.
		REQUIRE_FALSE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "B", "out" }, Types, Issue));
		REQUIRE_FALSE(Graph.IsGraphOutput(FRenderGraphResourceRef{ "B", "out" }));
	}

	SECTION("An input cannot be a graph output")
	{
		// Marking an input would claim the graph produces something it only consumes.
		REQUIRE_FALSE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "B", "in" }, Types, Issue));
		REQUIRE_FALSE(Issue.Message.empty());
	}

	SECTION("Several outputs may be marked, and each keeps its slot")
	{
		// One slot per thing worth looking at, such as colour and depth, each destined for its own viewport.
		// The slot has to follow the output rather than its position in the pass list, or a viewport would
		// start showing something else.
		REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "A", "out" }, Types, Issue));
		REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "B", "out" }, Types, Issue));

		REQUIRE(Graph.GetGraphOutputs().size() == 2);
		REQUIRE(Graph.FindGraphOutputSlot(FRenderGraphResourceRef{ "A", "out" }) == 0);
		REQUIRE(Graph.FindGraphOutputSlot(FRenderGraphResourceRef{ "B", "out" }) == 1);

		// Not marked at all, so it has no slot.
		REQUIRE(Graph.FindGraphOutputSlot(FRenderGraphResourceRef{ "B", "in" }) == -1);

		// Unmarking the first closes the gap: the second moves up rather than leaving slot 0 empty.
		REQUIRE_FALSE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "A", "out" }, Types, Issue));
		REQUIRE(Graph.FindGraphOutputSlot(FRenderGraphResourceRef{ "A", "out" }) == -1);
		REQUIRE(Graph.FindGraphOutputSlot(FRenderGraphResourceRef{ "B", "out" }) == 0);
	}
}

TEST_CASE("Validate reports each kind of problem", "[RenderGraph]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();

	SECTION("A complete graph with a marked output is clean")
	{
		FRenderGraphDesc Graph = MakeChain(Types);
		FRenderGraphIssue Issue;
		REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "B", "out" }, Types, Issue));

		const std::vector<FRenderGraphIssue> Issues = Graph.Validate(Types);
		const auto Errors = std::count_if(Issues.begin(), Issues.end(), [](const FRenderGraphIssue& Item) { return Item.IsError(); });
		REQUIRE(Errors == 0);
	}

	SECTION("A graph with no marked output is an error")
	{
		// Every pass would be culled by a real render graph, so the graph produces nothing.
		const FRenderGraphDesc Graph = MakeChain(Types);
		const std::vector<FRenderGraphIssue> Issues = Graph.Validate(Types);
		REQUIRE(std::any_of(Issues.begin(), Issues.end(), [](const FRenderGraphIssue& Item) { return Item.IsError(); }));
	}

	SECTION("An unconnected input is a warning, not an error")
	{
		// A pass whose input is not yet wired is a graph still being assembled, which is normal while editing.
		FRenderGraphDesc Graph;
		FRenderGraphIssue Issue;
		REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
		REQUIRE(Graph.AddPass("B", "Filter", Types, Issue));
		REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "B", "out" }, Types, Issue));

		const std::vector<FRenderGraphIssue> Issues = Graph.Validate(Types);
		REQUIRE(std::any_of(Issues.begin(), Issues.end(), [](const FRenderGraphIssue& Item) { return !Item.IsError(); }));
		const auto Errors = std::count_if(Issues.begin(), Issues.end(), [](const FRenderGraphIssue& Item) { return Item.IsError(); });
		REQUIRE(Errors == 0);
	}
}
