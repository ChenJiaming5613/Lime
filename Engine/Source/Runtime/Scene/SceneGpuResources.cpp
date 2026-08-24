#include "Scene/SceneGpuResources.h"

#include "Core/Logging/LogManager.h"

#include "Scene/Scene.h"

#include <array>

namespace Lime
{
	namespace
	{
		// Maps the asset module's format to the RHI's.
		//
		// This mapping lives here, at the boundary, because LimeAsset must not depend on the RHI: that is what
		// lets the loaders be unit tested without a device. An explicit switch rather than a table indexed by
		// the enum value, so inserting a format upstream cannot silently shift every entry.
		nvrhi::Format ToNvrhiFormat(EPixelFormat Format)
		{
			switch (Format)
			{
				// The sRGB variants matter: sampling an already-encoded texture through a linear format washes
				// the image out, because the hardware then skips the conversion the shader assumes happened.
				case EPixelFormat::Rgba8Unorm:
					return nvrhi::Format::RGBA8_UNORM;
				case EPixelFormat::Rgba8Srgb:
					return nvrhi::Format::SRGBA8_UNORM;
				case EPixelFormat::Bgra8Unorm:
					return nvrhi::Format::BGRA8_UNORM;
				case EPixelFormat::Bgra8Srgb:
					return nvrhi::Format::SBGRA8_UNORM;

				case EPixelFormat::Bc1Unorm:
					return nvrhi::Format::BC1_UNORM;
				case EPixelFormat::Bc1Srgb:
					return nvrhi::Format::BC1_UNORM_SRGB;
				case EPixelFormat::Bc2Unorm:
					return nvrhi::Format::BC2_UNORM;
				case EPixelFormat::Bc2Srgb:
					return nvrhi::Format::BC2_UNORM_SRGB;
				case EPixelFormat::Bc3Unorm:
					return nvrhi::Format::BC3_UNORM;
				case EPixelFormat::Bc3Srgb:
					return nvrhi::Format::BC3_UNORM_SRGB;
				case EPixelFormat::Bc4Unorm:
					return nvrhi::Format::BC4_UNORM;
				case EPixelFormat::Bc4Snorm:
					return nvrhi::Format::BC4_SNORM;
				case EPixelFormat::Bc5Unorm:
					return nvrhi::Format::BC5_UNORM;
				case EPixelFormat::Bc5Snorm:
					return nvrhi::Format::BC5_SNORM;
				case EPixelFormat::Bc6HUfloat:
					return nvrhi::Format::BC6H_UFLOAT;
				case EPixelFormat::Bc6HSfloat:
					return nvrhi::Format::BC6H_SFLOAT;
				case EPixelFormat::Bc7Unorm:
					return nvrhi::Format::BC7_UNORM;
				case EPixelFormat::Bc7Srgb:
					return nvrhi::Format::BC7_UNORM_SRGB;

				default:
					return nvrhi::Format::UNKNOWN;
			}
		}
	} // namespace

	FSceneGpuResources::~FSceneGpuResources()
	{
		Release();
		// Released only here, not in Release(): these do not depend on the scene, so recreating them on every
		// scene change would be wasted work.
		FallbackTexture = nullptr;
		Sampler = nullptr;
	}

	bool FSceneGpuResources::CreateSampler(nvrhi::IDevice* Device)
	{
		if (Sampler != nullptr)
		{
			return true;
		}

		// Wrap rather than clamp: glTF texture coordinates routinely fall outside [0, 1] and clamping would
		// smear the edge pixels across the surface.
		Sampler = Device->createSampler(
		    nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Wrap).setMaxAnisotropy(8.0f));

		if (Sampler == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_SCENE, "createSampler failed for the scene sampler");
			return false;
		}
		return true;
	}

	bool FSceneGpuResources::CreateFallbackTexture(nvrhi::IDevice* Device, nvrhi::ICommandList* CommandList)
	{
		if (FallbackTexture != nullptr)
		{
			return true;
		}

		// A 1x1 white texture stands in for an absent base colour map. Multiplying by white is a no-op, so
		// the shader needs no branch and the pipeline needs no untextured variant.
		const nvrhi::TextureDesc Desc = nvrhi::TextureDesc()
		                                    .setDimension(nvrhi::TextureDimension::Texture2D)
		                                    .setWidth(1)
		                                    .setHeight(1)
		                                    .setFormat(nvrhi::Format::RGBA8_UNORM)
		                                    .setInitialState(nvrhi::ResourceStates::ShaderResource)
		                                    .setKeepInitialState(true)
		                                    .setDebugName("SceneWhiteTexture");

		FallbackTexture = Device->createTexture(Desc);
		if (FallbackTexture == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_SCENE, "createTexture failed for the fallback white texture");
			return false;
		}

		const std::array<uint8, 4> White{ 255, 255, 255, 255 };
		CommandList->writeTexture(FallbackTexture, 0, 0, White.data(), White.size());
		return true;
	}

	bool FSceneGpuResources::UploadMeshes(nvrhi::IDevice* Device, nvrhi::ICommandList* CommandList, const FScene& Scene)
	{
		const std::vector<FMeshData>& SourceMeshes = Scene.GetMeshes();
		Meshes.clear();
		Meshes.resize(SourceMeshes.size());

		for (SizeType Index = 0; Index < SourceMeshes.size(); ++Index)
		{
			const FMeshData& Source = SourceMeshes[Index];
			if (Source.Vertices.empty() || Source.Indices.empty())
			{
				// A mesh whose primitives were all skipped, for example a point cloud. Left invalid so drawing
				// passes over it rather than issuing an empty draw.
				continue;
			}

			FMeshGpuData& Target = Meshes[Index];

			const size_t VertexBytes = Source.Vertices.size() * sizeof(FMeshVertex);
			Target.VertexBuffer = Device->createBuffer(nvrhi::BufferDesc()
			                                               .setByteSize(VertexBytes)
			                                               .setIsVertexBuffer(true)
			                                               .setInitialState(nvrhi::ResourceStates::VertexBuffer)
			                                               .setKeepInitialState(true)
			                                               .setDebugName("SceneMeshVertices"));

			const size_t IndexBytes = Source.Indices.size() * sizeof(uint32);
			Target.IndexBuffer = Device->createBuffer(nvrhi::BufferDesc()
			                                              .setByteSize(IndexBytes)
			                                              .setIsIndexBuffer(true)
			                                              .setInitialState(nvrhi::ResourceStates::IndexBuffer)
			                                              .setKeepInitialState(true)
			                                              .setDebugName("SceneMeshIndices"));

			if (Target.VertexBuffer == nullptr || Target.IndexBuffer == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_SCENE, "createBuffer failed for mesh '{}'", Source.Name);
				return false;
			}

			// Written directly from the imported array; the static_asserts above guarantee the layout matches
			// what the vertex shader expects.
			CommandList->writeBuffer(Target.VertexBuffer, Source.Vertices.data(), VertexBytes);
			CommandList->writeBuffer(Target.IndexBuffer, Source.Indices.data(), IndexBytes);
			Target.IndexCount = static_cast<uint32>(Source.Indices.size());
		}

		return true;
	}

	bool FSceneGpuResources::UploadTextures(nvrhi::IDevice* Device, nvrhi::ICommandList* CommandList, const FScene& Scene)
	{
		const std::vector<FImageData>& Images = Scene.GetImages();
		Textures.clear();
		Textures.resize(Images.size());

		for (SizeType Index = 0; Index < Images.size(); ++Index)
		{
			const FImageData& Image = Images[Index];
			if (!Image.IsValid())
			{
				continue;
			}

			const nvrhi::Format Format = ToNvrhiFormat(Image.Format);
			if (Format == nvrhi::Format::UNKNOWN)
			{
				// Reached only if a loader produced a format this mapping does not cover, which is a gap in the
				// engine rather than a problem with the asset. Skipped so the material falls back to white.
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_SCENE, "No RHI format for {} on image '{}'; it will use the fallback texture",
				                 ToString(Image.Format), Image.Name);
				continue;
			}

			const nvrhi::TextureDesc Desc = nvrhi::TextureDesc()
			                                    .setDimension(nvrhi::TextureDimension::Texture2D)
			                                    .setWidth(Image.Width)
			                                    .setHeight(Image.Height)
			                                    // Mip count comes from the image: a DDS carries its own chain, while a
			                                    // decoded PNG has exactly one level.
			                                    .setMipLevels(static_cast<uint32>(Image.Mips.size()))
			                                    .setFormat(Format)
			                                    .setInitialState(nvrhi::ResourceStates::ShaderResource)
			                                    .setKeepInitialState(true)
			                                    .setDebugName("SceneTexture");

			Textures[Index] = Device->createTexture(Desc);
			if (Textures[Index] == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_SCENE, "createTexture failed for a scene texture ({}x{} {})", Image.Width, Image.Height,
				               ToString(Image.Format));
				return false;
			}

			// Each level is written separately with its own row pitch. For a block compressed format the pitch
			// is a row of 4x4 blocks, not of pixels, which the loader has already worked out.
			for (SizeType Level = 0; Level < Image.Mips.size(); ++Level)
			{
				const FImageMipLevel& Mip = Image.Mips[Level];
				CommandList->writeTexture(Textures[Index], 0, static_cast<uint32>(Level), Image.Pixels.data() + Mip.Offset, Mip.RowPitch);
			}
		}

		// Resolved once here so that drawing needs no material to image to texture lookup chain, and so that
		// every material is guaranteed a usable texture.
		const std::vector<FMaterialData>& Materials = Scene.GetMaterials();
		MaterialTextures.assign(Materials.size(), FallbackTexture.Get());

		for (SizeType Index = 0; Index < Materials.size(); ++Index)
		{
			const int32 ImageIndex = Materials[Index].BaseColorImage;
			if (ImageIndex >= 0 && static_cast<SizeType>(ImageIndex) < Textures.size() &&
			    Textures[static_cast<SizeType>(ImageIndex)] != nullptr)
			{
				MaterialTextures[Index] = Textures[static_cast<SizeType>(ImageIndex)].Get();
			}
		}

		return true;
	}

	bool FSceneGpuResources::EnsureUploaded(nvrhi::IDevice* Device, nvrhi::ICommandList* CommandList, const FScene& Scene)
	{
		if (Device == nullptr || CommandList == nullptr)
		{
			return false;
		}

		// The common path: nothing changed since the last upload, so this costs one comparison per frame.
		if (bUploaded && UploadedRevision == Scene.GetAssetRevision())
		{
			return true;
		}

		Release();

		if (!CreateSampler(Device) || !CreateFallbackTexture(Device, CommandList))
		{
			return false;
		}

		if (!UploadMeshes(Device, CommandList, Scene) || !UploadTextures(Device, CommandList, Scene))
		{
			// Partial state is worse than none: it would draw some meshes with buffers belonging to a scene
			// that no longer exists.
			Release();
			return false;
		}

		UploadedRevision = Scene.GetAssetRevision();
		bUploaded = true;

		uint32 UploadedMeshes = 0;
		for (const FMeshGpuData& Mesh : Meshes)
		{
			if (Mesh.IsValid())
			{
				++UploadedMeshes;
			}
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_SCENE, "Uploaded {} mesh(es) and {} texture(s) to the GPU", UploadedMeshes, Textures.size());
		return true;
	}

	void FSceneGpuResources::Release()
	{
		Meshes.clear();
		Textures.clear();
		MaterialTextures.clear();
		// The sampler and the white texture are kept: they do not depend on the scene, and recreating them
		// on every scene change would be wasted work.
		bUploaded = false;
		UploadedRevision = 0;
	}

	const FMeshGpuData* FSceneGpuResources::GetMesh(uint32 MeshIndex) const
	{
		if (MeshIndex >= Meshes.size())
		{
			return nullptr;
		}

		const FMeshGpuData& Mesh = Meshes[MeshIndex];
		return Mesh.IsValid() ? &Mesh : nullptr;
	}

	nvrhi::ITexture* FSceneGpuResources::GetBaseColorTexture(int32 MaterialIndex) const
	{
		if (MaterialIndex < 0 || static_cast<SizeType>(MaterialIndex) >= MaterialTextures.size())
		{
			// A primitive with no material still has to draw, so it gets white rather than nothing.
			return FallbackTexture.Get();
		}
		return MaterialTextures[static_cast<SizeType>(MaterialIndex)];
	}
} // namespace Lime
