// Covers the viewport target's resize decision, which runs on the CPU and decides whether GPU
// resources are rebuilt. Getting it wrong either recreates the texture every frame or never at all.

#include "Renderer/ViewportTarget.h"

#include <catch2/catch_test_macros.hpp>

using namespace Lime;

TEST_CASE("Sizes are clamped into a usable range", "[Renderer][ViewportTarget]")
{
	REQUIRE(FViewportTarget::ClampSize(0) == FViewportTarget::MinSize);
	REQUIRE(FViewportTarget::ClampSize(1) == 1);
	REQUIRE(FViewportTarget::ClampSize(1920) == 1920);
	REQUIRE(FViewportTarget::ClampSize(FViewportTarget::MaxSize) == FViewportTarget::MaxSize);
	REQUIRE(FViewportTarget::ClampSize(FViewportTarget::MaxSize + 1) == FViewportTarget::MaxSize);
	REQUIRE(FViewportTarget::ClampSize(0xFFFFFFFFu) == FViewportTarget::MaxSize);
}

TEST_CASE("A resize is only needed when the size actually changes", "[Renderer][ViewportTarget]")
{
	REQUIRE(FViewportTarget::NeedsResize(800, 600, 1024, 768));
	REQUIRE(FViewportTarget::NeedsResize(800, 600, 800, 601));
	REQUIRE(FViewportTarget::NeedsResize(800, 600, 801, 600));
	REQUIRE_FALSE(FViewportTarget::NeedsResize(800, 600, 800, 600));
}

TEST_CASE("A zero request means the panel has not reported a size yet", "[Renderer][ViewportTarget]")
{
	// A collapsed or tabbed-out panel reports zero, which must not destroy the target.
	REQUIRE_FALSE(FViewportTarget::NeedsResize(800, 600, 0, 0));
	REQUIRE_FALSE(FViewportTarget::NeedsResize(800, 600, 1024, 0));
	REQUIRE_FALSE(FViewportTarget::NeedsResize(800, 600, 0, 768));
}

TEST_CASE("Clamping is applied before comparing, so a clamped request settles", "[Renderer][ViewportTarget]")
{
	// Without clamping on both sides an out of range request would rebuild forever.
	REQUIRE_FALSE(FViewportTarget::NeedsResize(FViewportTarget::MaxSize, 600, FViewportTarget::MaxSize + 500, 600));
	REQUIRE_FALSE(FViewportTarget::NeedsResize(1, 1, 1, 1));
}
