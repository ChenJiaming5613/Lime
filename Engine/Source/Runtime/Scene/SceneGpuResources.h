// GPU resources for a scene's meshes and textures.
//
// Separated from FScene so that the scene itself stays free of any device dependency: it can be built
// and inspected without a GPU, and it survives a device reset that invalidates everything here.
//
// Uploads happen once per asset revision, not per frame. The revision counter on the scene is what makes
// that safe: a scene whose assets have not changed is recognised and skipped, so a static scene costs
// nothing after the first frame.

#pragma once

#include "Core/CoreTypes.h"
#include "RHI/RHITypes.h"

#include <vector>

namespace Lime
{
	class FScene;

	// One mesh's buffers. Vertices and indices of every section live in a single buffer pair, so a mesh
	// needs one binding no matter how many materials it uses.
	struct FMeshGpuData
	{
		nvrhi::BufferHandle VertexBuffer;
		nvrhi::BufferHandle IndexBuffer;
		uint32 IndexCount = 0;

		bool IsValid() const { return VertexBuffer != nullptr && IndexBuffer != nullptr && IndexCount > 0; }
	};

	// The material maps a draw can bind, in the order FMaterialData declares them.
	//
	// An enum rather than five accessors so a pass can loop over what it needs, and so adding a slot does
	// not change any signature. Count is the array size, which is what keeps the per material table a
	// flat vector rather than a map.
	enum class EMaterialTextureSlot : uint8
	{
		BaseColor = 0,
		MetallicRoughness,
		Normal,
		Emissive,
		Occlusion,

		Count
	};

	class FSceneGpuResources
	{
	public:
		FSceneGpuResources() = default;
		~FSceneGpuResources();

		LIME_NON_COPYABLE(FSceneGpuResources);
		LIME_NON_MOVABLE(FSceneGpuResources);

		// Uploads whatever the scene needs and returns true when resources are ready to draw with.
		//
		// Cheap to call every frame: it returns immediately when the scene's asset revision matches what was
		// last uploaded. The command list must be open, since texture uploads are recorded into it.
		bool EnsureUploaded(nvrhi::IDevice* Device, nvrhi::ICommandList* CommandList, const FScene& Scene);

		void Release();

		// Null when the index is out of range or the mesh had no drawable geometry.
		const FMeshGpuData* GetMesh(uint32 MeshIndex) const;

		// Texture for a material's base colour. Never null for a valid material index: a material without a
		// texture resolves to a 1x1 white one, which keeps the shader free of a branch and the pipeline free
		// of a second variant.
		nvrhi::ITexture* GetBaseColorTexture(int32 MaterialIndex) const;

		// Texture for any material slot, never null for the same reason.
		//
		// An absent map resolves to the slot's neutral value rather than to white for all of them: white is
		// only neutral where the factor multiplies it. A missing normal map has to read as the flat tangent
		// space normal, which is not white, and getting that wrong tilts every unmapped surface.
		nvrhi::ITexture* GetMaterialTexture(int32 MaterialIndex, EMaterialTextureSlot Slot) const;

		nvrhi::ISampler* GetSampler() const { return Sampler; }

		bool IsReady() const { return bUploaded; }

	private:
		bool CreateSampler(nvrhi::IDevice* Device);
		bool CreateFallbackTextures(nvrhi::IDevice* Device, nvrhi::ICommandList* CommandList);
		bool UploadMeshes(nvrhi::IDevice* Device, nvrhi::ICommandList* CommandList, const FScene& Scene);
		bool UploadTextures(nvrhi::IDevice* Device, nvrhi::ICommandList* CommandList, const FScene& Scene);
		// The neutral texture for a slot, used when the material declares no map for it.
		nvrhi::ITexture* GetFallbackTexture(EMaterialTextureSlot Slot) const;

		static constexpr SizeType SlotCount = static_cast<SizeType>(EMaterialTextureSlot::Count);

		std::vector<FMeshGpuData> Meshes;
		// Indexed by image index, parallel to the scene's image array. An entry is null when that image
		// failed to decode, and materials referring to it fall back to their slot's neutral texture.
		std::vector<nvrhi::TextureHandle> Textures;
		// Indexed by material index times SlotCount plus the slot, so drawing needs no lookup chain.
		// Flattened rather than a vector of arrays to keep one allocation for the whole table.
		std::vector<nvrhi::ITexture*> MaterialTextures;

		// 1x1 stand-ins, one per distinct neutral value rather than one per slot: white serves base colour,
		// emissive, occlusion and metallic-roughness, since in each the factor multiplies it. Only the
		// normal map needs its own, because a flat tangent space normal is not a neutral colour.
		nvrhi::TextureHandle WhiteTexture;
		nvrhi::TextureHandle FlatNormalTexture;
		nvrhi::SamplerHandle Sampler;

		// Revision the current upload corresponds to. Compared against the scene to decide whether anything
		// needs doing.
		uint32 UploadedRevision = 0;
		bool bUploaded = false;
	};
} // namespace Lime
