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

#include <filesystem>
#include <fstream>
#include <string>

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

	// A document whose image data is not any format stb can read, standing in for the DDS that a scene
	// using MSFT_texture_dds lists alongside each PNG.
	//
	// tinygltf's own loader reports this as an error, which aborts the whole parse. One unsupported
	// texture must not cost an entire model, so the importer downgrades it to a warning.
	constexpr const char* UndecodableImageGltf = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0, "name": "Textured" } ],
  "meshes": [ {
    "primitives": [ { "attributes": { "POSITION": 1 }, "indices": 0, "material": 0, "mode": 4 } ]
  } ],
  "materials": [ {
    "pbrMetallicRoughness": {
      "baseColorFactor": [ 0.5, 0.6, 0.7, 1.0 ],
      "baseColorTexture": { "index": 0 }
    }
  } ],
  "textures": [ { "source": 0 } ],
  "images": [ { "uri": "data:image/vnd-ms.dds;base64,RERTIHwAAAAHEAAA" } ],
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

	// A triangle in the XY plane with an authored TANGENT, for the handedness conversion.
	//
	// Chosen so every expected value is exact: positions at the origin and along +X and +Y, normals at +Z,
	// tangents at +X with handedness +1, and UVs whose U runs along +X and V along +Y. Both the reflection
	// and the sign inversion are then readable off the result rather than being buried in arithmetic.
	//
	// Buffer, 152 bytes: 3 uint16 indices (6) + 2 padding, 3 positions (36), 3 normals (36), 3 VEC4
	// tangents (48), 3 texcoords (24).
	constexpr const char* TangentBuffer =
	    "data:application/octet-stream;base64,AAABAAIAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAA"
	    "AAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAA"
	    "AAAAgD8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8=";

	// Attributes are substituted, so the tangent-bearing and tangent-free cases describe the same geometry
	// down to the byte. That is what lets the two be compared against each other: any difference in the
	// resulting frame is the importer's doing, not the fixture's.
	std::string MakeTangentGltf(bool bDeclareTangent)
	{
		std::string Attributes = R"("POSITION": 1, "NORMAL": 2, "TEXCOORD_0": 4)";
		if (bDeclareTangent)
		{
			Attributes += R"(, "TANGENT": 3)";
		}

		std::string Json = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { ATTRIBUTES }, "indices": 0, "mode": 4 } ] } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5123, "count": 3, "type": "SCALAR" },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC2" }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 6 },
    { "buffer": 0, "byteOffset": 8,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 44,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 80,  "byteLength": 48 },
    { "buffer": 0, "byteOffset": 128, "byteLength": 24 }
  ],
  "buffers": [ { "byteLength": 152, "uri": "BUFFER" } ]
})";

		const std::string AttributePlaceholder = "ATTRIBUTES";
		Json.replace(Json.find(AttributePlaceholder), AttributePlaceholder.size(), Attributes);
		const std::string BufferPlaceholder = "BUFFER";
		Json.replace(Json.find(BufferPlaceholder), BufferPlaceholder.size(), TangentBuffer);
		return Json;
	}

	// A 1x1 opaque PNG, which stb decodes. Used wherever a slot needs a real image rather than a
	// reference: the colour does not matter, only that the image is valid and gets a format assigned.
	constexpr const char* OnePixelPng =
	    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8DwHwAFAAH/"
	    "q842iQAAAABJRU5ErkJggg==";

	// Every metallic-roughness slot filled, each pointing at its own image.
	//
	// Distinct images per slot on purpose: the colour space is decided per slot, so sharing one image
	// would hide whether the importer resolves them separately. Image 0 and 3 are colour, 1, 2 and 4 are
	// measurements.
	std::string MakeFullMaterialGltf()
	{
		std::string Json = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ {
    "primitives": [ { "attributes": { "POSITION": 1 }, "indices": 0, "material": 0, "mode": 4 } ]
  } ],
  "materials": [ {
    "name": "Full",
    "pbrMetallicRoughness": {
      "baseColorFactor": [ 0.1, 0.2, 0.3, 1.0 ],
      "baseColorTexture": { "index": 0 },
      "metallicFactor": 0.25,
      "roughnessFactor": 0.75,
      "metallicRoughnessTexture": { "index": 1 }
    },
    "normalTexture": { "index": 2, "scale": 0.5 },
    "emissiveFactor": [ 1.0, 0.5, 0.25 ],
    "emissiveTexture": { "index": 3 },
    "occlusionTexture": { "index": 4, "strength": 0.4 }
  } ],
  "textures": [
    { "source": 0 }, { "source": 1 }, { "source": 2 }, { "source": 3 }, { "source": 4 }
  ],
  "images": [ IMAGES ],
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

		std::string Images;
		for (int32 Index = 0; Index < 5; ++Index)
		{
			Images += (Index == 0 ? "" : ", ");
			Images += R"({ "uri": ")";
			Images += OnePixelPng;
			Images += R"(" })";
		}

		const std::string Placeholder = "IMAGES";
		Json.replace(Json.find(Placeholder), Placeholder.size(), Images);
		return Json;
	}

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
			// Authored as +Z, and -Z after import: glTF is right handed and this engine is left handed, so
			// the importer reflects Z. A normal that came through unchanged would point through its own
			// surface once the positions had been flipped.
			REQUIRE(IsNearlyEqual(Vertex.Normal, -FVector3::UnitZ(), 1.0e-4f));
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

TEST_CASE("Right handed glTF data is converted to the engine's left handed space", "[Asset][Gltf]")
{
	// The defect this guards against renders every scene mirrored. It is easy to miss, because a symmetric
	// model looks correct and only lettering or a known layout gives it away, and it is easy to reintroduce,
	// because each piece of the conversion looks optional on its own.
	const FGltfImportResult Result = Import(TriangleGltf);
	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Scene.Meshes.size() == 1);

	const FMeshData& Mesh = Result.Scene.Meshes[0];

	SECTION("Winding is reversed to match the reflected geometry")
	{
		// Authored as 0,1,2. Reflecting one axis reverses which way a triangle turns, so the importer has
		// to reverse the indices to match: otherwise every face is inside out, and with backface culling
		// enabled the model would disappear entirely.
		REQUIRE(Mesh.Indices[0] == 2);
		REQUIRE(Mesh.Indices[1] == 1);
		REQUIRE(Mesh.Indices[2] == 0);
	}

	SECTION("The triangle keeps the same shape it was authored with")
	{
		// A reflection preserves distances, so a conversion that got the signs wrong but stayed
		// self consistent would still fail here.
		const FVector3 A = Mesh.Vertices[0].Position;
		const FVector3 B = Mesh.Vertices[1].Position;
		const FVector3 C = Mesh.Vertices[2].Position;

		REQUIRE((B - A).Length() == Catch::Approx(1.0f).margin(1.0e-4f));
		REQUIRE((C - A).Length() == Catch::Approx(1.0f).margin(1.0e-4f));
	}

	SECTION("Normals stay consistent with the winding they belong to")
	{
		// The two have to agree. A conversion that flipped the normals but left the winding, or the other
		// way round, would light the surface as though it faced away from the camera.
		const FVector3 A = Mesh.Vertices[Mesh.Indices[0]].Position;
		const FVector3 B = Mesh.Vertices[Mesh.Indices[1]].Position;
		const FVector3 C = Mesh.Vertices[Mesh.Indices[2]].Position;

		// Left handed winding: the cross product of the edges points along the face normal.
		const FVector3 GeometricNormal = Cross(B - A, C - A).GetNormalized();
		const FVector3 Authored = Mesh.Vertices[0].Normal;

		REQUIRE(Dot(GeometricNormal, Authored) == Catch::Approx(1.0f).margin(1.0e-3f));
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
	// Authored at (1, 2, 3); Z is negated because glTF is right handed and this engine is not.
	REQUIRE(IsNearlyEqual(Parent.Translation, FVector3{ 1.0f, 2.0f, -3.0f }));

	REQUIRE(Child.Name == "Child");
	REQUIRE(Child.Children.empty());
	REQUIRE(IsNearlyEqual(Child.Scale, FVector3{ 2.0f, 2.0f, 2.0f }));

	SECTION("A quarter turn about Y is read from the xyzw quaternion")
	{
		// glTF and FQuat use the same component order, so a mismatch here would show up as a wrong axis.
		//
		// The authored turn is about +Y; it comes through as a turn about -Y because reflecting Z reverses
		// the direction of any rotation whose axis lies in the reflection plane. Verified against S*R*S,
		// which is what the conversion has to reproduce for the mirrored scene to stay self consistent.
		REQUIRE(IsNearlyEqual(Child.Rotation, FQuat::FromAxisAngle(-FVector3::UnitY(), HalfPi), 1.0e-4f));
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

		// The authored matrix maps +X onto -Z, scaled by 2 and offset by 5 along X. The importer reflects Z
		// on the way in, so the point that would have landed at -2 along Z lands at +2.
		REQUIRE(IsNearlyEqual(Recomposed.TransformPosition(Point), FVector3{ 5.0f, 0.0f, 2.0f }, 1.0e-3f));
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

TEST_CASE("An image stb cannot decode does not fail the import", "[Asset][Gltf]")
{
	// Found on the RTXDI Bistro scene, which lists a DDS next to every PNG for MSFT_texture_dds. stb
	// cannot read DDS, and tinygltf treats that as fatal, so a 5900 node model was lost to one texture.
	const FGltfImportResult Result = Import(UndecodableImageGltf);

	INFO("importer message: " << Result.Message);
	REQUIRE(Result.bSucceeded);

	SECTION("The geometry survives")
	{
		REQUIRE(Result.Scene.Meshes.size() == 1);
		REQUIRE(Result.Scene.GetTotalTriangleCount() == 1);
	}

	SECTION("The image is reported as unusable rather than half loaded")
	{
		// Zero sized, so the upload path substitutes the white texture instead of reading pixels that are
		// not there.
		REQUIRE(Result.Scene.Images.size() == 1);
		REQUIRE_FALSE(Result.Scene.Images[0].IsValid());
	}

	SECTION("The material stops referencing it")
	{
		// An index left pointing at an image with no pixels would be followed at upload time.
		REQUIRE(Result.Scene.Materials.size() == 1);
		REQUIRE(Result.Scene.Materials[0].BaseColorImage == -1);
	}

	SECTION("The colour factor is still applied")
	{
		// Losing the texture must not lose the rest of the material.
		REQUIRE(IsNearlyEqual(Result.Scene.Materials[0].BaseColorFactor, FVector4{ 0.5f, 0.6f, 0.7f, 1.0f }, 1.0e-3f));
	}
}

TEST_CASE("An authored tangent is converted into the engine's handedness", "[Asset][Gltf][Tangent]")
{
	const std::string Json = MakeTangentGltf(true);
	const FGltfImportResult Result = Import(Json.c_str());

	INFO("importer message: " << Result.Message);
	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Scene.Meshes.size() == 1);

	const FMeshData& Mesh = Result.Scene.Meshes[0];
	REQUIRE(Mesh.Vertices.size() == 3);

	SECTION("The direction reflects like a position")
	{
		// Authored along +X, which the Z reflection leaves alone. Included so a change that stopped
		// converting the direction at all is still caught by the W case below rather than passing silently.
		for (const FMeshVertex& Vertex : Mesh.Vertices)
		{
			const FVector3 Direction{ Vertex.Tangent.X, Vertex.Tangent.Y, Vertex.Tangent.Z };
			REQUIRE(IsNearlyEqual(Direction, FVector3{ 1.0f, 0.0f, 0.0f }, 1.0e-5f));
		}
	}

	SECTION("The handedness sign inverts")
	{
		// Authored as +1. The bitangent is cross(Normal, Tangent) * W, and a cross product picks up the
		// determinant of whatever was applied to its operands: -1 for a reflection. Reflecting the normal
		// and the tangent therefore flips the reconstructed bitangent by itself, and W has to flip back to
		// cancel that. Passing W through unchanged is the bug this asserts against.
		for (const FMeshVertex& Vertex : Mesh.Vertices)
		{
			REQUIRE(Vertex.Tangent.W == Approx(-1.0f));
		}
	}
}

TEST_CASE("A derived tangent frame matches what the file would have authored", "[Asset][Gltf][Tangent]")
{
	// The strongest check available on the handedness, because the two paths share no code: one reflects a
	// value read from the file, the other solves for it from positions and UVs already in engine space. A
	// sign error in either shows up as a disagreement, which no amount of testing one path alone would find.
	const std::string WithTangent = MakeTangentGltf(true);
	const std::string WithoutTangent = MakeTangentGltf(false);

	const FGltfImportResult Authored = Import(WithTangent.c_str());
	const FGltfImportResult Derived = Import(WithoutTangent.c_str());

	REQUIRE(Authored.bSucceeded);
	REQUIRE(Derived.bSucceeded);
	REQUIRE(Authored.Scene.Meshes[0].Vertices.size() == Derived.Scene.Meshes[0].Vertices.size());

	SECTION("Both paths agree on the whole frame")
	{
		for (SizeType Index = 0; Index < Authored.Scene.Meshes[0].Vertices.size(); ++Index)
		{
			const FVector4 A = Authored.Scene.Meshes[0].Vertices[Index].Tangent;
			const FVector4 D = Derived.Scene.Meshes[0].Vertices[Index].Tangent;
			INFO("vertex " << Index << " authored W " << A.W << " derived W " << D.W);
			REQUIRE(IsNearlyEqual(A, D, 1.0e-5f));
		}
	}
}

TEST_CASE("A derived tangent follows the texture's U direction", "[Asset][Gltf][Tangent]")
{
	// A frame that is merely orthogonal is not enough: it has to line up with the UVs, or a normal map is
	// applied rotated and the lighting leans the wrong way across the whole surface.
	const std::string Json = MakeTangentGltf(false);
	const FGltfImportResult Result = Import(Json.c_str());
	REQUIRE(Result.bSucceeded);

	const FMeshData& Mesh = Result.Scene.Meshes[0];
	REQUIRE(Mesh.Vertices.size() == 3);

	SECTION("The tangent runs along the axis U increases on")
	{
		// The fixture puts U along +X: vertex 1 sits at +X with U at 1 while the other two have U at 0.
		for (const FMeshVertex& Vertex : Mesh.Vertices)
		{
			const FVector3 Direction{ Vertex.Tangent.X, Vertex.Tangent.Y, Vertex.Tangent.Z };
			REQUIRE(IsNearlyEqual(Direction, FVector3{ 1.0f, 0.0f, 0.0f }, 1.0e-5f));
		}
	}

	SECTION("The reconstructed bitangent runs along the axis V increases on")
	{
		// V runs along +Y in the fixture. Reconstructed exactly as the shader will, so the sign is checked
		// the way it is going to be consumed rather than as an isolated number.
		for (const FMeshVertex& Vertex : Mesh.Vertices)
		{
			const FVector3 Tangent{ Vertex.Tangent.X, Vertex.Tangent.Y, Vertex.Tangent.Z };
			const FVector3 Bitangent = Cross(Vertex.Normal, Tangent) * Vertex.Tangent.W;
			REQUIRE(IsNearlyEqual(Bitangent, FVector3{ 0.0f, 1.0f, 0.0f }, 1.0e-5f));
		}
	}
}

TEST_CASE("Every vertex gets a usable tangent frame", "[Asset][Gltf][Tangent]")
{
	// The invariants shading depends on, asserted on inputs that have no tangent to derive one from. A zero
	// tangent collapses the frame and makes the mapped normal garbage rather than merely rotated, so the
	// fallback has to produce something valid even when the UVs say nothing.
	auto CheckFrame = [](const FGltfImportResult& Result)
	{
		REQUIRE(Result.bSucceeded);
		REQUIRE_FALSE(Result.Scene.Meshes.empty());

		for (const FMeshData& Mesh : Result.Scene.Meshes)
		{
			for (const FMeshVertex& Vertex : Mesh.Vertices)
			{
				const FVector3 Tangent{ Vertex.Tangent.X, Vertex.Tangent.Y, Vertex.Tangent.Z };

				REQUIRE(Tangent.Length() == Approx(1.0f).margin(1.0e-3f));
				// Perpendicular to the normal, which Gram-Schmidt is there to guarantee. A frame that is not
				// orthogonal skews the mapped normal by an amount that varies across the surface.
				REQUIRE(Dot(Tangent, Vertex.Normal) == Approx(0.0f).margin(1.0e-3f));
				// Exactly one of the two, since the shader multiplies by it rather than testing it.
				REQUIRE((Vertex.Tangent.W == Approx(1.0f) || Vertex.Tangent.W == Approx(-1.0f)));
			}
		}
	};

	SECTION("With no texture coordinates to derive from")
	{
		// No TEXCOORD_0 at all, so every triangle is degenerate in UV space and the arbitrary fallback runs.
		CheckFrame(Import(NoNormalsGltf));
	}

	SECTION("With texture coordinates present")
	{
		CheckFrame(Import(TriangleGltf));
	}
}

TEST_CASE("Every material channel is imported, not just base colour", "[Asset][Gltf][Material]")
{
	// The renderer shades with Blinn-Phong and reads only base colour, but the import is the expensive
	// one-off step: a map left behind here can only be recovered by importing the scene again. These cases
	// pin the whole material down so switching to PBR needs no importer change.
	const std::string Json = MakeFullMaterialGltf();
	const FGltfImportResult Result = Import(Json.c_str());

	INFO("importer message: " << Result.Message);
	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Scene.Materials.size() == 1);
	REQUIRE(Result.Scene.Images.size() == 5);

	const FMaterialData& Material = Result.Scene.Materials[0];

	SECTION("Every texture slot resolves to its own image")
	{
		// Distinct indices, so a slot cannot be quietly reading the one next to it.
		REQUIRE(Material.BaseColorImage == 0);
		REQUIRE(Material.MetallicRoughnessImage == 1);
		REQUIRE(Material.NormalImage == 2);
		REQUIRE(Material.EmissiveImage == 3);
		REQUIRE(Material.OcclusionImage == 4);
	}

	SECTION("Factors are read from the slots that carry them")
	{
		REQUIRE(IsNearlyEqual(Material.BaseColorFactor, FVector4{ 0.1f, 0.2f, 0.3f, 1.0f }, 1.0e-3f));
		REQUIRE(Material.MetallicFactor == Approx(0.25f));
		REQUIRE(Material.RoughnessFactor == Approx(0.75f));
		REQUIRE(IsNearlyEqual(Material.EmissiveFactor, FVector3{ 1.0f, 0.5f, 0.25f }, 1.0e-3f));

		// These two live on the texture reference rather than on the material, which is easy to read from
		// the wrong place.
		REQUIRE(Material.NormalScale == Approx(0.5f));
		REQUIRE(Material.OcclusionStrength == Approx(0.4f));
	}

	SECTION("Colour maps are decoded as sRGB and measurement maps as linear")
	{
		// The property the rest of the pipeline depends on. Nothing in a PNG says whether its values are
		// encoded, so only the slot referencing it can decide, and reading linear data through an sRGB
		// format makes the hardware decode it a second time: normals bend and roughness skews.
		REQUIRE(Result.Scene.Images[0].IsSrgb()); // base colour
		REQUIRE(Result.Scene.Images[3].IsSrgb()); // emissive

		REQUIRE_FALSE(Result.Scene.Images[1].IsSrgb()); // metallic-roughness
		REQUIRE_FALSE(Result.Scene.Images[2].IsSrgb()); // normal
		REQUIRE_FALSE(Result.Scene.Images[4].IsSrgb()); // occlusion
	}
}

TEST_CASE("A material that declares nothing gets the glTF defaults", "[Asset][Gltf][Material]")
{
	// TriangleGltf sets only a base colour factor, so every other channel falls back.
	const FGltfImportResult Result = Import(TriangleGltf);
	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Scene.Materials.size() == 1);

	const FMaterialData& Material = Result.Scene.Materials[0];

	SECTION("Absent maps are marked absent rather than pointing at image zero")
	{
		// Zero is a valid image index, so a default of 0 would make every untextured material sample
		// whichever image happened to be first.
		REQUIRE(Material.MetallicRoughnessImage == FMaterialData::NoImage);
		REQUIRE(Material.NormalImage == FMaterialData::NoImage);
		REQUIRE(Material.EmissiveImage == FMaterialData::NoImage);
		REQUIRE(Material.OcclusionImage == FMaterialData::NoImage);
	}

	SECTION("Factors follow the specification, not engine-flavoured defaults")
	{
		// glTF says both are 1, which makes an undeclared material a fully rough metal. Surprising, but
		// changing it here would render such a material differently from every other glTF viewer.
		REQUIRE(Material.MetallicFactor == Approx(1.0f));
		REQUIRE(Material.RoughnessFactor == Approx(1.0f));

		// Black, so a material that declares no emission does not glow.
		REQUIRE(IsNearlyEqual(Material.EmissiveFactor, FVector3{ 0.0f, 0.0f, 0.0f }));

		REQUIRE(Material.NormalScale == Approx(1.0f));
		REQUIRE(Material.OcclusionStrength == Approx(1.0f));
	}
}

TEST_CASE("Linear formats are recovered from their sRGB counterparts", "[Asset][Material]")
{
	// Used to correct a DDS whose file declares sRGB in a slot that must be linear, which exporters do
	// produce for normal maps.
	SECTION("Encoded formats map to their linear pair")
	{
		REQUIRE(ToLinearFormat(EPixelFormat::Rgba8Srgb) == EPixelFormat::Rgba8Unorm);
		REQUIRE(ToLinearFormat(EPixelFormat::Bgra8Srgb) == EPixelFormat::Bgra8Unorm);
		REQUIRE(ToLinearFormat(EPixelFormat::Bc1Srgb) == EPixelFormat::Bc1Unorm);
		REQUIRE(ToLinearFormat(EPixelFormat::Bc3Srgb) == EPixelFormat::Bc3Unorm);
		REQUIRE(ToLinearFormat(EPixelFormat::Bc7Srgb) == EPixelFormat::Bc7Unorm);
	}

	SECTION("Formats that are already linear are returned unchanged")
	{
		// Including the ones with no sRGB counterpart at all, which must not fall through to something else.
		REQUIRE(ToLinearFormat(EPixelFormat::Rgba8Unorm) == EPixelFormat::Rgba8Unorm);
		REQUIRE(ToLinearFormat(EPixelFormat::Bc5Snorm) == EPixelFormat::Bc5Snorm);
		REQUIRE(ToLinearFormat(EPixelFormat::Bc6HUfloat) == EPixelFormat::Bc6HUfloat);
		REQUIRE(ToLinearFormat(EPixelFormat::Unknown) == EPixelFormat::Unknown);
	}

	SECTION("Every sRGB format has a mapping")
	{
		// A new sRGB format added upstream without an entry here would silently stay encoded in a linear
		// slot, so the mapping is required to be total rather than best effort.
		constexpr EPixelFormat AllFormats[] = {
			EPixelFormat::Rgba8Unorm, EPixelFormat::Rgba8Srgb, EPixelFormat::Bgra8Unorm, EPixelFormat::Bgra8Srgb,
			EPixelFormat::Bc1Unorm,   EPixelFormat::Bc1Srgb,   EPixelFormat::Bc2Unorm,   EPixelFormat::Bc2Srgb,
			EPixelFormat::Bc3Unorm,   EPixelFormat::Bc3Srgb,   EPixelFormat::Bc4Unorm,   EPixelFormat::Bc4Snorm,
			EPixelFormat::Bc5Unorm,   EPixelFormat::Bc5Snorm,  EPixelFormat::Bc6HUfloat, EPixelFormat::Bc6HSfloat,
			EPixelFormat::Bc7Unorm,   EPixelFormat::Bc7Srgb,
		};

		for (const EPixelFormat Format : AllFormats)
		{
			REQUIRE_FALSE(IsSrgbFormat(ToLinearFormat(Format)));
		}
	}
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

TEST_CASE("The reported error explains the real failure", "[Asset][Gltf]")
{
	// A .gltf that fails to parse is retried as a .glb, which then fails with a container level complaint
	// ("Invalid magic" for text read as binary). Reporting that instead of the original hides the cause
	// completely, and cost real time to diagnose on the Bistro scene.
	const std::filesystem::path Path = std::filesystem::temp_directory_path() / "lime-broken-fixture.gltf";

	{
		std::ofstream File(Path, std::ios::binary);
		REQUIRE(File.is_open());
		// Valid JSON but not a glTF document: the required "asset" property is absent, so the failure comes
		// from glTF validation rather than from the JSON parser.
		File << R"({ "hello": "world" })";
	}

	const FGltfImportResult Result = FGltfImporter::LoadFromFile(Path);
	std::error_code Error;
	std::filesystem::remove(Path, Error);

	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE_FALSE(Result.Message.empty());
	REQUIRE(Result.Message.find("Invalid magic") == std::string::npos);
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
