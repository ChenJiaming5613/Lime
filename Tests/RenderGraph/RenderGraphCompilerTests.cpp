// Compiler tests: the rules that decide whether a graph can run, and in what order.
//
// These matter more than they look. A description that validates can still fail to compile, and a graph
// that compiles wrongly renders something subtly incorrect rather than failing loudly. Every case here
// is decidable from data, so none of them needs a graphics device.

#include "RenderGraph/RenderGraphCompiler.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <utility>

using namespace Lime;

namespace
{
	// Source produces, Filter consumes and produces, Sink only consumes. Enough shape to build a chain, a
	// fork and a dead end.
	FRenderGraphPassTypeRegistry MakeTestRegistry()
	{
		FRenderGraphPassTypeRegistry Registry;

		FRenderGraphPassTypeDesc Source;
		Source.Name = "Source";
		Source.Outputs.push_back(MakeTextureResource("out", ERenderGraphResourceVisibility::Output, nvrhi::Format::RGBA8_UNORM));
		Registry.Register(std::move(Source));

		FRenderGraphPassTypeDesc Filter;
		Filter.Name = "Filter";
		Filter.Inputs.push_back(MakeTextureResource("in", ERenderGraphResourceVisibility::Input));
		Filter.Outputs.push_back(MakeTextureResource("out", ERenderGraphResourceVisibility::Output, nvrhi::Format::RGBA8_UNORM));
		Registry.Register(std::move(Filter));

		FRenderGraphPassTypeDesc Sink;
		Sink.Name = "Sink";
		Sink.Inputs.push_back(MakeTextureResource("in", ERenderGraphResourceVisibility::Input));
		Sink.Outputs.push_back(MakeTextureResource("out", ERenderGraphResourceVisibility::Output, nvrhi::Format::RGBA8_UNORM));
		Registry.Register(std::move(Sink));

		// Writes a colour and a depth target, so a graph can try to present either. Stands in for a lit pass,
		// which is the case where marking the wrong one is an easy mistake to make.
		FRenderGraphPassTypeDesc Lit;
		Lit.Name = "Lit";
		Lit.Inputs.push_back(MakeTextureResource("in", ERenderGraphResourceVisibility::Input));
		Lit.Outputs.push_back(MakeTextureResource("color", ERenderGraphResourceVisibility::Output, nvrhi::Format::RGBA8_UNORM));
		Lit.Outputs.push_back(MakeTextureResource("depth", ERenderGraphResourceVisibility::Output, nvrhi::Format::D32));
		Registry.Register(std::move(Lit));

		return Registry;
	}

	FRenderGraphEdge MakeEdge(std::string FromPass, std::string FromResource, std::string ToPass, std::string ToResource)
	{
		FRenderGraphEdge Edge;
		Edge.From = FRenderGraphResourceRef{ std::move(FromPass), std::move(FromResource) };
		Edge.To = FRenderGraphResourceRef{ std::move(ToPass), std::move(ToResource) };
		return Edge;
	}

	// Position of a pass in the compiled order, or -1 when it was culled.
	int32 IndexOf(const FRenderGraphCompileResult& Result, std::string_view PassName)
	{
		for (SizeType Index = 0; Index < Result.ExecutionOrder.size(); ++Index)
		{
			if (Result.ExecutionOrder[Index].PassName == PassName)
			{
				return static_cast<int32>(Index);
			}
		}
		return -1;
	}

	const FCompiledPassBinding* FindBinding(const FCompiledPass& Pass, std::string_view FieldName)
	{
		const auto Found = std::find_if(Pass.Bindings.begin(), Pass.Bindings.end(),
		                                [FieldName](const FCompiledPassBinding& Binding) { return Binding.FieldName == FieldName; });
		return Found != Pass.Bindings.end() ? &*Found : nullptr;
	}

	// A -> B -> C with C.out marked as the graph output.
	FRenderGraphDesc MakeChain(const FRenderGraphPassTypeRegistry& Types)
	{
		FRenderGraphDesc Graph;
		FRenderGraphIssue Issue;
		REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
		REQUIRE(Graph.AddPass("B", "Filter", Types, Issue));
		REQUIRE(Graph.AddPass("C", "Sink", Types, Issue));
		REQUIRE(Graph.AddEdge(MakeEdge("A", "out", "B", "in"), Types, Issue));
		REQUIRE(Graph.AddEdge(MakeEdge("B", "out", "C", "in"), Types, Issue));
		REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "C", "out" }, Types, Issue));
		return Graph;
	}
} // namespace

TEST_CASE("A chain compiles into producer before consumer order", "[RenderGraph][Compiler]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	const FRenderGraphDesc Graph = MakeChain(Types);

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.CountErrors() == 0);
	REQUIRE(Result.ExecutionOrder.size() == 3);

	// The order is what the whole compile exists to produce: a consumer running first would read a texture
	// nothing had written yet.
	REQUIRE(IndexOf(Result, "A") < IndexOf(Result, "B"));
	REQUIRE(IndexOf(Result, "B") < IndexOf(Result, "C"));
}

TEST_CASE("A depth graph output is rejected", "[RenderGraph][Compiler]")
{
	// Presenting copies the output to the viewport, and that copy needs a colour format. Left to run, a
	// depth output makes the copy silently skip and the viewport stay black on a graph reporting success —
	// indistinguishable from a rendering bug, which is why it has to fail here instead.
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();

	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("Lit", "Lit", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeEdge("A", "out", "Lit", "in"), Types, Issue));
	REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "Lit", "depth" }, Types, Issue));

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE(Result.CountErrors() > 0);

	// Nothing is reported as runnable. The order is built before the outputs are checked, so leaving it
	// populated would tell a reader those passes are about to run when the graph will not execute at all.
	REQUIRE(Result.ExecutionOrder.empty());
	REQUIRE(Result.OutputResourceIndices.empty());

	// The message has to name the way out, or the error only says no.
	const auto Found = std::find_if(Result.Issues.begin(), Result.Issues.end(), [](const FRenderGraphIssue& Candidate)
	                                { return Candidate.Message.find("DebugVisualizer") != std::string::npos; });
	REQUIRE(Found != Result.Issues.end());
}

TEST_CASE("A colour output beside a depth one is still rejected", "[RenderGraph][Compiler]")
{
	// Only the first output reaches the viewport today, so a graph listing depth first would present nothing
	// even though a usable colour output exists. Rejecting the whole graph rather than quietly picking the
	// colour keeps the marked order meaningful.
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();

	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("Lit", "Lit", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeEdge("A", "out", "Lit", "in"), Types, Issue));
	REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "Lit", "color" }, Types, Issue));
	REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "Lit", "depth" }, Types, Issue));

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE_FALSE(Result.bSucceeded);
}

TEST_CASE("A colour graph output compiles", "[RenderGraph][Compiler]")
{
	// The counterpart to the rejection above: the depth rule must not catch the case it is meant to allow.
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();

	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("Lit", "Lit", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeEdge("A", "out", "Lit", "in"), Types, Issue));
	REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "Lit", "color" }, Types, Issue));

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.CountErrors() == 0);
	// The depth target is still allocated, because the pass writes it whether or not it is presented.
	REQUIRE(Result.Resources.size() == 3);
}

TEST_CASE("An edge makes both ends share one resource", "[RenderGraph][Compiler]")
{
	// An edge is not a copy: the producer and consumer name the same texture. Two resources here would mean
	// the chain allocated twice the memory and read the wrong one.

	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	const FRenderGraphDesc Graph = MakeChain(Types);

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);
	REQUIRE(Result.bSucceeded);

	// Three outputs across three passes, and the edges do not add any.
	REQUIRE(Result.Resources.size() == 3);

	const FCompiledPass& ProducerPass = Result.ExecutionOrder[static_cast<SizeType>(IndexOf(Result, "A"))];
	const FCompiledPass& ConsumerPass = Result.ExecutionOrder[static_cast<SizeType>(IndexOf(Result, "B"))];

	const FCompiledPassBinding* Produced = FindBinding(ProducerPass, "out");
	const FCompiledPassBinding* Consumed = FindBinding(ConsumerPass, "in");
	REQUIRE(Produced != nullptr);
	REQUIRE(Consumed != nullptr);
	REQUIRE(Produced->ResourceIndex == Consumed->ResourceIndex);

	// Written by one pass and read by another, so it needs to be usable as both.
	const FCompiledResource& Shared = Result.Resources[Produced->ResourceIndex];
	REQUIRE(Shared.bUsedAsRenderTarget);
	REQUIRE(Shared.bUsedAsShaderResource);
	REQUIRE(Shared.Name == "A.out");
}

TEST_CASE("A pass that reaches no graph output is culled", "[RenderGraph][Compiler]")
{
	// This is what decides whether a pass runs at all. Work whose result nothing asks for has no observable
	// effect, so executing it would only cost time.
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph = MakeChain(Types);

	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("Unused", "Filter", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeEdge("A", "out", "Unused", "in"), Types, Issue));

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE(Result.bSucceeded);
	REQUIRE(IndexOf(Result, "Unused") == -1);
	REQUIRE(IndexOf(Result, "A") >= 0);

	// Reported rather than dropped in silence: a pass that quietly never runs looks like a pass that ran
	// and did nothing.
	const bool bMentioned = std::any_of(Result.Issues.begin(), Result.Issues.end(),
	                                    [](const FRenderGraphIssue& Candidate)
	                                    { return Candidate.Message.find("Unused") != std::string::npos; });
	REQUIRE(bMentioned);
}

TEST_CASE("A required input with nothing connected fails the compile", "[RenderGraph][Compiler]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Filter", Types, Issue));
	REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "A", "out" }, Types, Issue));

	// Filter.in is not optional and nothing writes it.
	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE(Result.CountErrors() > 0);
}

TEST_CASE("An optional input may be left unconnected", "[RenderGraph][Compiler]")
{
	// The reason optional exists: a lit pass with no shadow map should draw without shadows rather than
	// refuse to run.
	FRenderGraphPassTypeRegistry Types;
	FRenderGraphPassTypeDesc Lit;
	Lit.Name = "Lit";
	FRenderGraphResourceDesc Shadow = MakeTextureResource("shadow", ERenderGraphResourceVisibility::Input, nvrhi::Format::D32);
	Shadow.bOptional = true;
	Lit.Inputs.push_back(std::move(Shadow));
	Lit.Outputs.push_back(MakeTextureResource("color", ERenderGraphResourceVisibility::Output, nvrhi::Format::RGBA8_UNORM));
	Types.Register(std::move(Lit));

	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("Lit", "Lit", Types, Issue));
	REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "Lit", "color" }, Types, Issue));

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.ExecutionOrder.size() == 1);

	// No resource is bound to it, which is how the pass detects the reduced case at execution time.
	REQUIRE(FindBinding(Result.ExecutionOrder[0], "shadow") == nullptr);
}

TEST_CASE("An unspecified format is taken from the other end of the edge", "[RenderGraph][Compiler]")
{
	// A pass that does not care about a format writes UNKNOWN and gets whatever it is connected to. That is
	// what lets one pass be reused between an 8 bit and a float pipeline without editing it.
	FRenderGraphPassTypeRegistry Types;

	FRenderGraphPassTypeDesc Producer;
	Producer.Name = "Producer";
	Producer.Outputs.push_back(MakeTextureResource("out", ERenderGraphResourceVisibility::Output, nvrhi::Format::RGBA16_FLOAT));
	Types.Register(std::move(Producer));

	FRenderGraphPassTypeDesc Consumer;
	Consumer.Name = "Consumer";
	// Left UNKNOWN on purpose.
	Consumer.Inputs.push_back(MakeTextureResource("in", ERenderGraphResourceVisibility::Input));
	Consumer.Outputs.push_back(MakeTextureResource("out", ERenderGraphResourceVisibility::Output, nvrhi::Format::RGBA8_UNORM));
	Types.Register(std::move(Consumer));

	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("P", "Producer", Types, Issue));
	REQUIRE(Graph.AddPass("C", "Consumer", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeEdge("P", "out", "C", "in"), Types, Issue));
	REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "C", "out" }, Types, Issue));

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE(Result.bSucceeded);
	const FCompiledResource* Shared = Result.FindResource("P.out");
	REQUIRE(Shared != nullptr);
	REQUIRE(Shared->Format == nvrhi::Format::RGBA16_FLOAT);
}

TEST_CASE("Two ends demanding different formats is a conflict", "[RenderGraph][Compiler]")
{
	// Silently picking one would render through a format the other end was not compiled against.
	FRenderGraphPassTypeRegistry Types;

	FRenderGraphPassTypeDesc Producer;
	Producer.Name = "Producer";
	Producer.Outputs.push_back(MakeTextureResource("out", ERenderGraphResourceVisibility::Output, nvrhi::Format::RGBA16_FLOAT));
	Types.Register(std::move(Producer));

	FRenderGraphPassTypeDesc Consumer;
	Consumer.Name = "Consumer";
	Consumer.Inputs.push_back(MakeTextureResource("in", ERenderGraphResourceVisibility::Input, nvrhi::Format::RGBA32_FLOAT));
	Consumer.Outputs.push_back(MakeTextureResource("out", ERenderGraphResourceVisibility::Output, nvrhi::Format::RGBA8_UNORM));
	Types.Register(std::move(Consumer));

	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("P", "Producer", Types, Issue));
	REQUIRE(Graph.AddPass("C", "Consumer", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeEdge("P", "out", "C", "in"), Types, Issue));
	REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "C", "out" }, Types, Issue));

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE(Result.CountErrors() > 0);
}

TEST_CASE("A fixed size survives a connection to an unspecified one", "[RenderGraph][Compiler]")
{
	// A shadow map is 2048 square whatever the window is, so a consumer writing 0 must not drag it back to
	// the viewport size.
	FRenderGraphPassTypeRegistry Types;

	FRenderGraphPassTypeDesc Caster;
	Caster.Name = "Caster";
	FRenderGraphResourceDesc Depth = MakeTextureResource("depth", ERenderGraphResourceVisibility::Output, nvrhi::Format::D32);
	Depth.Width = 2048;
	Depth.Height = 2048;
	Caster.Outputs.push_back(std::move(Depth));
	Types.Register(std::move(Caster));

	FRenderGraphPassTypeDesc Lit;
	Lit.Name = "Lit";
	Lit.Inputs.push_back(MakeTextureResource("shadow", ERenderGraphResourceVisibility::Input));
	Lit.Outputs.push_back(MakeTextureResource("color", ERenderGraphResourceVisibility::Output, nvrhi::Format::RGBA8_UNORM));
	Types.Register(std::move(Lit));

	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("Caster", "Caster", Types, Issue));
	REQUIRE(Graph.AddPass("Lit", "Lit", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeEdge("Caster", "depth", "Lit", "shadow"), Types, Issue));
	REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "Lit", "color" }, Types, Issue));

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE(Result.bSucceeded);
	const FCompiledResource* Shadow = Result.FindResource("Caster.depth");
	REQUIRE(Shadow != nullptr);
	REQUIRE(Shadow->Width == 2048);
	REQUIRE(Shadow->Height == 2048);
	REQUIRE(Shadow->bIsDepth);

	// The lit pass's own colour target was left unspecified, so it follows the graph.
	const FCompiledResource* Colour = Result.FindResource("Lit.color");
	REQUIRE(Colour != nullptr);
	REQUIRE(Colour->Width == 0);
	REQUIRE(Colour->Height == 0);
	REQUIRE_FALSE(Colour->bIsDepth);
}

TEST_CASE("A graph with no marked output does not compile", "[RenderGraph][Compiler]")
{
	// Nothing is asked for, so the cull would drop every pass and leave a result that looks compiled but
	// draws nothing.
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE(Result.CountErrors() > 0);
}

TEST_CASE("An empty graph does not compile", "[RenderGraph][Compiler]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	const FRenderGraphDesc Graph;

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE(Result.CountErrors() > 0);
}

TEST_CASE("An unknown pass type does not compile", "[RenderGraph][Compiler]")
{
	// The graph could have been written against a pass the build no longer provides, which is exactly the
	// case the startup fallback exists for.
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
	REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "A", "out" }, Types, Issue));

	// Added behind the checks, standing in for a file naming a type this build does not have.
	Graph.AddPassUnchecked(FRenderGraphPassInstance{ "Ghost", "NoSuchType" });

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE(Result.CountErrors() > 0);
}

TEST_CASE("A cycle does not compile", "[RenderGraph][Compiler]")
{
	// AddEdge refuses to close a cycle, so one can only arrive through a path that bypasses those checks.
	// The compiler still has to detect it: an execution order containing a cycle has no meaning, and the
	// walk would otherwise silently drop the passes involved.
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Filter", Types, Issue));
	REQUIRE(Graph.AddPass("B", "Filter", Types, Issue));
	REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "B", "out" }, Types, Issue));

	Graph.AddEdgeUnchecked(MakeEdge("A", "out", "B", "in"));
	Graph.AddEdgeUnchecked(MakeEdge("B", "out", "A", "in"));

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE(Result.CountErrors() > 0);
}

TEST_CASE("Two producers writing one input do not compile", "[RenderGraph][Compiler]")
{
	// The reader would see whichever pass ran last, which the graph does not state anywhere.
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("B", "Source", Types, Issue));
	REQUIRE(Graph.AddPass("C", "Sink", Types, Issue));
	REQUIRE(Graph.AddEdge(MakeEdge("A", "out", "C", "in"), Types, Issue));
	REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "C", "out" }, Types, Issue));

	// AddEdge already refuses a second producer, so this arrives the way a hand edited file would.
	Graph.AddEdgeUnchecked(MakeEdge("B", "out", "C", "in"));

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE(Result.CountErrors() > 0);
}

TEST_CASE("Graph outputs are reported as resource indices in the order they were marked", "[RenderGraph][Compiler]")
{
	// A viewport binds to one of these by slot, so the order has to follow the marking rather than the
	// execution order.
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph = MakeChain(Types);

	FRenderGraphIssue Issue;
	REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "B", "out" }, Types, Issue));

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);

	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.OutputResourceIndices.size() == 2);
	REQUIRE(Result.Resources[Result.OutputResourceIndices[0]].Name == "C.out");
	REQUIRE(Result.Resources[Result.OutputResourceIndices[1]].Name == "B.out");
}
