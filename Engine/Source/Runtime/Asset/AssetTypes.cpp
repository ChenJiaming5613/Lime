#include "Asset/AssetTypes.h"

#include <algorithm>

namespace Lime
{
	bool IsBlockCompressed(EPixelFormat Format)
	{
		switch (Format)
		{
			case EPixelFormat::Bc1Unorm:
			case EPixelFormat::Bc1Srgb:
			case EPixelFormat::Bc2Unorm:
			case EPixelFormat::Bc2Srgb:
			case EPixelFormat::Bc3Unorm:
			case EPixelFormat::Bc3Srgb:
			case EPixelFormat::Bc4Unorm:
			case EPixelFormat::Bc4Snorm:
			case EPixelFormat::Bc5Unorm:
			case EPixelFormat::Bc5Snorm:
			case EPixelFormat::Bc6HUfloat:
			case EPixelFormat::Bc6HSfloat:
			case EPixelFormat::Bc7Unorm:
			case EPixelFormat::Bc7Srgb:
				return true;
			default:
				return false;
		}
	}

	uint32 GetFormatBlockSize(EPixelFormat Format)
	{
		switch (Format)
		{
			// BC1 and BC4 pack a 4x4 block into 8 bytes; every other block format uses 16.
			case EPixelFormat::Bc1Unorm:
			case EPixelFormat::Bc1Srgb:
			case EPixelFormat::Bc4Unorm:
			case EPixelFormat::Bc4Snorm:
				return 8;

			case EPixelFormat::Bc2Unorm:
			case EPixelFormat::Bc2Srgb:
			case EPixelFormat::Bc3Unorm:
			case EPixelFormat::Bc3Srgb:
			case EPixelFormat::Bc5Unorm:
			case EPixelFormat::Bc5Snorm:
			case EPixelFormat::Bc6HUfloat:
			case EPixelFormat::Bc6HSfloat:
			case EPixelFormat::Bc7Unorm:
			case EPixelFormat::Bc7Srgb:
				return 16;

			case EPixelFormat::Rgba8Unorm:
			case EPixelFormat::Rgba8Srgb:
			case EPixelFormat::Bgra8Unorm:
			case EPixelFormat::Bgra8Srgb:
				return 4;

			default:
				return 0;
		}
	}

	bool IsSrgbFormat(EPixelFormat Format)
	{
		switch (Format)
		{
			case EPixelFormat::Rgba8Srgb:
			case EPixelFormat::Bgra8Srgb:
			case EPixelFormat::Bc1Srgb:
			case EPixelFormat::Bc2Srgb:
			case EPixelFormat::Bc3Srgb:
			case EPixelFormat::Bc7Srgb:
				return true;
			default:
				return false;
		}
	}

	const char* ToString(EPixelFormat Format)
	{
		switch (Format)
		{
			case EPixelFormat::Rgba8Unorm:
				return "RGBA8_UNORM";
			case EPixelFormat::Rgba8Srgb:
				return "RGBA8_SRGB";
			case EPixelFormat::Bgra8Unorm:
				return "BGRA8_UNORM";
			case EPixelFormat::Bgra8Srgb:
				return "BGRA8_SRGB";
			case EPixelFormat::Bc1Unorm:
				return "BC1_UNORM";
			case EPixelFormat::Bc1Srgb:
				return "BC1_SRGB";
			case EPixelFormat::Bc2Unorm:
				return "BC2_UNORM";
			case EPixelFormat::Bc2Srgb:
				return "BC2_SRGB";
			case EPixelFormat::Bc3Unorm:
				return "BC3_UNORM";
			case EPixelFormat::Bc3Srgb:
				return "BC3_SRGB";
			case EPixelFormat::Bc4Unorm:
				return "BC4_UNORM";
			case EPixelFormat::Bc4Snorm:
				return "BC4_SNORM";
			case EPixelFormat::Bc5Unorm:
				return "BC5_UNORM";
			case EPixelFormat::Bc5Snorm:
				return "BC5_SNORM";
			case EPixelFormat::Bc6HUfloat:
				return "BC6H_UFLOAT";
			case EPixelFormat::Bc6HSfloat:
				return "BC6H_SFLOAT";
			case EPixelFormat::Bc7Unorm:
				return "BC7_UNORM";
			case EPixelFormat::Bc7Srgb:
				return "BC7_SRGB";
			default:
				return "Unknown";
		}
	}

	void FImageData::SetSingleLevel(uint32 InWidth, uint32 InHeight, EPixelFormat InFormat, std::vector<uint8> InPixels)
	{
		Width = InWidth;
		Height = InHeight;
		Format = InFormat;
		Pixels = std::move(InPixels);

		Mips.clear();
		if (Width == 0 || Height == 0 || Pixels.empty())
		{
			return;
		}

		FImageMipLevel Level;
		Level.Width = Width;
		Level.Height = Height;
		Level.Offset = 0;
		Level.Size = Pixels.size();
		Level.RowPitch = Width * GetFormatBlockSize(Format);
		Mips.push_back(Level);
	}

	void FBoundingBox::Include(const FVector3& Point)
	{
		if (!bValid)
		{
			// The first point seeds both corners. Starting from a zero box instead would wrongly pull the
			// bounds towards the origin for a model that sits away from it.
			Min = Point;
			Max = Point;
			bValid = true;
			return;
		}

		Min.X = std::min(Min.X, Point.X);
		Min.Y = std::min(Min.Y, Point.Y);
		Min.Z = std::min(Min.Z, Point.Z);
		Max.X = std::max(Max.X, Point.X);
		Max.Y = std::max(Max.Y, Point.Y);
		Max.Z = std::max(Max.Z, Point.Z);
	}

	FVector3 FBoundingBox::GetCenter() const
	{
		return bValid ? (Min + Max) * 0.5f : FVector3::Zero();
	}

	FVector3 FBoundingBox::GetExtents() const
	{
		return bValid ? (Max - Min) * 0.5f : FVector3::Zero();
	}

	float FBoundingBox::GetLongestEdge() const
	{
		if (!bValid)
		{
			return 0.0f;
		}

		const FVector3 Size = Max - Min;
		return std::max({ Size.X, Size.Y, Size.Z });
	}

	uint32 FGltfSceneData::GetTotalTriangleCount() const
	{
		uint32 Total = 0;
		for (const FMeshData& Mesh : Meshes)
		{
			Total += Mesh.GetTriangleCount();
		}
		return Total;
	}

	FBoundingBox FGltfSceneData::GetMeshBounds() const
	{
		FBoundingBox Result;
		for (const FMeshData& Mesh : Meshes)
		{
			if (!Mesh.Bounds.bValid)
			{
				continue;
			}
			Result.Include(Mesh.Bounds.Min);
			Result.Include(Mesh.Bounds.Max);
		}
		return Result;
	}
} // namespace Lime
