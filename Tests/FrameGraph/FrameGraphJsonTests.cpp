// Frame graph JSON tests.
//
// Two properties matter most. First, a round trip has to preserve the graph: the file is the only record,
// so anything lost on save is lost for good. Second, one bad element must not lose the rest, because these
// files are hand written and script generated, and a graph that refuses to open over a single typo would
// make that unusable.
//
// Also asserts what must *not* be written. Layout is recomputed on load, so a file carrying positions
// would create a second source of truth that silently disagrees with the computed one.

#include "FrameGraph/FrameGraphJson.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

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

	SizeType CountErrors(const FFrameGraphLoadResult& Result)
	{
		return Result.CountErrors();
	}
} // namespace

TEST_CASE("A frame graph survives a round trip", "[FrameGraph][Json]")
{
	const FFramePassTypeRegistry Types = MakeTestRegistry();

	FFrameGraphDesc Loaded;
	const FFrameGraphLoadResult First = FFrameGraphJson::LoadFromString(ValidGraph, Types, Loaded);
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
		const std::string Text = FFrameGraphJson::SaveToString(Loaded);

		FFrameGraphDesc Again;
		const FFrameGraphLoadResult Second = FFrameGraphJson::LoadFromString(Text, Types, Again);
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
		const std::string Text = FFrameGraphJson::SaveToString(Loaded);
		REQUIRE(Text.find("position") == std::string::npos);
		REQUIRE(Text.find("location") == std::string::npos);
		REQUIRE(Text.find("zoom") == std::string::npos);
		REQUIRE(Text.find("layout") == std::string::npos);
	}
}

TEST_CASE("Execution edges keep their kind through a round trip", "[FrameGraph][Json]")
{
	// The distinction has to survive: a data edge redrawn as an execution edge would lose the resource it
	// carries, and the reverse would invent one.
	const FFramePassTypeRegistry Types = MakeTestRegistry();
	const char* const Document = R"({
		"passes": [
			{ "name": "A", "type": "Source" },
			{ "name": "B", "type": "Filter" }
		],
		"edges": [
			{ "from": "A", "to": "B", "kind": "execution" }
		],
		"graphOutputs": [ "B.out" ]
	})";

	FFrameGraphDesc Graph;
	REQUIRE(FFrameGraphJson::LoadFromString(Document, Types, Graph).bSucceeded);
	REQUIRE(Graph.GetEdges().size() == 1);
	REQUIRE(Graph.GetEdges()[0].Kind == EFrameEdgeKind::Execution);
	REQUIRE(Graph.GetEdges()[0].From.IsPassOnly());

	FFrameGraphDesc Again;
	REQUIRE(FFrameGraphJson::LoadFromString(FFrameGraphJson::SaveToString(Graph), Types, Again).bSucceeded);
	REQUIRE(Again.GetEdges() == Graph.GetEdges());
}

TEST_CASE("A malformed element is dropped without losing the graph", "[FrameGraph][Json]")
{
	const FFramePassTypeRegistry Types = MakeTestRegistry();

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

		FFrameGraphDesc Graph;
		const FFrameGraphLoadResult Result = FFrameGraphJson::LoadFromString(Document, Types, Graph);
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

		FFrameGraphDesc Graph;
		const FFrameGraphLoadResult Result = FFrameGraphJson::LoadFromString(Document, Types, Graph);
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

		FFrameGraphDesc Graph;
		const FFrameGraphLoadResult Result = FFrameGraphJson::LoadFromString(Document, Types, Graph);
		REQUIRE(Result.bSucceeded);
		REQUIRE(Graph.GetPasses().size() == 1);
	}
}

TEST_CASE("An unusable document fails outright", "[FrameGraph][Json]")
{
	const FFramePassTypeRegistry Types = MakeTestRegistry();

	SECTION("Text that is not JSON")
	{
		FFrameGraphDesc Graph;
		const FFrameGraphLoadResult Result = FFrameGraphJson::LoadFromString("this is not json", Types, Graph);
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE(CountErrors(Result) > 0);
	}

	SECTION("A document without a passes array")
	{
		// Nothing to salvage: without passes there is no graph, and edges would all dangle.
		FFrameGraphDesc Graph;
		const FFrameGraphLoadResult Result = FFrameGraphJson::LoadFromString(R"({"name":"Empty"})", Types, Graph);
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE(CountErrors(Result) > 0);
	}

	SECTION("A failed load leaves the previous graph alone")
	{
		// The panel keeps showing what it had, rather than replacing a working graph with an empty one
		// because a path was mistyped.
		FFrameGraphDesc Graph;
		REQUIRE(FFrameGraphJson::LoadFromString(ValidGraph, Types, Graph).bSucceeded);
		REQUIRE(Graph.GetPasses().size() == 2);

		REQUIRE_FALSE(FFrameGraphJson::LoadFromString("{{{", Types, Graph).bSucceeded);
		REQUIRE(Graph.GetPasses().size() == 2);
	}
}

TEST_CASE("A frame graph round trips through a file", "[FrameGraph][Json]")
{
	const FFramePassTypeRegistry Types = MakeTestRegistry();
	const std::filesystem::path Path = std::filesystem::temp_directory_path() / "LimeFrameGraphTest.json";

	FFrameGraphDesc Graph;
	REQUIRE(FFrameGraphJson::LoadFromString(ValidGraph, Types, Graph).bSucceeded);
	REQUIRE(FFrameGraphJson::SaveToFile(Path, Graph));

	FFrameGraphDesc Reloaded;
	const FFrameGraphLoadResult Result = FFrameGraphJson::LoadFromFile(Path, Types, Reloaded);
	REQUIRE(Result.bSucceeded);
	REQUIRE(Reloaded.GetPasses().size() == Graph.GetPasses().size());
	REQUIRE(Reloaded.GetEdges() == Graph.GetEdges());

	std::error_code Error;
	std::filesystem::remove(Path, Error);
}

TEST_CASE("A missing file is reported rather than throwing", "[FrameGraph][Json]")
{
	const FFramePassTypeRegistry Types = MakeTestRegistry();
	FFrameGraphDesc Graph;
	const FFrameGraphLoadResult Result =
	    FFrameGraphJson::LoadFromFile(std::filesystem::temp_directory_path() / "LimeNoSuchGraph.json", Types, Graph);

	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE(CountErrors(Result) > 0);
}
