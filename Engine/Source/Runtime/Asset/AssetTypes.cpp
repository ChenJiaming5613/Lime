#include "Asset/AssetTypes.h"

#include <algorithm>

namespace Lime
{
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
