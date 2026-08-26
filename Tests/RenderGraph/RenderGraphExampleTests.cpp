// Verifies the shipped example graph loads and compiles.
//
// Written because the example and the passes are edited independently: a resource renamed in one and not
// the other produces a file that opens with warnings and draws a graph missing its edges, which is easy
// to miss by eye and pointless to ship.
//
// The pass types are declared here for now. They will be replaced by the renderer's reflected types once
// the built-in passes provide them, which is the only way this test can catch the file and the passes
// drifting apart; until then it checks the file against the shape it is expected to have.

#include "RenderGraph/RenderGraphCompiler.h"
#include "RenderGraph/RenderGraphJson.h"
#include "RenderGraph/RenderGraphLayout.h"
#include "Renderer/Passes/BuiltinPasses.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <utility>

using namespace Lime;

namespace
{
	// Walks up from the build output to the repository, so the test does not depend on where it is run from.
	std::filesystem::path FindExample()
	{
		std::filesystem::path Current = std::filesystem::current_path();
		for (int32 Depth = 0; Depth < 8; ++Depth)
		{
			const std::filesystem::path Candidate = Current / "Engine" / "Content" / "RenderGraph" / "DefaultGraph.json";
			if (std::filesystem::exists(Candidate))
			{
				return Candidate;
			}
			if (!Current.has_parent_path() || Current.parent_path() == Current)
			{
				break;
			}
			Current = Current.parent_path();
		}
		return {};
	}

	// The types the shipped example refers to.
	//
	// Reflected from the real passes rather than declared here. The point of this test is that the graph the
	// engine ships can actually be loaded and run by it, and a hand written table would keep passing after
	// a pass renamed one of its fields.
	FRenderGraphPassTypeRegistry MakeExampleRegistry()
	{
		return BuildRenderGraphPassTypes();
	}
} // namespace

TEST_CASE("The example render graph loads and compiles", "[RenderGraph][Json]")
{
	const std::filesystem::path Path = FindExample();
	if (Path.empty())
	{
		SKIP("Engine/Content/RenderGraph/DefaultGraph.json was not found");
	}

	const FRenderGraphPassTypeRegistry Types = MakeExampleRegistry();
	FRenderGraphDesc Graph;
	const FRenderGraphLoadResult Result = FRenderGraphJson::LoadFromFile(Path, Types, Graph);

	REQUIRE(Result.bSucceeded);

	SECTION("Nothing was dropped")
	{
		// Every warning here means an element of the example did not survive loading, so the drawn graph
		// would be missing a pass or an edge.
		for (const FRenderGraphIssue& Issue : Result.Issues)
		{
			INFO("issue: " << Issue.Message);
			REQUIRE(Issue.Message.empty());
		}
	}

	SECTION("The graph is valid")
	{
		const std::vector<FRenderGraphIssue> Issues = Graph.Validate(Types);
		for (const FRenderGraphIssue& Issue : Issues)
		{
			INFO("validation: " << Issue.Message);
			REQUIRE_FALSE(Issue.IsError());
		}
		REQUIRE_FALSE(Graph.GetGraphOutputs().empty());
	}

	SECTION("It compiles into a runnable order")
	{
		// Loading only proves the file parsed. Compiling is what proves the graph could actually run, which
		// is the state a shipped example should be in.
		const FRenderGraphCompileResult Compiled = CompileRenderGraph(Graph, Types);
		for (const FRenderGraphIssue& Issue : Compiled.Issues)
		{
			INFO("compile: " << Issue.Message);
			REQUIRE_FALSE(Issue.IsError());
		}
		REQUIRE(Compiled.bSucceeded);
	}

	SECTION("It lays out as a pipeline rather than a single column")
	{
		// A file whose edges were all dropped would still lay out, but every pass would land in layer 0.
		// Asserting a depth confirms the dependencies actually took effect.
		const std::vector<FRenderGraphNodePlacement> Placements = ComputeRenderGraphLayout(Graph);
		REQUIRE(Placements.size() == Graph.GetPasses().size());

		int32 MaxLayer = 0;
		for (const FRenderGraphNodePlacement& Placement : Placements)
		{
			MaxLayer = MaxLayer > Placement.Layer ? MaxLayer : Placement.Layer;
		}
		// The shipped graph is a chain of three, so the last pass sits in layer 2. Anything less would mean
		// edges were dropped and passes collapsed onto each other.
		REQUIRE(MaxLayer >= 2);
	}
}
