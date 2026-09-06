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
		// Tangent frame for normal mapping. XYZ runs along increasing U; W is +1 or -1 and says which way
		// the bitangent goes, reconstructed in the shader as cross(Normal, Tangent.xyz) * Tangent.W.
		//
		// Storing the sign rather than the bitangent itself costs one float instead of three, and the
		// bitangent is fully determined by it: being perpendicular to both the normal and the tangent
		// leaves only the direction in doubt. The sign is not redundant, because a mirrored UV island
		// flips the bitangent while the normal and tangent stay put, and glTF stores TANGENT this way for
		// the same reason.
		FVector4 Tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
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

	// The full glTF metallic-roughness material, imported whether or not the current renderer reads it.
	//
	// Every channel is carried because the import is the expensive, one-off part: re-importing a scene to
	// pick up a map that was in the file all along is far worse than uploading one that nothing samples
	// yet. Blinn-Phong reads only base colour, which costs it nothing — an unread slot is an index the
	// shading never looks at.
	//
	// Factors follow the glTF defaults rather than engine-flavoured ones, so a material that omits a field
	// behaves as the specification says. That matters most for the pair with a non-obvious default:
	// metallic and roughness are both 1, meaning a material that declares neither is a fully rough metal.
	//
	// A texture index of -1 means the map is absent, and the factor alone applies. That is why the factors
	// are multiplicative against a neutral fallback texture: the shading needs no branch and the pipeline
	// no untextured variant.
	struct FMaterialData
	{
		std::string Name;

		// Index into FGltfSceneData::Images, or -1 when the map is absent.
		static constexpr int32 NoImage = -1;

		FVector4 BaseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
		int32 BaseColorImage = NoImage;

		// glTF packs both into one texture: roughness in G, metallic in B. One index, two factors.
		float MetallicFactor = 1.0f;
		float RoughnessFactor = 1.0f;
		int32 MetallicRoughnessImage = NoImage;

		// Tangent space normals. Scale attenuates the perturbation; 1 applies it as authored.
		int32 NormalImage = NoImage;
		float NormalScale = 1.0f;

		// Black by default, so a material that declares no emission does not glow. The texture is
		// multiplied by the factor, which is what makes a bare texture with no factor invisible.
		FVector3 EmissiveFactor{ 0.0f, 0.0f, 0.0f };
		int32 EmissiveImage = NoImage;

		// Ambient occlusion in R. Frequently the same image as MetallicRoughnessImage: the ORM convention
		// packs occlusion, roughness and metallic into one texture's R, G and B, and glTF expresses that as
		// two slots pointing at one texture. Both indices are kept as the file gives them rather than
		// deduplicated, so a consumer can tell an ORM pack from two separate maps.
		int32 OcclusionImage = NoImage;
		float OcclusionStrength = 1.0f;

		// Alpha below this is discarded when the glTF alpha mode is MASK. Zero means no cutout.
		float AlphaCutoff = 0.0f;
		bool bDoubleSided = false;
	};

	// How an image's values are encoded, which decides the format it is uploaded in.
	//
	// Not a property of the image file but of the slot that references it: the same PNG is sRGB as a base
	// colour map and linear as a normal map, and nothing in the file says which. glTF fixes this per slot,
	// so the importer resolves it from the materials before decoding.
	//
	// Getting it wrong is not subtle in one direction and invisible in the other. Linear data read through
	// an sRGB format is decoded a second time by the hardware, which bends normals and skews roughness;
	// colour data read as linear merely looks washed out.
	enum class EImageColorSpace : uint8
	{
		// Referenced by no material, or by none that decides. Treated as sRGB, since an unreferenced image
		// is most likely colour.
		Unknown,
		// Colour: base colour and emissive.
		Srgb,
		// Measurements: normal, metallic-roughness and occlusion.
		Linear,
	};

	// Pixel formats an image can arrive in.
	//
	// Declared here rather than reusing nvrhi::Format because this module must not depend on the RHI: that
	// is what lets the loaders be unit tested without a device. The mapping to nvrhi lives at the upload
	// site, where the two meet.
	//
	// Block compressed entries are carried through to the GPU as-is. Decompressing them on the CPU would
	// throw away the memory and bandwidth saving they exist for, and every backend this engine targets
	// supports them natively.
	//
	// Named in the engine's convention rather than DXGI's, with the DXGI equivalent in a comment where the
	// two differ in more than case. The mapping tables in DdsLoader.cpp and SceneGpuResources.cpp are the
	// places to check when adding one.
	enum class EPixelFormat : uint8
	{
		Unknown,

		// Uncompressed, 4 bytes per pixel. What the stb path produces after widening.
		Rgba8Unorm,
		Rgba8Srgb,
		Bgra8Unorm,
		Bgra8Srgb,

		// Block compressed, 4x4 blocks. BC1 and BC4 pack a block into 8 bytes, the rest into 16.
		Bc1Unorm,
		Bc1Srgb,
		Bc2Unorm,
		Bc2Srgb,
		Bc3Unorm,
		Bc3Srgb,
		Bc4Unorm,
		Bc4Snorm,
		Bc5Unorm,
		Bc5Snorm,
		// DXGI calls these BC6H_UF16 and BC6H_SF16.
		Bc6HUfloat,
		Bc6HSfloat,
		Bc7Unorm,
		Bc7Srgb,
	};

	// True for the 4x4 block compressed formats, whose row pitch is measured in blocks rather than pixels.
	bool IsBlockCompressed(EPixelFormat Format);

	// Bytes per 4x4 block for a compressed format, or bytes per pixel for an uncompressed one.
	uint32 GetFormatBlockSize(EPixelFormat Format);

	// True when the format carries an sRGB transfer function, which the shader must not decode a second
	// time.
	bool IsSrgbFormat(EPixelFormat Format);

	// The linear counterpart of a format, or the format itself when it already is one.
	//
	// Used to correct an image whose file declares sRGB but whose slot requires linear. A DDS carries an
	// explicit DXGI format and is normally trusted, but exporters do write sRGB normal maps, and that
	// combination is always a mistake rather than an authoring choice.
	EPixelFormat ToLinearFormat(EPixelFormat Format);

	const char* ToString(EPixelFormat Format);

	// One mip level's slice of an image's byte array.
	//
	// Offsets rather than separate allocations, so an image is one contiguous buffer however many levels
	// it has. That is also the shape writeTexture wants.
	struct FImageMipLevel
	{
		uint32 Width = 0;
		uint32 Height = 0;
		// Byte offset into FImageData::Pixels.
		uint64 Offset = 0;
		uint64 Size = 0;
		// Bytes per row of pixels, or per row of blocks for a compressed format.
		uint32 RowPitch = 0;
	};

	// An image's raw bytes plus the layout needed to interpret them.
	//
	// The stb path produces a single RGBA8 level: glTF images may be 1 to 4 channels, and widening them at
	// load keeps the upload path and the shader free of per-image special cases. The DDS path produces the
	// levels the file already contains, in whatever block compressed format it was authored in.
	struct FImageData
	{
		std::string Name;
		uint32 Width = 0;
		uint32 Height = 0;
		EPixelFormat Format = EPixelFormat::Rgba8Srgb;
		// Raw bytes of every mip level, concatenated. Interpreted through Mips.
		std::vector<uint8> Pixels;
		// Always at least one entry for a valid image. Level 0 covers the full size.
		std::vector<FImageMipLevel> Mips;

		bool IsValid() const { return Width > 0 && Height > 0 && !Pixels.empty() && !Mips.empty(); }
		bool IsSrgb() const { return IsSrgbFormat(Format); }

		// Fills Mips with the single level an uncompressed image has. Used by the stb path, which decodes
		// one level and knows nothing about mips.
		void SetSingleLevel(uint32 InWidth, uint32 InHeight, EPixelFormat InFormat, std::vector<uint8> InPixels);
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
