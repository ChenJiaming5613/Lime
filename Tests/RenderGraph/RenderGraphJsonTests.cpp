// Render graph JSON tests.
//
// Two properties matter most. First, a round trip has to preserve the graph: the file is the only record,
// so anything lost on save is lost for good. Second, one bad element must not lose the rest, because these
// files are hand written and script generated, and a graph that refuses to open over a single typo would
// make that unusable.
//
// Also asserts what must *not* be written. Layout is recomputed on load, so a file carrying positions
// would create a second source of truth that silently disagrees with the computed one.

#include "RenderGraph/RenderGraphJson.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

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

		return Registry;
	}

	const char* const ValidGraph = R"({
		"name": "Round Trip",
		"passes": [
			{ "name": "A", "type": "Source" },
			{ "name": "B", "type": "Filter" }
		],
		"edges": [
			{ "from": "A.out", "to": "B.in" }
		],
		"graphOutputs": [ "B.out" ]
	})";

	SizeType CountErrors(const FRenderGraphLoadResult& Result)
	{
		return Result.CountErrors();
	}
} // namespace

TEST_CASE("A render graph survives a round trip", "[RenderGraph][Json]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();

	FRenderGraphDesc Loaded;
	const FRenderGraphLoadResult First = FRenderGraphJson::LoadFromString(ValidGraph, Types, Loaded);
	REQUIRE(First.bSucceeded);
	REQUIRE(CountErrors(First) == 0);
	REQUIRE(Loaded.GetName() == "Round Trip");
	REQUIRE(Loaded.GetPasses().size() == 2);
	REQUIRE(Loaded.GetEdges().size() == 1);
	REQUIRE(Loaded.GetGraphOutputs().size() == 1);

	SECTION("Saving and loading again yields an equivalent graph")
	{
		// Compared element by element rather than by text: key order and whitespace are not part of the
		// meaning, but the passes, edges and outputs are.
		const std::string Text = FRenderGraphJson::SaveToString(Loaded);

		FRenderGraphDesc Again;
		const FRenderGraphLoadResult Second = FRenderGraphJson::LoadFromString(Text, Types, Again);
		REQUIRE(Second.bSucceeded);
		REQUIRE(CountErrors(Second) == 0);

		REQUIRE(Again.GetName() == Loaded.GetName());
		REQUIRE(Again.GetPasses().size() == Loaded.GetPasses().size());
		for (SizeType Index = 0; Index < Loaded.GetPasses().size(); ++Index)
		{
			REQUIRE(Again.GetPasses()[Index].Name == Loaded.GetPasses()[Index].Name);
			REQUIRE(Again.GetPasses()[Index].TypeName == Loaded.GetPasses()[Index].TypeName);
		}
		REQUIRE(Again.GetEdges() == Loaded.GetEdges());
		REQUIRE(Again.GetGraphOutputs() == Loaded.GetGraphOutputs());
	}

	SECTION("The saved document carries no layout")
	{
		// Positions are a function of the graph, recomputed on load. A file that stored them would disagree
		// with the computed layout as soon as either changed, with no way to tell which was right.
		const std::string Text = FRenderGraphJson::SaveToString(Loaded);
		REQUIRE(Text.find("position") == std::string::npos);
		REQUIRE(Text.find("location") == std::string::npos);
		REQUIRE(Text.find("zoom") == std::string::npos);
		REQUIRE(Text.find("layout") == std::string::npos);
	}
}

TEST_CASE("An edge whose endpoint names only a pass is skipped", "[RenderGraph][Json]")
{
	// Both ends have to name a resource. This shape used to mean an execution edge; now it is malformed,
	// and the loader should say so rather than invent a resource or drop it silently.
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	const char* const Document = R"({
		"passes": [
			{ "name": "A", "type": "Source" },
			{ "name": "B", "type": "Filter" }
		],
		"edges": [
			{ "from": "A", "to": "B" },
			{ "from": "A.out", "to": "B.in" }
		],
		"graphOutputs": [ "B.out" ]
	})";

	FRenderGraphDesc Graph;
	const FRenderGraphLoadResult Result = FRenderGraphJson::LoadFromString(Document, Types, Graph);

	// A warning, not an error: the rest of the file is still a usable graph.
	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.CountErrors() == 0);
	REQUIRE_FALSE(Result.Issues.empty());

	// Only the well formed edge survives.
	REQUIRE(Graph.GetEdges().size() == 1);
	REQUIRE(Graph.GetEdges()[0].From.ResourceName == "out");
	REQUIRE(Graph.GetEdges()[0].To.ResourceName == "in");
}

TEST_CASE("A malformed element is dropped without losing the graph", "[RenderGraph][Json]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();

	SECTION("An edge naming a missing pass is dropped")
	{
		const char* const Document = R"({
			"passes": [
				{ "name": "A", "type": "Source" },
				{ "name": "B", "type": "Filter" }
			],
			"edges": [
				{ "from": "A.out", "to": "Ghost.in" },
				{ "from": "A.out", "to": "B.in" }
			],
			"graphOutputs": [ "B.out" ]
		})";

		FRenderGraphDesc Graph;
		const FRenderGraphLoadResult Result = FRenderGraphJson::LoadFromString(Document, Types, Graph);
		// Succeeded, because the rest of the file is usable; reported, because something was lost.
		REQUIRE(Result.bSucceeded);
		REQUIRE(Result.HasIssues());
		REQUIRE(Graph.GetPasses().size() == 2);
		REQUIRE(Graph.GetEdges().size() == 1);
	}

	SECTION("A pass with an unknown type is dropped")
	{
		const char* const Document = R"({
			"passes": [
				{ "name": "A", "type": "Source" },
				{ "name": "Weird", "type": "NoSuchType" }
			],
			"graphOutputs": [ "A.out" ]
		})";

		FRenderGraphDesc Graph;
		const FRenderGraphLoadResult Result = FRenderGraphJson::LoadFromString(Document, Types, Graph);
		REQUIRE(Result.bSucceeded);
		REQUIRE(Result.HasIssues());
		REQUIRE(Graph.GetPasses().size() == 1);
	}

	SECTION("Entries of the wrong shape are dropped")
	{
		const char* const Document = R"({
			"passes": [
				{ "name": "A", "type": "Source" },
				"not an object",
				{ "type": "Filter" }
			],
			"graphOutputs": [ "A.out" ]
		})";

		FRenderGraphDesc Graph;
		const FRenderGraphLoadResult Result = FRenderGraphJson::LoadFromString(Document, Types, Graph);
		REQUIRE(Result.bSucceeded);
		REQUIRE(Graph.GetPasses().size() == 1);
	}
}

TEST_CASE("An unusable document fails outright", "[RenderGraph][Json]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();

	SECTION("Text that is not JSON")
	{
		FRenderGraphDesc Graph;
		const FRenderGraphLoadResult Result = FRenderGraphJson::LoadFromString("this is not json", Types, Graph);
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE(CountErrors(Result) > 0);
	}

	SECTION("A document without a passes array")
	{
		// Nothing to salvage: without passes there is no graph, and edges would all dangle.
		FRenderGraphDesc Graph;
		const FRenderGraphLoadResult Result = FRenderGraphJson::LoadFromString(R"({"name":"Empty"})", Types, Graph);
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE(CountErrors(Result) > 0);
	}

	SECTION("A failed load leaves the previous graph alone")
	{
		// The panel keeps showing what it had, rather than replacing a working graph with an empty one
		// because a path was mistyped.
		FRenderGraphDesc Graph;
		REQUIRE(FRenderGraphJson::LoadFromString(ValidGraph, Types, Graph).bSucceeded);
		REQUIRE(Graph.GetPasses().size() == 2);

		REQUIRE_FALSE(FRenderGraphJson::LoadFromString("{{{", Types, Graph).bSucceeded);
		REQUIRE(Graph.GetPasses().size() == 2);
	}
}

TEST_CASE("A render graph round trips through a file", "[RenderGraph][Json]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	const std::filesystem::path Path = std::filesystem::temp_directory_path() / "LimeRenderGraphTest.json";

	FRenderGraphDesc Graph;
	REQUIRE(FRenderGraphJson::LoadFromString(ValidGraph, Types, Graph).bSucceeded);
	REQUIRE(FRenderGraphJson::SaveToFile(Path, Graph));

	FRenderGraphDesc Reloaded;
	const FRenderGraphLoadResult Result = FRenderGraphJson::LoadFromFile(Path, Types, Reloaded);
	REQUIRE(Result.bSucceeded);
	REQUIRE(Reloaded.GetPasses().size() == Graph.GetPasses().size());
	REQUIRE(Reloaded.GetEdges() == Graph.GetEdges());

	std::error_code Error;
	std::filesystem::remove(Path, Error);
}

TEST_CASE("A missing file is reported rather than throwing", "[RenderGraph][Json]")
{
	const FRenderGraphPassTypeRegistry Types = MakeTestRegistry();
	FRenderGraphDesc Graph;
	const FRenderGraphLoadResult Result =
	    FRenderGraphJson::LoadFromFile(std::filesystem::temp_directory_path() / "LimeNoSuchGraph.json", Types, Graph);

	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE(CountErrors(Result) > 0);
}
