// Injection tests: the passes the engine appends to a compiled graph.
//
// These cover the properties the design depends on rather than the plumbing. An injected pass must not
// appear in the description the panel draws or the file saving writes, must be reachable by the renderer
// as an existing instance, and must never turn a runnable graph into a broken one. All of that is
// decidable from data, so none of it needs a graphics device.
//
// The pass types are declared by hand rather than reflected from the real passes. Those live in
// LimeRenderer, which this binary links, but building them here would tie the cases to whatever the
// concrete passes currently declare; a hand written shape states what the injector actually requires.

#include "RenderGraph/RenderGraphInjection.h"
#include "RenderGraph/RenderGraphJson.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <utility>

using namespace Lime;

namespace
{
	// A producer plus the two shapes the engine actually injects: one writing an imported target and
	// reading a graph output, and one that names its own import slot.
	FRenderGraphPassTypeRegistry MakeRegistry()
	{
		FRenderGraphPassTypeRegistry Registry;

		FRenderGraphPassTypeDesc Source;
		Source.Name = "Source";
		Source.Outputs.push_back(MakeTextureResource("out", ERenderGraphResourceVisibility::Output, nvrhi::Format::RGBA8_UNORM));
		Registry.Register(std::move(Source));

		// Mirrors FBlitPass: optional input, imported output, nothing pinned.
		FRenderGraphPassTypeDesc Blit;
		Blit.Name = "Blit";
		FRenderGraphResourceDesc BlitSource = MakeTextureResource("source", ERenderGraphResourceVisibility::Input);
		BlitSource.bOptional = true;
		Blit.Inputs.push_back(std::move(BlitSource));
		FRenderGraphResourceDesc BlitTarget = MakeTextureResource("target", ERenderGraphResourceVisibility::Output);
		BlitTarget.Source = ERenderGraphResourceSource::Imported;
		BlitTarget.LoadAction = ERenderGraphLoadAction::DontCare;
		Blit.Outputs.push_back(std::move(BlitTarget));
		Registry.Register(std::move(Blit));

		// Mirrors FEditorUIPass: clears its imported target rather than loading it.
		FRenderGraphPassTypeDesc EditorUI;
		EditorUI.Name = "EditorUI";
		FRenderGraphResourceDesc Scene = MakeTextureResource("scene", ERenderGraphResourceVisibility::Input);
		Scene.bOptional = true;
		EditorUI.Inputs.push_back(std::move(Scene));
		FRenderGraphResourceDesc UITarget = MakeTextureResource("target", ERenderGraphResourceVisibility::Output);
		UITarget.Source = ERenderGraphResourceSource::Imported;
		UITarget.LoadAction = ERenderGraphLoadAction::Clear;
		EditorUI.Outputs.push_back(std::move(UITarget));
		Registry.Register(std::move(EditorUI));

		return Registry;
	}

	// A single Source with its output marked, which is the smallest runnable graph.
	FRenderGraphDesc MakeGraph(const FRenderGraphPassTypeRegistry& Types)
	{
		FRenderGraphDesc Graph;
		FRenderGraphIssue Issue;
		REQUIRE(Graph.AddPass("A", "Source", Types, Issue));
		REQUIRE(Graph.ToggleGraphOutput(FRenderGraphResourceRef{ "A", "out" }, Types, Issue));
		return Graph;
	}

	FRenderGraphInjection MakeInjection(std::string PassName, std::string TypeName, std::string InputField)
	{
		FRenderGraphInjection Injection;
		Injection.PassName = std::move(PassName);
		Injection.TypeName = std::move(TypeName);
		Injection.InputFieldName = std::move(InputField);
		Injection.InputFromOutputSlot = 0;
		Injection.ImportedTarget = "$BackBuffer";
		return Injection;
	}

	const FCompiledPass* FindPass(const FRenderGraphCompileResult& Result, std::string_view PassName)
	{
		const auto Found = std::find_if(Result.ExecutionOrder.begin(), Result.ExecutionOrder.end(),
		         [PassName](const FCompiledPass& Pass) { return Pass.PassName == PassName; });
		return Found != Result.ExecutionOrder.end() ? &*Found : nullptr;
	}

	const FCompiledPassBinding* FindBinding(const FCompiledPass& Pass, std::string_view FieldName)
	{
		const auto Found = std::find_if(Pass.Bindings.begin(), Pass.Bindings.end(),
		         [FieldName](const FCompiledPassBinding& Binding) { return Binding.FieldName == FieldName; });
		return Found != Pass.Bindings.end() ? &*Found : nullptr;
	}
} // namespace

TEST_CASE("An injected pass is appended and marked", "[RenderGraph][Injection]")
{
	const FRenderGraphPassTypeRegistry Types = MakeRegistry();
	const FRenderGraphDesc Graph = MakeGraph(Types);

	FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);
	REQUIRE(Result.bSucceeded);
	const SizeType DescribedCount = Result.ExecutionOrder.size();

	InjectRenderGraphPasses(Result, { MakeInjection("$Present", "Blit", "source") }, Types);

	REQUIRE(Result.ExecutionOrder.size() == DescribedCount + 1);

	// Last, which is the only placement that makes sense for a pass consuming a graph output.
	const FCompiledPass& Injected = Result.ExecutionOrder.back();
	REQUIRE(Injected.PassName == "$Present");
	REQUIRE(Injected.bInjected);

	// The described pass is untouched, so nothing about it depends on whether an injection happened.
	const FCompiledPass* Described = FindPass(Result, "A");
	REQUIRE(Described != nullptr);
	REQUIRE_FALSE(Described->bInjected);
}

TEST_CASE("An injected pass binds the graph output to its input", "[RenderGraph][Injection]")
{
	const FRenderGraphPassTypeRegistry Types = MakeRegistry();
	const FRenderGraphDesc Graph = MakeGraph(Types);

	FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);
	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.OutputResourceIndices.size() == 1);
	const SizeType OutputIndex = Result.OutputResourceIndices[0];

	InjectRenderGraphPasses(Result, { MakeInjection("$Present", "Blit", "source") }, Types);

	const FCompiledPass* Injected = FindPass(Result, "$Present");
	REQUIRE(Injected != nullptr);

	// Bound to the same resource the graph was asked to produce, not a copy of it: that shared identity is
	// what orders the injection after the producer.
	const FCompiledPassBinding* SourceBinding = FindBinding(*Injected, "source");
	REQUIRE(SourceBinding != nullptr);
	REQUIRE(SourceBinding->ResourceIndex == OutputIndex);
	REQUIRE(SourceBinding->Visibility == ERenderGraphResourceVisibility::Input);

	// Now read by a pass, so it needs the bind flag a copy alone would not have required.
	REQUIRE(Result.Resources[OutputIndex].bUsedAsShaderResource);
}

TEST_CASE("An injected imported target becomes one shared resource", "[RenderGraph][Injection]")
{
	const FRenderGraphPassTypeRegistry Types = MakeRegistry();
	const FRenderGraphDesc Graph = MakeGraph(Types);

	FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);
	REQUIRE(Result.bSucceeded);

	// Two passes writing the same slot. They have to end up on one resource, or the second would not be
	// able to see that the first already wrote the target this frame.
	InjectRenderGraphPasses(Result,
	        { MakeInjection("$Present", "Blit", "source"), MakeInjection("$EditorUI", "EditorUI", "scene") }, Types);

	const FCompiledPass* Present = FindPass(Result, "$Present");
	const FCompiledPass* EditorUI = FindPass(Result, "$EditorUI");
	REQUIRE(Present != nullptr);
	REQUIRE(EditorUI != nullptr);

	const FCompiledPassBinding* PresentTarget = FindBinding(*Present, "target");
	const FCompiledPassBinding* EditorTarget = FindBinding(*EditorUI, "target");
	REQUIRE(PresentTarget != nullptr);
	REQUIRE(EditorTarget != nullptr);
	REQUIRE(PresentTarget->ResourceIndex == EditorTarget->ResourceIndex);

	const FCompiledResource& Target = Result.Resources[PresentTarget->ResourceIndex];
	REQUIRE(Target.IsImported());
	REQUIRE(Target.ImportName == "$BackBuffer");
	// Never allocated by the graph, so it must not be mistaken for a transient texture.
	REQUIRE_FALSE(Target.Source == ERenderGraphResourceSource::Transient);
}

TEST_CASE("An injected pass keeps the load action it declared", "[RenderGraph][Injection]")
{
	const FRenderGraphPassTypeRegistry Types = MakeRegistry();
	const FRenderGraphDesc Graph = MakeGraph(Types);

	FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);
	REQUIRE(Result.bSucceeded);

	InjectRenderGraphPasses(Result,
	        { MakeInjection("$Present", "Blit", "source"), MakeInjection("$EditorUI", "EditorUI", "scene") }, Types);

	// The distinction the executor acts on: a blit overwrites every pixel so there is nothing to preserve,
	// while the editor owns the window and clears it. Neither may resolve to the transient default, which
	// would erase whatever had already been drawn into the back buffer.
	const FCompiledPass* Present = FindPass(Result, "$Present");
	REQUIRE(Present != nullptr);
	REQUIRE(FindBinding(*Present, "target")->LoadAction == ERenderGraphLoadAction::DontCare);

	const FCompiledPass* EditorUI = FindPass(Result, "$EditorUI");
	REQUIRE(EditorUI != nullptr);
	REQUIRE(FindBinding(*EditorUI, "target")->LoadAction == ERenderGraphLoadAction::Clear);
}

TEST_CASE("A described pass clears its transient targets by default", "[RenderGraph][Injection]")
{
	const FRenderGraphPassTypeRegistry Types = MakeRegistry();
	const FRenderGraphDesc Graph = MakeGraph(Types);

	const FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);
	REQUIRE(Result.bSucceeded);

	// The behaviour the executor had before load actions existed, now stated explicitly: a pass that said
	// nothing still gets its attachments cleared, so no existing graph changes behaviour.
	const FCompiledPass* Described = FindPass(Result, "A");
	REQUIRE(Described != nullptr);
	const FCompiledPassBinding* Output = FindBinding(*Described, "out");
	REQUIRE(Output != nullptr);
	REQUIRE(Output->LoadAction == ERenderGraphLoadAction::Clear);
}

TEST_CASE("Injection is skipped for a graph that failed to compile", "[RenderGraph][Injection]")
{
	const FRenderGraphPassTypeRegistry Types = MakeRegistry();

	// No output marked, so the compile fails.
	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;
	REQUIRE(Graph.AddPass("A", "Source", Types, Issue));

	FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);
	REQUIRE_FALSE(Result.bSucceeded);

	InjectRenderGraphPasses(Result, { MakeInjection("$Present", "Blit", "source") }, Types);

	// A failed compile has no order and no outputs to bind to. Appending to it would produce a result that
	// looks partially runnable, which is the one state the compiler never returns.
	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE(Result.ExecutionOrder.empty());
}

TEST_CASE("An unknown injected type is a warning rather than a failure", "[RenderGraph][Injection]")
{
	const FRenderGraphPassTypeRegistry Types = MakeRegistry();
	const FRenderGraphDesc Graph = MakeGraph(Types);

	FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);
	REQUIRE(Result.bSucceeded);
	const SizeType Before = Result.ExecutionOrder.size();

	InjectRenderGraphPasses(Result, { MakeInjection("$Missing", "NotARealPass", "source") }, Types);

	// The project's own rendering is unaffected by an engine pass that could not be resolved, which is what
	// keeps a build without the editor from failing every graph.
	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.ExecutionOrder.size() == Before);
	REQUIRE(Result.CountErrors() == 0);
	REQUIRE_FALSE(Result.Issues.empty());
}

TEST_CASE("An injected pass survives a graph with no output to read", "[RenderGraph][Injection]")
{
	const FRenderGraphPassTypeRegistry Types = MakeRegistry();
	const FRenderGraphDesc Graph = MakeGraph(Types);

	FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);
	REQUIRE(Result.bSucceeded);

	// Slot 1 does not exist: the graph marked only one output. The editor still has to be drawn, or the
	// reason nothing rendered could not be reported on screen.
	FRenderGraphInjection Injection = MakeInjection("$EditorUI", "EditorUI", "scene");
	Injection.InputFromOutputSlot = 1;
	InjectRenderGraphPasses(Result, { Injection }, Types);

	const FCompiledPass* Injected = FindPass(Result, "$EditorUI");
	REQUIRE(Injected != nullptr);
	// Runs unbound: the input is optional, and the target it writes is what matters.
	REQUIRE(FindBinding(*Injected, "scene") == nullptr);
	REQUIRE(FindBinding(*Injected, "target") != nullptr);
}

TEST_CASE("The graph description refuses a reserved pass name", "[RenderGraph][Injection]")
{
	const FRenderGraphPassTypeRegistry Types = MakeRegistry();
	FRenderGraphDesc Graph;
	FRenderGraphIssue Issue;

	// Hand editing a graph file is expected, so the prefix the engine injects under has to be refused at
	// the point a name enters the description rather than merely being unlikely to collide.
	REQUIRE_FALSE(Graph.AddPass("$EditorUI", "EditorUI", Types, Issue));
	REQUIRE(Issue.IsError());
	REQUIRE(Graph.GetPasses().empty());
}

TEST_CASE("An injected pass is never written to the graph file", "[RenderGraph][Injection]")
{
	const FRenderGraphPassTypeRegistry Types = MakeRegistry();
	const FRenderGraphDesc Graph = MakeGraph(Types);

	FRenderGraphCompileResult Result = CompileRenderGraph(Graph, Types);
	REQUIRE(Result.bSucceeded);
	InjectRenderGraphPasses(Result, { MakeInjection("$EditorUI", "EditorUI", "scene") }, Types);

	// Saving works from the description, which injection never touches. This is what makes "not in the
	// json" and "not on the panel" free rather than two filters that could drift apart.
	REQUIRE(Graph.GetPasses().size() == 1);
	REQUIRE(Graph.FindPass("$EditorUI") == nullptr);
}
