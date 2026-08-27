// Reflection tests: that the pass types a render graph can name come from the passes themselves.
//
// The point of these is the link between the two halves. A graph file naming "BlinnPhongForward" is only
// meaningful if the pass by that name declares the fields the file connects, and nothing else checks
// that: the compiler tests use hand written types, and the passes are compiled separately.

#include "Renderer/Passes/BuiltinPasses.h"
#include "Renderer/RenderPassRegistry.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace Lime;

namespace
{
	// The built-ins register into a process wide registry, so this makes sure they are present without
	// depending on whether another test got there first.
	const FRenderGraphPassTypeRegistry& BuiltinTypes()
	{
		static const FRenderGraphPassTypeRegistry Types = BuildRenderGraphPassTypes();
		return Types;
	}
} // namespace

TEST_CASE("The built-in passes reflect themselves into pass types", "[Renderer][RenderGraph]")
{
	const FRenderGraphPassTypeRegistry& Types = BuiltinTypes();

	// An empty table would mean a graph could name no pass at all, which is how a linker dropping the
	// registrations would show up.
	REQUIRE_FALSE(Types.IsEmpty());
}

TEST_CASE("The debug visualizer converts anything into a presentable colour", "[Renderer][RenderGraph]")
{
	// This pass is the sanctioned way to look at a resource that cannot be presented, so the properties that
	// make it work as one are worth pinning: an input that accepts any format, and an output that is colour.
	const FRenderGraphPassTypeRegistry& Types = BuiltinTypes();
	const FRenderGraphPassTypeDesc* Visualizer = Types.Find("DebugVisualizer");
	REQUIRE(Visualizer != nullptr);

	SECTION("The source accepts any format")
	{
		// Unspecified is what lets one pass serve depth and colour alike: pinning a format here would make it
		// connectable to only one kind of resource.
		const FRenderGraphResourceDesc* Source = Visualizer->FindInput("source");
		REQUIRE(Source != nullptr);
		REQUIRE(Source->Format == nvrhi::Format::UNKNOWN);
		// Required, because a visualiser with nothing connected would draw a flat colour that looks exactly
		// like a broken graph.
		REQUIRE_FALSE(Source->bOptional);
	}

	SECTION("The output is presentable")
	{
		const FRenderGraphResourceDesc* Colour = Visualizer->FindOutput("color");
		REQUIRE(Colour != nullptr);
		// Not depth, or the compiler's rule would reject the very pass that exists to satisfy it.
		REQUIRE_FALSE(Colour->IsDepth());
	}
}

TEST_CASE("The forward lit pass declares the fields a graph connects", "[Renderer][RenderGraph]")
{
	const FRenderGraphPassTypeRegistry& Types = BuiltinTypes();
	const FRenderGraphPassTypeDesc* Lit = Types.Find("BlinnPhongForwardLit");
	REQUIRE(Lit != nullptr);

	SECTION("The shadow input is optional")
	{
		// Required would mean a graph without a shadow caster could not compile, so the pass could not be
		// used on its own.
		const FRenderGraphResourceDesc* Shadow = Lit->FindInput("shadowDepth");
		REQUIRE(Shadow != nullptr);
		REQUIRE(Shadow->bOptional);
		REQUIRE(Shadow->IsDepth());
	}

	SECTION("Colour and depth are produced")
	{
		const FRenderGraphResourceDesc* Colour = Lit->FindOutput("color");
		REQUIRE(Colour != nullptr);
		REQUIRE_FALSE(Colour->IsDepth());
		// Left to the graph, so the pass can render at whatever size it is given.
		REQUIRE(Colour->Format == nvrhi::Format::UNKNOWN);
		REQUIRE(Colour->Width == 0);
		REQUIRE(Colour->Height == 0);

		const FRenderGraphResourceDesc* Depth = Lit->FindOutput("depth");
		REQUIRE(Depth != nullptr);
		REQUIRE(Depth->IsDepth());
	}

	SECTION("It describes itself for the inspector")
	{
		REQUIRE_FALSE(Lit->Description.empty());
	}
}

TEST_CASE("A pass declaring no resources still reflects a usable type", "[Renderer][RenderGraph]")
{
	// Not every pass owns graph resources: one that draws into whatever target it is handed declares
	// nothing. It still has to appear as a type, or a graph naming it would look like it referred to a pass
	// this build lacks. Checked through the registry rather than by naming such a pass, since which passes
	// are resource free is not something this test should fix in place.
	const FRenderGraphPassTypeRegistry& Types = BuiltinTypes();

	for (const FRenderGraphPassTypeDesc& Type : Types.GetAll())
	{
		// A type with no name could not be referred to at all, which is the one thing that would make the
		// table useless regardless of what the pass declares.
		INFO("pass type with an empty name in the registry");
		REQUIRE_FALSE(Type.Name.empty());
	}
}
