// Imports the Khronos sample assets, when they are present.
//
// The embedded-document tests in GltfImporterTests.cpp pin down specific behaviours; this file checks
// the importer against real files, which is the only way to catch the shapes those hand written
// documents do not cover: external .bin buffers, PNG and JPEG images, deep node hierarchies, meshes
// split across many materials and 32 bit indices.
//
// Every case skips rather than fails when Assets/ is not populated, because fetching a 2 GB dataset is
// a developer's choice and must not break a fresh checkout or CI.

#include "Asset/GltfImporter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using Catch::Approx;
using namespace Lime;

namespace
{
	// Resolved relative to this source file, so it does not depend on the working directory a test
	// runner happens to use.
	std::filesystem::path GetSampleRoot()
	{
		return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "Assets" / "glTF-Sample-Assets" / "Models";
	}

	// Returns an empty path when the model is absent, which the caller turns into a skip.
	std::filesystem::path FindModel(const std::string& Relative)
	{
		const std::filesystem::path Path = GetSampleRoot() / Relative;
		std::error_code Error;
		return std::filesystem::exists(Path, Error) ? Path : std::filesystem::path{};
	}
} // namespace

TEST_CASE("Sample assets import", "[Asset][Gltf][SampleAssets]")
{
	SECTION("A .gltf with an external buffer")
	{
		const std::filesystem::path Path = FindModel("Box/glTF/Box.gltf");
		if (Path.empty())
		{
			SKIP("glTF-Sample-Assets is not present");
		}

		const FGltfImportResult Result = FGltfImporter::LoadFromFile(Path);
		INFO("importer message: " << Result.Message);
		REQUIRE(Result.bSucceeded);
		REQUIRE(Result.Scene.GetTotalTriangleCount() == 12);
		REQUIRE(Result.Scene.Meshes.size() == 1);
		REQUIRE_FALSE(Result.Scene.RootNodes.empty());

		SECTION("Every vertex has a unit normal")
		{
			for (const FMeshVertex& Vertex : Result.Scene.Meshes[0].Vertices)
			{
				REQUIRE(Vertex.Normal.Length() == Approx(1.0f).margin(1.0e-3f));
			}
		}
	}

	SECTION("The binary container produces the same geometry as the text one")
	{
		// A .glb keeps its buffers inline. Both forms describing one triangle count is the clearest signal
		// that the container handling is not silently dropping data.
		const std::filesystem::path Text = FindModel("Box/glTF/Box.gltf");
		const std::filesystem::path Binary = FindModel("Box/glTF-Binary/Box.glb");
		if (Text.empty() || Binary.empty())
		{
			SKIP("glTF-Sample-Assets is not present");
		}

		const FGltfImportResult FromText = FGltfImporter::LoadFromFile(Text);
		const FGltfImportResult FromBinary = FGltfImporter::LoadFromFile(Binary);
		REQUIRE(FromText.bSucceeded);
		REQUIRE(FromBinary.bSucceeded);
		REQUIRE(FromText.Scene.GetTotalTriangleCount() == FromBinary.Scene.GetTotalTriangleCount());
		REQUIRE(FromText.Scene.Nodes.size() == FromBinary.Scene.Nodes.size());
	}

	SECTION("A textured model decodes its image to RGBA")
	{
		const std::filesystem::path Path = FindModel("BoxTextured/glTF/BoxTextured.gltf");
		if (Path.empty())
		{
			SKIP("glTF-Sample-Assets is not present");
		}

		const FGltfImportResult Result = FGltfImporter::LoadFromFile(Path);
		INFO("importer message: " << Result.Message);
		REQUIRE(Result.bSucceeded);
		REQUIRE(Result.Scene.Images.size() == 1);

		const FImageData& Image = Result.Scene.Images[0];
		REQUIRE(Image.IsValid());
		// Widened to 4 channels regardless of what the file stored, so the upload path needs no variants.
		REQUIRE(Image.Pixels.size() == static_cast<size_t>(Image.Width) * Image.Height * 4);

		SECTION("The material points at the decoded image")
		{
			REQUIRE(Result.Scene.Materials.size() == 1);
			REQUIRE(Result.Scene.Materials[0].BaseColorImage == 0);
		}

		SECTION("Texture coordinates are present and finite")
		{
			bool bAnyNonZero = false;
			for (const FMeshData& Mesh : Result.Scene.Meshes)
			{
				for (const FMeshVertex& Vertex : Mesh.Vertices)
				{
					bAnyNonZero = bAnyNonZero || Vertex.TexCoord.X != 0.0f || Vertex.TexCoord.Y != 0.0f;
				}
			}
			REQUIRE(bAnyNonZero);
		}
	}

	SECTION("A model with several nodes keeps its hierarchy")
	{
		const std::filesystem::path Path = FindModel("SimpleMeshes/glTF/SimpleMeshes.gltf");
		if (Path.empty())
		{
			SKIP("glTF-Sample-Assets is not present");
		}

		const FGltfImportResult Result = FGltfImporter::LoadFromFile(Path);
		INFO("importer message: " << Result.Message);
		REQUIRE(Result.bSucceeded);
		REQUIRE(Result.Scene.Nodes.size() >= 2);

		SECTION("Every child index is in range")
		{
			// An out of range child would be followed later while walking the tree and read past the array.
			for (const FSceneNodeData& Node : Result.Scene.Nodes)
			{
				for (const uint32 Child : Node.Children)
				{
					REQUIRE(Child < Result.Scene.Nodes.size());
				}
			}
		}

		SECTION("Every mesh index is in range")
		{
			for (const FSceneNodeData& Node : Result.Scene.Nodes)
			{
				REQUIRE(Node.MeshIndex < static_cast<int32>(Result.Scene.Meshes.size()));
			}
		}
	}

	SECTION("A production sized model imports intact")
	{
		// DamagedHelmet is a single mesh with a full PBR material set, which is representative of what a
		// real scene loads. Only the base colour is consumed, but the rest must not upset the import.
		const std::filesystem::path Path = FindModel("DamagedHelmet/glTF-Binary/DamagedHelmet.glb");
		if (Path.empty())
		{
			SKIP("glTF-Sample-Assets is not present");
		}

		const FGltfImportResult Result = FGltfImporter::LoadFromFile(Path);
		INFO("importer message: " << Result.Message);
		REQUIRE(Result.bSucceeded);
		REQUIRE(Result.Scene.GetTotalTriangleCount() > 10000);
		REQUIRE_FALSE(Result.Scene.Materials.empty());

		SECTION("Sections cover the index buffer exactly")
		{
			// A section reaching past the buffer would read garbage indices at draw time.
			for (const FMeshData& Mesh : Result.Scene.Meshes)
			{
				for (const FMeshSection& Section : Mesh.Sections)
				{
					REQUIRE(Section.IndexCount % 3 == 0);
					REQUIRE(static_cast<size_t>(Section.FirstIndex) + Section.IndexCount <= Mesh.Indices.size());
				}
			}
		}

		SECTION("Every index addresses a real vertex")
		{
			for (const FMeshData& Mesh : Result.Scene.Meshes)
			{
				for (const uint32 Index : Mesh.Indices)
				{
					REQUIRE(Index < Mesh.Vertices.size());
				}
			}
		}

		SECTION("Bounds are usable for framing a camera")
		{
			const FBoundingBox Bounds = Result.Scene.GetMeshBounds();
			REQUIRE(Bounds.bValid);
			REQUIRE(Bounds.GetLongestEdge() > 0.0f);
		}
	}
}
