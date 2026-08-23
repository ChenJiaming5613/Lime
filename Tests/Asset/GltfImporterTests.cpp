// glTF import tests.
//
// Every document is embedded as a string with its buffers base64 encoded, so the suite needs no
// fixture files and stays runnable on a machine with no graphics device.
//
// The cases are chosen around what the Khronos sample assets actually contain: missing normals, index
// types of every width, non triangle primitives, node transforms given as a matrix rather than as
// components, and malformed files.

#include "Asset/GltfImporter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using namespace Lime;

namespace
{
	// A single triangle with positions, normals and texture coordinates, indexed with unsigned shorts.
	//
	// Buffer layout, 104 bytes: 3 indices as uint16 (6) + 2 bytes of padding so the floats that follow
	// start 4 byte aligned, then 3 positions (36), 3 normals (36) and 3 texture coordinates (24). The
	// padding is what glTF requires of accessor alignment, not an arbitrary choice.
	constexpr const char* TriangleGltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0, "name": "Triangle" } ],
  "meshes": [ {
    "name": "TriangleMesh",
    "primitives": [ {
      "attributes": { "POSITION": 1, "NORMAL": 2, "TEXCOORD_0": 3 },
      "indices": 0,
      "material": 0,
      "mode": 4
    } ]
  } ],
  "materials": [ {
    "name": "Red",
    "pbrMetallicRoughness": { "baseColorFactor": [ 1.0, 0.0, 0.0, 1.0 ] },
    "doubleSided": true
  } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5123, "count": 3, "type": "SCALAR" },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [ 0.0, 0.0, 0.0 ], "max": [ 1.0, 1.0, 0.0 ] },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC2" }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 6 },
    { "buffer": 0, "byteOffset": 8,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 44, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 80, "byteLength": 24 }
  ],
  "buffers": [ {
    "byteLength": 104,
    "uri": "data:application/octet-stream;base64,AAABAAIAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8="
  } ]
})";

	// Same triangle, but the primitive has no NORMAL attribute. The importer has to derive one.
	constexpr const char* NoNormalsGltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 1 }, "indices": 0, "mode": 4 } ] } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5123, "count": 3, "type": "SCALAR" },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 6 },
    { "buffer": 0, "byteOffset": 8, "byteLength": 36 }
  ],
  "buffers": [ {
    "byteLength": 44,
    "uri": "data:application/octet-stream;base64,AAABAAIAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAA="
  } ]
})";

	// A parent node with one child, to check that the hierarchy survives import.
	constexpr const char* HierarchyGltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [
    { "name": "Parent", "children": [ 1 ], "translation": [ 1.0, 2.0, 3.0 ] },
    { "name": "Child", "scale": [ 2.0, 2.0, 2.0 ], "rotation": [ 0.0, 0.7071068, 0.0, 0.7071068 ] }
  ]
})";

	// A node whose transform is a full matrix: 90 degrees about Y, scale 2, translated along X.
	// Column major in the file, so the translation occupies elements 12 to 14.
	constexpr const char* MatrixNodeGltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ {
    "name": "MatrixNode",
    "matrix": [ 0.0, 0.0, -2.0, 0.0,
                0.0, 2.0, 0.0, 0.0,
                2.0, 0.0, 0.0, 0.0,
                5.0, 0.0, 0.0, 1.0 ]
  } ]
})";

	// A primitive drawn as a triangle strip. Unsupported, and must be skipped rather than misdrawn.
	constexpr const char* TriangleStripGltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "mode": 5 } ] } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "buffers": [ {
    "byteLength": 36,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/"
  } ]
})";

	FGltfImportResult Import(const char* Json)
	{
		return FGltfImporter::LoadFromString(Json, {});
	}
} // namespace

TEST_CASE("Importing a triangle yields the expected geometry", "[Asset][Gltf]")
{
	const FGltfImportResult Result = Import(TriangleGltf);
	// The message carries tinygltf's reason, which is what makes a broken fixture diagnosable instead of
	// just reporting false.
	INFO("importer message: " << Result.Message);
	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Scene.Meshes.size() == 1);

	const FMeshData& Mesh = Result.Scene.Meshes[0];

	SECTION("Vertices, indices and sections are counted correctly")
	{
		REQUIRE(Mesh.Name == "TriangleMesh");
		REQUIRE(Mesh.Vertices.size() == 3);
		REQUIRE(Mesh.Indices.size() == 3);
		REQUIRE(Mesh.GetTriangleCount() == 1);
		REQUIRE(Mesh.Sections.size() == 1);
		REQUIRE(Mesh.Sections[0].FirstIndex == 0);
		REQUIRE(Mesh.Sections[0].IndexCount == 3);
		REQUIRE(Mesh.Sections[0].MaterialIndex == 0);
	}

	SECTION("Positions are read in order")
	{
		REQUIRE(IsNearlyEqual(Mesh.Vertices[0].Position, FVector3{ 0.0f, 0.0f, 0.0f }));
		REQUIRE(IsNearlyEqual(Mesh.Vertices[1].Position, FVector3{ 1.0f, 0.0f, 0.0f }));
		REQUIRE(IsNearlyEqual(Mesh.Vertices[2].Position, FVector3{ 0.0f, 1.0f, 0.0f }));
	}

	SECTION("Authored normals are kept rather than regenerated")
	{
		for (const FMeshVertex& Vertex : Mesh.Vertices)
		{
			REQUIRE(IsNearlyEqual(Vertex.Normal, FVector3::UnitZ(), 1.0e-4f));
		}
	}

	SECTION("Texture coordinates are read")
	{
		REQUIRE(IsNearlyEqual(Mesh.Vertices[1].TexCoord, FVector2{ 1.0f, 0.0f }, 1.0e-5f));
	}

	SECTION("Bounds enclose every vertex")
	{
		REQUIRE(Mesh.Bounds.bValid);
		REQUIRE(IsNearlyEqual(Mesh.Bounds.Min, FVector3{ 0.0f, 0.0f, 0.0f }));
		REQUIRE(IsNearlyEqual(Mesh.Bounds.Max, FVector3{ 1.0f, 1.0f, 0.0f }));
		REQUIRE(Mesh.Bounds.GetLongestEdge() == Approx(1.0f));
	}

	SECTION("Material factors are carried over")
	{
		REQUIRE(Result.Scene.Materials.size() == 1);
		REQUIRE(Result.Scene.Materials[0].Name == "Red");
		REQUIRE(IsNearlyEqual(Result.Scene.Materials[0].BaseColorFactor, FVector4{ 1.0f, 0.0f, 0.0f, 1.0f }));
		REQUIRE(Result.Scene.Materials[0].bDoubleSided);
		// No texture in this document, so nothing should be referenced.
		REQUIRE(Result.Scene.Materials[0].BaseColorImage == -1);
	}

	SECTION("The scene exposes its single root")
	{
		REQUIRE(Result.Scene.RootNodes.size() == 1);
		REQUIRE(Result.Scene.Nodes.size() == 1);
		REQUIRE(Result.Scene.Nodes[0].Name == "Triangle");
		REQUIRE(Result.Scene.Nodes[0].MeshIndex == 0);
		REQUIRE(Result.Scene.GetTotalTriangleCount() == 1);
	}
}

TEST_CASE("Missing normals are generated", "[Asset][Gltf]")
{
	// Without this the surface would receive uniform lighting and read as a flat silhouette, so the
	// generated normal has to be both present and unit length.
	const FGltfImportResult Result = Import(NoNormalsGltf);
	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Scene.Meshes.size() == 1);

	const FMeshData& Mesh = Result.Scene.Meshes[0];
	REQUIRE(Mesh.Vertices.size() == 3);

	for (const FMeshVertex& Vertex : Mesh.Vertices)
	{
		REQUIRE(Vertex.Normal.Length() == Approx(1.0f).margin(1.0e-4f));
	}

	SECTION("The generated normal is perpendicular to the triangle")
	{
		const FVector3 Edge1 = Mesh.Vertices[1].Position - Mesh.Vertices[0].Position;
		const FVector3 Edge2 = Mesh.Vertices[2].Position - Mesh.Vertices[0].Position;
		REQUIRE(Dot(Mesh.Vertices[0].Normal, Edge1) == Approx(0.0f).margin(1.0e-4f));
		REQUIRE(Dot(Mesh.Vertices[0].Normal, Edge2) == Approx(0.0f).margin(1.0e-4f));
	}
}

TEST_CASE("Node hierarchy and transforms survive import", "[Asset][Gltf]")
{
	const FGltfImportResult Result = Import(HierarchyGltf);
	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Scene.Nodes.size() == 2);
	REQUIRE(Result.Scene.RootNodes.size() == 1);
	REQUIRE(Result.Scene.RootNodes[0] == 0);

	const FSceneNodeData& Parent = Result.Scene.Nodes[0];
	const FSceneNodeData& Child = Result.Scene.Nodes[1];

	REQUIRE(Parent.Name == "Parent");
	REQUIRE(Parent.Children.size() == 1);
	REQUIRE(Parent.Children[0] == 1);
	REQUIRE(IsNearlyEqual(Parent.Translation, FVector3{ 1.0f, 2.0f, 3.0f }));

	REQUIRE(Child.Name == "Child");
	REQUIRE(Child.Children.empty());
	REQUIRE(IsNearlyEqual(Child.Scale, FVector3{ 2.0f, 2.0f, 2.0f }));

	SECTION("A quarter turn about Y is read from the xyzw quaternion")
	{
		// glTF and FQuat use the same component order, so a mismatch here would show up as a wrong axis.
		REQUIRE(IsNearlyEqual(Child.Rotation, FQuat::FromAxisAngle(FVector3::UnitY(), HalfPi), 1.0e-4f));
	}

	SECTION("Nodes without a mesh are marked as such")
	{
		REQUIRE(Parent.MeshIndex == -1);
		REQUIRE(Child.MeshIndex == -1);
	}
}

TEST_CASE("A matrix node is decomposed into components", "[Asset][Gltf]")
{
	// glTF allows either a matrix or separate components. Decomposing means the editor can present
	// editable values instead of sixteen opaque numbers.
	const FGltfImportResult Result = Import(MatrixNodeGltf);
	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Scene.Nodes.size() == 1);

	const FSceneNodeData& Node = Result.Scene.Nodes[0];

	REQUIRE(IsNearlyEqual(Node.Translation, FVector3{ 5.0f, 0.0f, 0.0f }, 1.0e-4f));
	REQUIRE(IsNearlyEqual(Node.Scale, FVector3{ 2.0f, 2.0f, 2.0f }, 1.0e-4f));

	SECTION("Recomposing reproduces the original transform")
	{
		// The strongest check available: whatever convention the decomposition used, putting the pieces
		// back together has to place a point where the matrix would have.
		const FMatrix4x4 Recomposed = MakeTransform(Node.Translation, Node.Rotation, Node.Scale);
		const FVector3 Point{ 1.0f, 0.0f, 0.0f };

		// The authored matrix maps +X onto -Z, scaled by 2 and offset by 5 along X.
		REQUIRE(IsNearlyEqual(Recomposed.TransformPosition(Point), FVector3{ 5.0f, 0.0f, -2.0f }, 1.0e-3f));
	}
}

TEST_CASE("Unsupported primitive modes are skipped", "[Asset][Gltf]")
{
	// Strips and fans appear in the sample assets. Skipping the primitive loses part of a model, which is
	// far better than drawing it with the wrong topology or failing the whole import.
	const FGltfImportResult Result = Import(TriangleStripGltf);
	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Scene.Meshes.size() == 1);
	REQUIRE(Result.Scene.Meshes[0].Sections.empty());
	REQUIRE(Result.Scene.Meshes[0].Vertices.empty());
	REQUIRE(Result.Scene.GetTotalTriangleCount() == 0);
}

TEST_CASE("Malformed input fails without crashing", "[Asset][Gltf]")
{
	SECTION("Not JSON at all")
	{
		const FGltfImportResult Result = Import("this is not gltf");
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE_FALSE(Result.Message.empty());
		REQUIRE(Result.Scene.IsEmpty());
	}

	SECTION("Valid JSON that is not a glTF document")
	{
		const FGltfImportResult Result = Import(R"({ "hello": "world" })");
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE_FALSE(Result.Message.empty());
	}

	SECTION("An empty document")
	{
		const FGltfImportResult Result = Import("");
		REQUIRE_FALSE(Result.bSucceeded);
	}
}

TEST_CASE("A missing file is reported rather than throwing", "[Asset][Gltf]")
{
	// The engine must start with an empty scene when a configured path is wrong, so this path has to
	// return a message instead of raising.
	const FGltfImportResult Result = FGltfImporter::LoadFromFile("does/not/exist.gltf");
	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE(Result.Message.find("does not exist") != std::string::npos);
	REQUIRE(Result.Scene.IsEmpty());
}

TEST_CASE("Bounding box helpers", "[Asset][Gltf]")
{
	SECTION("An empty box reports no extent")
	{
		const FBoundingBox Box;
		REQUIRE_FALSE(Box.bValid);
		REQUIRE(Box.GetLongestEdge() == Approx(0.0f));
		REQUIRE(Box.GetCenter() == FVector3::Zero());
	}

	SECTION("The first point seeds both corners")
	{
		// Starting from a zero box would wrongly stretch the bounds back to the origin for a model that
		// sits away from it, which would then place the camera in the wrong spot.
		FBoundingBox Box;
		Box.Include({ 10.0f, 10.0f, 10.0f });
		REQUIRE(Box.bValid);
		REQUIRE(IsNearlyEqual(Box.GetCenter(), FVector3{ 10.0f, 10.0f, 10.0f }));
		REQUIRE(Box.GetLongestEdge() == Approx(0.0f));
	}

	SECTION("Bounds grow to enclose every point")
	{
		FBoundingBox Box;
		Box.Include({ -1.0f, 0.0f, 2.0f });
		Box.Include({ 3.0f, -4.0f, 2.0f });
		REQUIRE(IsNearlyEqual(Box.Min, FVector3{ -1.0f, -4.0f, 2.0f }));
		REQUIRE(IsNearlyEqual(Box.Max, FVector3{ 3.0f, 0.0f, 2.0f }));
		REQUIRE(Box.GetLongestEdge() == Approx(4.0f));
	}
}
