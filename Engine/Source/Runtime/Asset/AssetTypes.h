// CPU side asset data produced by the importers.
//
// Deliberately free of any GPU handles. Keeping the import result as plain data means glTF parsing,
// attribute extraction and hierarchy flattening can all be verified in unit tests on a machine with
// no graphics device, which matches how the rest of the test suite works. Uploading is a separate
// step owned by the scene module.

#pragma once

#include "Core/CoreTypes.h"
#include "Core/Math/Quaternion.h"
#include "Core/Math/Vector.h"

#include <string>
#include <vector>

namespace Lime
{
	// Matches FStaticMeshVertex in the renderer. Duplicated rather than shared because this module must
	// not depend on the renderer: the two are checked against each other by a static_assert where they
	// meet, during upload.
	struct FMeshVertex
	{
		FVector3 Position;
		FVector3 Normal;
		FVector2 TexCoord;
	};

	// Axis aligned bounds, used to frame the camera on a freshly loaded model.
	struct FBoundingBox
	{
		FVector3 Min{ 0.0f, 0.0f, 0.0f };
		FVector3 Max{ 0.0f, 0.0f, 0.0f };
		bool bValid = false;

		void Include(const FVector3& Point);
		FVector3 GetCenter() const;
		FVector3 GetExtents() const;
		// Longest edge, which is what a camera distance is derived from.
		float GetLongestEdge() const;
	};

	// One drawable range of a mesh. glTF calls these primitives; a mesh holds several when parts of it
	// use different materials, and each becomes its own draw.
	struct FMeshSection
	{
		uint32 FirstIndex = 0;
		uint32 IndexCount = 0;
		// Index into FGltfSceneData::Materials, or -1 when the primitive had no material and should be
		// drawn with the default one.
		int32 MaterialIndex = -1;
	};

	struct FMeshData
	{
		std::string Name;
		// Vertices and indices of every section are concatenated, so a mesh needs one buffer pair no
		// matter how many materials it uses.
		std::vector<FMeshVertex> Vertices;
		std::vector<uint32> Indices;
		std::vector<FMeshSection> Sections;
		FBoundingBox Bounds;

		uint32 GetTriangleCount() const { return static_cast<uint32>(Indices.size() / 3); }
	};

	struct FMaterialData
	{
		std::string Name;
		FVector4 BaseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
		// Index into FGltfSceneData::Images, or -1 when the material is untextured.
		int32 BaseColorImage = -1;
		// Alpha below this is discarded when the glTF alpha mode is MASK. Zero means no cutout.
		float AlphaCutoff = 0.0f;
		bool bDoubleSided = false;
	};

	// Decoded pixels, always expanded to 4 channels. glTF images may be 1 to 4 channels, and widening
	// them here keeps the upload path and the shader free of per-image special cases; the cost is
	// paid once at load rather than per draw.
	struct FImageData
	{
		std::string Name;
		uint32 Width = 0;
		uint32 Height = 0;
		std::vector<uint8> Pixels;
		// True when the source was authored in sRGB, which base colour textures are by definition.
		bool bIsSrgb = true;

		bool IsValid() const { return Width > 0 && Height > 0 && !Pixels.empty(); }
	};

	// A node of the glTF hierarchy. Children are indices into the flat node array rather than pointers,
	// so the whole structure stays copyable and has no ownership questions.
	struct FSceneNodeData
	{
		std::string Name;
		FVector3 Translation{ 0.0f, 0.0f, 0.0f };
		FQuat Rotation = FQuat::Identity();
		FVector3 Scale{ 1.0f, 1.0f, 1.0f };
		// Index into FGltfSceneData::Meshes, or -1 for a node that only groups children.
		int32 MeshIndex = -1;
		std::vector<uint32> Children;
	};

	struct FGltfSceneData
	{
		std::string SourcePath;
		std::vector<FMeshData> Meshes;
		std::vector<FMaterialData> Materials;
		std::vector<FImageData> Images;
		std::vector<FSceneNodeData> Nodes;
		std::vector<uint32> RootNodes;

		uint32 GetTotalTriangleCount() const;
		// Bounds of every mesh in local space, ignoring node transforms. Enough to pick a sensible
		// camera distance for the common case of a model authored around the origin.
		FBoundingBox GetMeshBounds() const;

		bool IsEmpty() const { return Nodes.empty(); }
	};
} // namespace Lime
