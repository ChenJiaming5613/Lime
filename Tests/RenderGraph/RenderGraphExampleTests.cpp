// Verifies the shipped example graph loads against the built-in pass types.
//
// Written because the example and the built-in type list are edited independently: a resource renamed in
// one and not the other produces a file that opens with warnings and draws a graph missing its edges,
// which is easy to miss by eye and pointless to ship.

#include "RenderGraph/RenderGraphJson.h"
#include "RenderGraph/RenderGraphLayout.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace Lime;

namespace
{
	// Walks up from the build output to the repository, so the test does not depend on where it is run from.
	std::filesystem::path FindExample()
	{
		std::filesystem::path Current = std::filesystem::current_path();
		for (int32 Depth = 0; Depth < 8; ++Depth)
		{
			const std::filesystem::path Candidate = Current / "Engine" / "Content" / "RenderGraph" / "DeferredExample.json";
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
} // namespace

TEST_CASE("The example render graph loads cleanly against the built-in pass types", "[RenderGraph][Json]")
{
	const std::filesystem::path Path = FindExample();
	if (Path.empty())
	{
		SKIP("Engine/Content/RenderGraph/DeferredExample.json was not found");
	}

	// The default registry, not a test one: the point is that the shipped file matches the shipped types.
	const FRenderGraphPassTypeRegistry Types;
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
		REQUIRE(MaxLayer >= 3);
	}
}
