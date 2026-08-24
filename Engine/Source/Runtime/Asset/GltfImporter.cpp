#include "Asset/GltfImporter.h"

#include "Core/Logging/LogManager.h"

#include "Asset/DdsLoader.h"

// tinygltf pulls in stb and defines its implementation in the vendored tiny_gltf.cc, so this
// translation unit only needs the declarations.
#include <tiny_gltf.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace Lime
{
	namespace
	{
		// Reads one element of an accessor as floats, whatever the source component type is.
		//
		// glTF allows positions and normals to be float, and texture coordinates to additionally be
		// normalized bytes or shorts. Converting on read keeps a single vertex format downstream.
		bool ReadAccessorAsFloats(const tinygltf::Model& Model, const tinygltf::Accessor& Accessor, size_t ElementIndex, float* OutValues,
		                          size_t ComponentCount)
		{
			if (Accessor.bufferView < 0 || Accessor.bufferView >= static_cast<int>(Model.bufferViews.size()))
			{
				return false;
			}

			const tinygltf::BufferView& View = Model.bufferViews[Accessor.bufferView];
			if (View.buffer < 0 || View.buffer >= static_cast<int>(Model.buffers.size()))
			{
				return false;
			}

			const tinygltf::Buffer& Buffer = Model.buffers[View.buffer];
			const int32 ComponentSize = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(Accessor.componentType));
			const int32 TypeComponents = tinygltf::GetNumComponentsInType(static_cast<uint32_t>(Accessor.type));
			if (ComponentSize <= 0 || TypeComponents <= 0)
			{
				return false;
			}

			// A zero byteStride means tightly packed, which is the common case.
			const size_t Stride = View.byteStride > 0 ? View.byteStride : static_cast<size_t>(ComponentSize) * TypeComponents;
			const size_t Offset = View.byteOffset + Accessor.byteOffset + Stride * ElementIndex;
			if (Offset + static_cast<size_t>(ComponentSize) * TypeComponents > Buffer.data.size())
			{
				return false;
			}

			const uint8* Element = Buffer.data.data() + Offset;
			const size_t Count = std::min(ComponentCount, static_cast<size_t>(TypeComponents));

			for (size_t Index = 0; Index < Count; ++Index)
			{
				const uint8* Component = Element + static_cast<size_t>(ComponentSize) * Index;
				switch (Accessor.componentType)
				{
					case TINYGLTF_COMPONENT_TYPE_FLOAT:
					{
						float Value = 0.0f;
						std::memcpy(&Value, Component, sizeof(float));
						OutValues[Index] = Value;
						break;
					}
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					{
						// Normalized accessors map the integer range onto [0, 1]; a plain one is a raw count.
						const uint8 Value = *Component;
						OutValues[Index] = Accessor.normalized ? static_cast<float>(Value) / 255.0f : static_cast<float>(Value);
						break;
					}
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					{
						uint16 Value = 0;
						std::memcpy(&Value, Component, sizeof(uint16));
						OutValues[Index] = Accessor.normalized ? static_cast<float>(Value) / 65535.0f : static_cast<float>(Value);
						break;
					}
					case TINYGLTF_COMPONENT_TYPE_SHORT:
					{
						int16 Value = 0;
						std::memcpy(&Value, Component, sizeof(int16));
						OutValues[Index] =
						    Accessor.normalized ? std::max(static_cast<float>(Value) / 32767.0f, -1.0f) : static_cast<float>(Value);
						break;
					}
					case TINYGLTF_COMPONENT_TYPE_BYTE:
					{
						int8 Value = 0;
						std::memcpy(&Value, Component, sizeof(int8));
						OutValues[Index] =
						    Accessor.normalized ? std::max(static_cast<float>(Value) / 127.0f, -1.0f) : static_cast<float>(Value);
						break;
					}
					default:
						return false;
				}
			}

			return true;
		}

		// Indices come as unsigned byte, short or int. They are widened to uint32 so the renderer binds one
		// index format and never has to branch per mesh.
		bool ReadIndices(const tinygltf::Model& Model, const tinygltf::Accessor& Accessor, uint32 VertexOffset,
		                 std::vector<uint32>& OutIndices)
		{
			if (Accessor.bufferView < 0 || Accessor.bufferView >= static_cast<int>(Model.bufferViews.size()))
			{
				return false;
			}

			const tinygltf::BufferView& View = Model.bufferViews[Accessor.bufferView];
			if (View.buffer < 0 || View.buffer >= static_cast<int>(Model.buffers.size()))
			{
				return false;
			}

			const tinygltf::Buffer& Buffer = Model.buffers[View.buffer];
			const int32 ComponentSize = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(Accessor.componentType));
			if (ComponentSize <= 0)
			{
				return false;
			}

			const size_t Stride = View.byteStride > 0 ? View.byteStride : static_cast<size_t>(ComponentSize);
			const size_t Base = View.byteOffset + Accessor.byteOffset;

			OutIndices.reserve(OutIndices.size() + Accessor.count);
			for (size_t Index = 0; Index < Accessor.count; ++Index)
			{
				const size_t Offset = Base + Stride * Index;
				if (Offset + static_cast<size_t>(ComponentSize) > Buffer.data.size())
				{
					return false;
				}

				const uint8* Component = Buffer.data.data() + Offset;
				uint32 Value = 0;
				switch (Accessor.componentType)
				{
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
						Value = *Component;
						break;
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					{
						uint16 Narrow = 0;
						std::memcpy(&Narrow, Component, sizeof(uint16));
						Value = Narrow;
						break;
					}
					case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
						std::memcpy(&Value, Component, sizeof(uint32));
						break;
					default:
						return false;
				}

				// Sections share one buffer pair, so each primitive's indices are rebased onto its slice.
				OutIndices.push_back(VertexOffset + Value);
			}

			return true;
		}

		// Derives flat normals when a primitive has none. Required rather than cosmetic: without normals
		// every surface would receive the same lighting and the model would read as a silhouette.
		void GenerateFlatNormals(std::vector<FMeshVertex>& Vertices, const std::vector<uint32>& Indices, uint32 FirstIndex,
		                         uint32 IndexCount)
		{
			for (uint32 Offset = 0; Offset + 2 < IndexCount; Offset += 3)
			{
				const uint32 I0 = Indices[FirstIndex + Offset];
				const uint32 I1 = Indices[FirstIndex + Offset + 1];
				const uint32 I2 = Indices[FirstIndex + Offset + 2];
				if (I0 >= Vertices.size() || I1 >= Vertices.size() || I2 >= Vertices.size())
				{
					continue;
				}

				const FVector3 Edge1 = Vertices[I1].Position - Vertices[I0].Position;
				const FVector3 Edge2 = Vertices[I2].Position - Vertices[I0].Position;
				const FVector3 Normal = Cross(Edge1, Edge2).GetNormalized();

				// Accumulated rather than assigned, so vertices shared between triangles end up smoothed.
				Vertices[I0].Normal = Vertices[I0].Normal + Normal;
				Vertices[I1].Normal = Vertices[I1].Normal + Normal;
				Vertices[I2].Normal = Vertices[I2].Normal + Normal;
			}
		}

		FImageData ConvertImage(const tinygltf::Image& Source)
		{
			const uint32 Width = static_cast<uint32>(std::max(Source.width, 0));
			const uint32 Height = static_cast<uint32>(std::max(Source.height, 0));

			// 16 bit sources are rare and would need a different upload format, so they are skipped rather
			// than silently truncated to something that looks wrong.
			if (Width == 0 || Height == 0 || Source.component <= 0 || Source.bits != 8)
			{
				return {};
			}

			const size_t PixelCount = static_cast<size_t>(Width) * Height;
			const size_t SourceChannels = static_cast<size_t>(Source.component);
			if (Source.image.size() < PixelCount * SourceChannels)
			{
				return {};
			}

			// Always widened to RGBA. A 1 or 3 channel texture would otherwise need its own shader path.
			std::vector<uint8> Pixels(PixelCount * 4);
			for (size_t Pixel = 0; Pixel < PixelCount; ++Pixel)
			{
				const uint8* In = Source.image.data() + Pixel * SourceChannels;
				uint8* Out = Pixels.data() + Pixel * 4;

				switch (SourceChannels)
				{
					case 1:
						// Greyscale replicated across RGB, which is how glTF expects single channel colour data.
						Out[0] = In[0];
						Out[1] = In[0];
						Out[2] = In[0];
						Out[3] = 255;
						break;
					case 2:
						Out[0] = In[0];
						Out[1] = In[0];
						Out[2] = In[0];
						Out[3] = In[1];
						break;
					case 3:
						Out[0] = In[0];
						Out[1] = In[1];
						Out[2] = In[2];
						Out[3] = 255;
						break;
					default:
						Out[0] = In[0];
						Out[1] = In[1];
						Out[2] = In[2];
						Out[3] = In[3];
						break;
				}
			}

			FImageData Result;
			Result.Name = Source.name;
			// Base colour textures are authored in sRGB by definition, and this path only feeds base colour.
			Result.SetSingleLevel(Width, Height, EPixelFormat::Rgba8Srgb, std::move(Pixels));
			return Result;
		}

		// Loads a DDS that tinygltf left alone.
		//
		// tinygltf hands image loading to stb, which does not know DDS, so those images arrive empty. The
		// file is read here instead, keeping the block compressed payload intact all the way to the GPU.
		FImageData LoadDdsImage(const tinygltf::Image& Source, const std::filesystem::path& BaseDirectory, std::string& OutWarning)
		{
			if (Source.uri.empty() || BaseDirectory.empty())
			{
				// An embedded DDS would have to arrive as a data uri, which tinygltf decodes into Source.image;
				// that case is handled by the caller before reaching here.
				return {};
			}

			const std::filesystem::path Path = BaseDirectory / std::filesystem::u8path(Source.uri);
			const FDdsLoadResult Loaded = FDdsLoader::LoadFromFile(Path);
			if (!Loaded.bSucceeded)
			{
				// A warning rather than a failure: one texture that cannot be read should cost that texture,
				// not the whole scene. The material falls back to a white texture at upload.
				OutWarning += Loaded.Message + "; ";
				return {};
			}

			return Loaded.Image;
		}

		// Resolves the image a texture actually refers to.
		//
		// MSFT_texture_dds points at a DDS while texture.source keeps a PNG for readers that do not
		// understand the extension. In the RTXDI assets those PNGs are not shipped at all, so following
		// source alone finds nothing: the extension has to be preferred where present.
		int GetTextureImageIndex(const tinygltf::Texture& Texture)
		{
			const auto Extension = Texture.extensions.find("MSFT_texture_dds");
			if (Extension != Texture.extensions.end() && Extension->second.Has("source"))
			{
				const tinygltf::Value& SourceValue = Extension->second.Get("source");
				if (SourceValue.IsInt())
				{
					return SourceValue.Get<int>();
				}
			}

			return Texture.source;
		}

		// glTF is right handed, this engine is left handed, so imported data has to be converted or every
		// scene renders mirrored. The symptom is subtle enough to miss on a symmetric model and obvious on
		// anything with text: lettering reads backwards, and an object authored to the right appears left.
		//
		// Negating Z is the conversion that keeps the up axis and the ground plane where the author put
		// them. Negating X would mirror just as correctly in the mathematical sense but would swap left and
		// right in the scene, which is wrong for anything built around a facing direction.
		//
		// This is a single reflection, so it also reverses which way a triangle winds. Every place that
		// consumes the flipped positions has to account for that, or back faces end up front and the
		// lighting inverts with them.
		FVector3 ConvertPosition(float X, float Y, float Z)
		{
			return { X, Y, -Z };
		}

		// A rotation is mirrored by negating the components that pair with the reflected axis.
		//
		// For a reflection through Z that is x and y, leaving z and w: the axis reflects like a position
		// while the angle reverses, and the two sign changes cancel on the z term. Applying the position
		// rule to all four components instead would tilt every rotated node.
		FQuat ConvertRotation(float X, float Y, float Z, float W)
		{
			return FQuat(-X, -Y, Z, W);
		}

		bool ConvertMesh(const tinygltf::Model& Model, const tinygltf::Mesh& Source, FMeshData& OutMesh)
		{
			OutMesh.Name = Source.name;

			for (const tinygltf::Primitive& Primitive : Source.primitives)
			{
				// Only triangle lists are drawn. Strips, fans and point clouds appear in the sample assets;
				// skipping them loses part of a model rather than failing the whole import.
				if (Primitive.mode != TINYGLTF_MODE_TRIANGLES)
				{
					continue;
				}

				const auto PositionIt = Primitive.attributes.find("POSITION");
				if (PositionIt == Primitive.attributes.end())
				{
					continue;
				}

				const int PositionAccessorIndex = PositionIt->second;
				if (PositionAccessorIndex < 0 || PositionAccessorIndex >= static_cast<int>(Model.accessors.size()))
				{
					continue;
				}

				const tinygltf::Accessor& PositionAccessor = Model.accessors[PositionAccessorIndex];
				const size_t VertexCount = PositionAccessor.count;
				if (VertexCount == 0)
				{
					continue;
				}

				const tinygltf::Accessor* NormalAccessor = nullptr;
				if (const auto It = Primitive.attributes.find("NORMAL"); It != Primitive.attributes.end())
				{
					if (It->second >= 0 && It->second < static_cast<int>(Model.accessors.size()))
					{
						NormalAccessor = &Model.accessors[It->second];
					}
				}

				const tinygltf::Accessor* TexCoordAccessor = nullptr;
				if (const auto It = Primitive.attributes.find("TEXCOORD_0"); It != Primitive.attributes.end())
				{
					if (It->second >= 0 && It->second < static_cast<int>(Model.accessors.size()))
					{
						TexCoordAccessor = &Model.accessors[It->second];
					}
				}

				const uint32 VertexOffset = static_cast<uint32>(OutMesh.Vertices.size());
				const uint32 FirstIndex = static_cast<uint32>(OutMesh.Indices.size());

				OutMesh.Vertices.reserve(OutMesh.Vertices.size() + VertexCount);
				for (size_t Index = 0; Index < VertexCount; ++Index)
				{
					FMeshVertex Vertex;

					float Position[3] = { 0.0f, 0.0f, 0.0f };
					if (!ReadAccessorAsFloats(Model, PositionAccessor, Index, Position, 3))
					{
						return false;
					}
					Vertex.Position = ConvertPosition(Position[0], Position[1], Position[2]);

					if (NormalAccessor != nullptr)
					{
						float Normal[3] = { 0.0f, 0.0f, 0.0f };
						if (ReadAccessorAsFloats(Model, *NormalAccessor, Index, Normal, 3))
						{
							// Same reflection as the positions: a normal that kept its original Z would point
							// through the surface it belongs to.
							Vertex.Normal = ConvertPosition(Normal[0], Normal[1], Normal[2]);
						}
					}

					if (TexCoordAccessor != nullptr)
					{
						float TexCoord[2] = { 0.0f, 0.0f };
						if (ReadAccessorAsFloats(Model, *TexCoordAccessor, Index, TexCoord, 2))
						{
							Vertex.TexCoord = { TexCoord[0], TexCoord[1] };
						}
					}

					OutMesh.Bounds.Include(Vertex.Position);
					OutMesh.Vertices.push_back(Vertex);
				}

				if (Primitive.indices >= 0 && Primitive.indices < static_cast<int>(Model.accessors.size()))
				{
					if (!ReadIndices(Model, Model.accessors[Primitive.indices], VertexOffset, OutMesh.Indices))
					{
						return false;
					}
				}
				else
				{
					// An unindexed primitive draws its vertices in order. Generating the indices here means
					// the renderer only ever deals with indexed geometry.
					OutMesh.Indices.reserve(OutMesh.Indices.size() + VertexCount);
					for (size_t Index = 0; Index < VertexCount; ++Index)
					{
						OutMesh.Indices.push_back(VertexOffset + static_cast<uint32>(Index));
					}
				}

				const uint32 IndexCount = static_cast<uint32>(OutMesh.Indices.size()) - FirstIndex;

				// A primitive whose index count is not a multiple of three would read past the last triangle.
				if (IndexCount % 3 != 0)
				{
					OutMesh.Vertices.resize(VertexOffset);
					OutMesh.Indices.resize(FirstIndex);
					continue;
				}

				// Reversed because negating Z mirrored the geometry, and a mirrored triangle winds the other
				// way. Without this every face would be inside out: with culling on they would disappear,
				// and with culling off, as here, they light as though lit from behind.
				//
				// Done after the multiple-of-three check, so a malformed primitive is discarded rather than
				// having its indices shuffled across triangle boundaries.
				for (uint32 Triangle = FirstIndex; Triangle + 2 < FirstIndex + IndexCount; Triangle += 3)
				{
					std::swap(OutMesh.Indices[Triangle], OutMesh.Indices[Triangle + 2]);
				}

				// After the winding fix, so the generated normals face the same way as the triangles they
				// are derived from.
				if (NormalAccessor == nullptr)
				{
					GenerateFlatNormals(OutMesh.Vertices, OutMesh.Indices, FirstIndex, IndexCount);
				}

				FMeshSection Section;
				Section.FirstIndex = FirstIndex;
				Section.IndexCount = IndexCount;
				Section.MaterialIndex = Primitive.material;
				OutMesh.Sections.push_back(Section);
			}

			// Accumulated normals, and any that were authored unnormalized, are made unit length once here.
			for (FMeshVertex& Vertex : OutMesh.Vertices)
			{
				const FVector3 Normalized = Vertex.Normal.GetNormalized();
				// A degenerate triangle can leave a zero normal. Facing up is arbitrary but keeps the surface
				// lit rather than black.
				Vertex.Normal = Normalized == FVector3::Zero() ? FVector3::UnitY() : Normalized;
			}

			return true;
		}

		// Reads the base colour a KHR_materials_pbrSpecularGlossiness material carries.
		//
		// The extension replaces pbrMetallicRoughness rather than adding to it, so a material using it leaves
		// baseColorTexture unset and a reader that only looks there finds nothing: every surface then draws
		// with the fallback white texture. Bistro is entirely authored this way, 234 of its 254 materials.
		//
		// diffuse is mapped onto base colour rather than the extension being implemented properly. That is
		// exact for the diffuse term, which is all this renderer has: shading is Blinn-Phong, with no
		// metallic or roughness of its own, so there is nothing for specular and glossiness to feed. Taking
		// the diffuse map is what makes the surface show its texture at all; the alternative is white.
		//
		// The extension is archived and superseded by metallic-roughness, but assets predating that are
		// common enough to matter: it stays worth reading, not worth emulating.
		void ApplySpecularGlossiness(const tinygltf::Model& Model, const tinygltf::Material& Source, FMaterialData& OutMaterial)
		{
			const auto Extension = Source.extensions.find("KHR_materials_pbrSpecularGlossiness");
			if (Extension == Source.extensions.end())
			{
				return;
			}

			const tinygltf::Value& SpecularGlossiness = Extension->second;

			// Plays the part of baseColorFactor, so an untextured material still gets its authored colour.
			if (SpecularGlossiness.Has("diffuseFactor"))
			{
				const tinygltf::Value& Factor = SpecularGlossiness.Get("diffuseFactor");
				if (Factor.IsArray() && Factor.ArrayLen() >= 4)
				{
					bool bComplete = true;
					std::array<float, 4> Components{};
					for (int Component = 0; Component < 4; ++Component)
					{
						const tinygltf::Value& Entry = Factor.Get(Component);
						// A malformed entry leaves the default rather than a zero, which would render black.
						if (!Entry.IsNumber())
						{
							bComplete = false;
							break;
						}
						Components[static_cast<size_t>(Component)] = static_cast<float>(Entry.GetNumberAsDouble());
					}

					if (bComplete)
					{
						OutMaterial.BaseColorFactor = { Components[0], Components[1], Components[2], Components[3] };
					}
				}
			}

			if (!SpecularGlossiness.Has("diffuseTexture"))
			{
				return;
			}

			const tinygltf::Value& DiffuseTexture = SpecularGlossiness.Get("diffuseTexture");
			if (!DiffuseTexture.Has("index"))
			{
				return;
			}

			const tinygltf::Value& IndexValue = DiffuseTexture.Get("index");
			if (!IndexValue.IsInt())
			{
				return;
			}

			const int TextureIndex = IndexValue.Get<int>();
			if (TextureIndex < 0 || TextureIndex >= static_cast<int>(Model.textures.size()))
			{
				return;
			}

			// Routed through the same resolver as the standard path, so MSFT_texture_dds is honoured here too.
			// Bistro needs both at once: specular-glossiness materials pointing at DDS images.
			const int ImageIndex = GetTextureImageIndex(Model.textures[static_cast<size_t>(TextureIndex)]);
			if (ImageIndex >= 0 && ImageIndex < static_cast<int>(Model.images.size()))
			{
				OutMaterial.BaseColorImage = ImageIndex;
			}
		}

		FMaterialData ConvertMaterial(const tinygltf::Model& Model, const tinygltf::Material& Source)
		{
			FMaterialData Result;
			Result.Name = Source.name;
			Result.bDoubleSided = Source.doubleSided;

			const std::vector<double>& Factor = Source.pbrMetallicRoughness.baseColorFactor;
			if (Factor.size() >= 4)
			{
				Result.BaseColorFactor = { static_cast<float>(Factor[0]), static_cast<float>(Factor[1]), static_cast<float>(Factor[2]),
				                           static_cast<float>(Factor[3]) };
			}

			if (Source.alphaMode == "MASK")
			{
				Result.AlphaCutoff = static_cast<float>(Source.alphaCutoff);
			}

			// The texture index points at a sampler/image pair; only the image is needed because sampling
			// state is uniform for base colour in this renderer.
			const int TextureIndex = Source.pbrMetallicRoughness.baseColorTexture.index;
			if (TextureIndex >= 0 && TextureIndex < static_cast<int>(Model.textures.size()))
			{
				const int ImageIndex = GetTextureImageIndex(Model.textures[TextureIndex]);
				if (ImageIndex >= 0 && ImageIndex < static_cast<int>(Model.images.size()))
				{
					Result.BaseColorImage = ImageIndex;
				}
			}

			// After the standard path, because the extension is what the material is actually authored
			// against: where both are present the extension is the one describing the intended appearance.
			ApplySpecularGlossiness(Model, Source, Result);

			return Result;
		}

		FSceneNodeData ConvertNode(const tinygltf::Node& Source)
		{
			FSceneNodeData Result;
			Result.Name = Source.name;
			Result.MeshIndex = Source.mesh;

			// Node transforms are converted from right handed to left handed the same way the geometry is.
			// Decomposing first and converting the parts is valid because the reflection distributes over
			// translation, rotation and scale: scale is unaffected, being diagonal.

			// glTF gives a node either a 4x4 matrix or separate TRS components, never both.
			if (Source.matrix.size() == 16)
			{
				// Decomposed rather than kept as a matrix, so the editor can show and edit meaningful values.
				// Column major in the file: translation sits in elements 12 to 14.
				Result.Translation = ConvertPosition(static_cast<float>(Source.matrix[12]), static_cast<float>(Source.matrix[13]),
				                                     static_cast<float>(Source.matrix[14]));

				FVector3 Columns[3];
				for (int32 Column = 0; Column < 3; ++Column)
				{
					Columns[Column] = { static_cast<float>(Source.matrix[Column * 4 + 0]),
					                    static_cast<float>(Source.matrix[Column * 4 + 1]),
					                    static_cast<float>(Source.matrix[Column * 4 + 2]) };
				}

				Result.Scale = { Columns[0].Length(), Columns[1].Length(), Columns[2].Length() };

				// Rotation is what remains once the scale is divided out. A zero scale leaves the basis
				// undefined, so identity is used instead of producing NaNs.
				FMatrix4x4 Rotation = FMatrix4x4::Identity();
				const bool bHasScale = Result.Scale.X > SmallNumber && Result.Scale.Y > SmallNumber && Result.Scale.Z > SmallNumber;
				if (bHasScale)
				{
					for (int32 Column = 0; Column < 3; ++Column)
					{
						const float Inverse = 1.0f / (&Result.Scale.X)[Column];
						Rotation.M[0][Column] = Columns[Column].X * Inverse;
						Rotation.M[1][Column] = Columns[Column].Y * Inverse;
						Rotation.M[2][Column] = Columns[Column].Z * Inverse;
					}
					const FQuat Extracted = FQuat::FromRotationMatrix(Rotation);
					Result.Rotation = ConvertRotation(Extracted.X, Extracted.Y, Extracted.Z, Extracted.W);
				}
			}
			else
			{
				if (Source.translation.size() >= 3)
				{
					Result.Translation =
					    ConvertPosition(static_cast<float>(Source.translation[0]), static_cast<float>(Source.translation[1]),
						                static_cast<float>(Source.translation[2]));
				}
				if (Source.rotation.size() >= 4)
				{
					// glTF stores quaternions as xyzw, the same order as FQuat.
					Result.Rotation = ConvertRotation(static_cast<float>(Source.rotation[0]), static_cast<float>(Source.rotation[1]),
					                                  static_cast<float>(Source.rotation[2]), static_cast<float>(Source.rotation[3]))
					                      .GetNormalized();
				}
				if (Source.scale.size() >= 3)
				{
					Result.Scale = { static_cast<float>(Source.scale[0]), static_cast<float>(Source.scale[1]),
					                 static_cast<float>(Source.scale[2]) };
				}
			}

			for (const int Child : Source.children)
			{
				if (Child >= 0)
				{
					Result.Children.push_back(static_cast<uint32>(Child));
				}
			}

			return Result;
		}

		// Marks a read the DDS loader will perform itself, because tinygltf reports a refused read only as a
		// warning string and this is the one way to recognise it again afterwards. Deliberately distinctive so
		// the match cannot collide with a genuine filesystem error.
		constexpr const char* DdsDeferralMarker = "read deferred to the DDS loader";

		// Drops the lines of a tinygltf warning that describe things this importer expects and handles.
		//
		// An asset using MSFT_texture_dds lists an ordinary image beside every DDS purely so a reader without
		// the extension has something to fall back on, and the RTXDI assets ship none of those: 343 of
		// Bistro's 686 images are absent by design, and the placeholders are not even all one format, being
		// 341 PNG and 2 JPEG. Each missing file produced a "File not found" line and each deferred DDS read
		// another, burying anything genuinely wrong in several hundred lines that all describe correct
		// behaviour.
		//
		// Matched on the shape of the message rather than on a list of extensions, because keying on ".png"
		// silently lets the JPEG pair through and the next asset would bring some third format. Every line
		// dropped here concerns an image, so a missing buffer or a malformed accessor is unaffected, and the
		// filter runs line by line so a real image problem in the same import still comes through.
		std::string StripExpectedImageWarnings(const std::string& Warning)
		{
			if (Warning.empty())
			{
				return Warning;
			}

			// A line naming an image tinygltf could not fetch, where this importer either supplies the data
			// itself or has a working substitute. Matched on the phrases tinygltf emits at that point.
			const auto IsExpected = [](const std::string& Line)
			{
				if (Line.find(DdsDeferralMarker) != std::string::npos)
				{
					return true;
				}

				// tinygltf reports the failure across two lines: the path it looked for, then the image it
				// belonged to. Only the second names an image, so the first is recognised by its own wording.
				if (Line.find("File not found") != std::string::npos || Line.find("File read error") != std::string::npos)
				{
					return true;
				}

				const bool bMentionsImage = Line.find("for image[") != std::string::npos || Line.find("image[") == 0;
				return bMentionsImage && (Line.find("Failed to load external") != std::string::npos ||
				                          Line.find("could not be decoded and will be ignored") != std::string::npos);
			};

			std::string Kept;
			SizeType LineStart = 0;
			while (LineStart <= Warning.size())
			{
				const SizeType LineEnd = Warning.find('\n', LineStart);
				const SizeType Length = LineEnd == std::string::npos ? Warning.size() - LineStart : LineEnd - LineStart;
				const std::string Line = Warning.substr(LineStart, Length);

				if (!Line.empty() && !IsExpected(Line))
				{
					Kept += Line;
					Kept += '\n';
				}

				if (LineEnd == std::string::npos)
				{
					break;
				}
				LineStart = LineEnd + 1;
			}

			return Kept;
		}

		FGltfImportResult ConvertModel(const tinygltf::Model& Model, const std::string& Warning, const std::filesystem::path& BaseDirectory,
		                               FGltfImportProgress* Progress)
		{
			FGltfImportResult Result;
			Result.Message = StripExpectedImageWarnings(Warning);

			FGltfSceneData& Scene = Result.Scene;

			// The total is published before the loop so a reader never sees a count without a denominator.
			if (Progress != nullptr)
			{
				Progress->TextureCount.store(static_cast<uint32>(Model.images.size()));
				Progress->TexturesDone.store(0);
				Progress->Phase.store(EGltfImportPhase::Textures);
			}

			std::string ImageWarnings;
			Scene.Images.reserve(Model.images.size());
			for (const tinygltf::Image& Image : Model.images)
			{
				FImageData Converted = ConvertImage(Image);

				// An image tinygltf could not decode arrives empty. When the uri names a DDS that is expected,
				// since stb does not know the format, so it is read here instead.
				if (!Converted.IsValid() && FDdsLoader::HasDdsExtension(std::filesystem::u8path(Image.uri)))
				{
					Converted = LoadDdsImage(Image, BaseDirectory, ImageWarnings);
				}

				Scene.Images.push_back(std::move(Converted));

				// After the push, so the count only ever describes work that is actually finished.
				if (Progress != nullptr)
				{
					Progress->TexturesDone.store(static_cast<uint32>(Scene.Images.size()));
				}
			}

			if (!ImageWarnings.empty())
			{
				// Appended rather than replacing, so a tinygltf warning is not lost. Kept as one message
				// because a scene can reference hundreds of textures and each would otherwise be its own line.
				Result.Message += (Result.Message.empty() ? "" : " ") + ImageWarnings;
			}

			Scene.Materials.reserve(Model.materials.size());
			for (const tinygltf::Material& Material : Model.materials)
			{
				Scene.Materials.push_back(ConvertMaterial(Model, Material));
			}

			// An image that failed to decode must not stay referenced, or the upload step would look for
			// pixels that are not there.
			for (FMaterialData& Material : Scene.Materials)
			{
				if (Material.BaseColorImage >= 0 && !Scene.Images[static_cast<size_t>(Material.BaseColorImage)].IsValid())
				{
					Material.BaseColorImage = -1;
				}
			}

			if (Progress != nullptr)
			{
				Progress->MeshCount.store(static_cast<uint32>(Model.meshes.size()));
				Progress->MeshesDone.store(0);
				Progress->Phase.store(EGltfImportPhase::Meshes);
			}

			Scene.Meshes.reserve(Model.meshes.size());
			for (const tinygltf::Mesh& Mesh : Model.meshes)
			{
				FMeshData MeshData;
				if (!ConvertMesh(Model, Mesh, MeshData))
				{
					Result.bSucceeded = false;
					Result.Message = "Failed to read mesh '" + Mesh.name + "': accessor data is out of range";
					return Result;
				}
				Scene.Meshes.push_back(std::move(MeshData));

				if (Progress != nullptr)
				{
					Progress->MeshesDone.store(static_cast<uint32>(Scene.Meshes.size()));
				}
			}

			if (Progress != nullptr)
			{
				Progress->Phase.store(EGltfImportPhase::Finalizing);
			}

			Scene.Nodes.reserve(Model.nodes.size());
			for (const tinygltf::Node& Node : Model.nodes)
			{
				Scene.Nodes.push_back(ConvertNode(Node));
			}

			// A child index outside the array would be followed later while walking the hierarchy.
			for (FSceneNodeData& Node : Scene.Nodes)
			{
				std::erase_if(Node.Children, [&Scene](uint32 Child) { return Child >= Scene.Nodes.size(); });
			}

			// A mesh index that survived conversion may still point past the meshes that were kept.
			for (FSceneNodeData& Node : Scene.Nodes)
			{
				if (Node.MeshIndex >= static_cast<int32>(Scene.Meshes.size()))
				{
					Node.MeshIndex = -1;
				}
			}

			// The default scene names the roots. Files without one still have nodes, so every node that is
			// nobody's child becomes a root instead of the import coming back empty.
			const int DefaultScene = Model.defaultScene >= 0 ? Model.defaultScene : 0;
			if (DefaultScene < static_cast<int>(Model.scenes.size()))
			{
				for (const int Node : Model.scenes[static_cast<size_t>(DefaultScene)].nodes)
				{
					if (Node >= 0 && Node < static_cast<int>(Scene.Nodes.size()))
					{
						Scene.RootNodes.push_back(static_cast<uint32>(Node));
					}
				}
			}

			if (Scene.RootNodes.empty() && !Scene.Nodes.empty())
			{
				std::vector<bool> bIsChild(Scene.Nodes.size(), false);
				for (const FSceneNodeData& Node : Scene.Nodes)
				{
					for (const uint32 Child : Node.Children)
					{
						bIsChild[Child] = true;
					}
				}
				for (size_t Index = 0; Index < Scene.Nodes.size(); ++Index)
				{
					if (!bIsChild[Index])
					{
						Scene.RootNodes.push_back(static_cast<uint32>(Index));
					}
				}
			}

			Result.bSucceeded = true;
			return Result;
		}

		// Decodes an image, treating anything stb cannot read as absent rather than as a failure.
		//
		// tinygltf's built-in loader reports an undecodable image as an error, which aborts the whole parse.
		// That is too strict for real assets: a scene using MSFT_texture_dds lists a DDS alongside each PNG,
		// and stb cannot read DDS at all, so one unsupported texture would cost the entire model. The upload
		// path already substitutes a white texture for an image that did not decode, so losing one texture
		// degrades the material rather than the scene.
		bool LoadImageLenient(tinygltf::Image* Image, const int ImageIndex, std::string* Error, std::string* Warning, int RequiredWidth,
		                      int RequiredHeight, const unsigned char* Bytes, int Size, void* UserData)
		{
			LIME_UNUSED(UserData);

			// Errors are deliberately swallowed: tinygltf propagates whatever lands in Error, and the point
			// here is that an image problem must not fail the import. A local buffer keeps the reason
			// available so it can be downgraded to a warning.
			std::string DecodeError;
			if (tinygltf::LoadImageData(Image, ImageIndex, &DecodeError, Warning, RequiredWidth, RequiredHeight, Bytes, Size, nullptr))
			{
				return true;
			}

			if (Warning != nullptr)
			{
				*Warning += "image[" + std::to_string(ImageIndex) + "] could not be decoded and will be ignored: " + DecodeError;
			}
			LIME_UNUSED(Error);

			// Cleared so nothing downstream mistakes it for a usable image; FImageData::IsValid then reports
			// false and the material falls back to the white texture.
			Image->width = 0;
			Image->height = 0;
			Image->component = 0;
			Image->image.clear();
			return true;
		}

		// Skips reading a file whose contents cannot be used, so the bytes are never fetched at all.
		//
		// tinygltf reads an external image in full before handing it to the decode callback, and a DDS is
		// then discarded because stb cannot read it, only to be read again from disk by LoadDdsImage. On
		// Bistro that is 2.2 GB of block compressed texture transferred twice for one scene, which is the
		// bulk of its load time and happens with no indication that anything is progressing.
		//
		// Refusing the read here removes the wasted copy. Returning false is a supported outcome rather than
		// a trick: ParseImage treats an image it could not load as non-fatal, keeps image->uri, and carries
		// on, which is exactly the state LoadDdsImage needs to do the real read.
		//
		// Deliberately narrow. Only paths this engine will re-read itself are refused; buffers share these
		// callbacks, and refusing a .bin would lose the geometry.
		bool ReadWholeFileSkippingRedundant(std::vector<unsigned char>* Out, std::string* Error, const std::string& FilePath,
		                                    void* UserData)
		{
			if (FDdsLoader::HasDdsExtension(std::filesystem::u8path(FilePath)))
			{
				if (Error != nullptr)
				{
					*Error = DdsDeferralMarker;
				}
				return false;
			}

			return tinygltf::ReadWholeFile(Out, Error, FilePath, UserData);
		}

		// Filesystem callbacks with the redundant image read removed; see ReadWholeFileSkippingRedundant.
		//
		// tinygltf requires every callback to be set when any is, so the defaults are named explicitly.
		tinygltf::FsCallbacks MakeFsCallbacks()
		{
			return tinygltf::FsCallbacks{ &tinygltf::FileExists,     &tinygltf::ExpandFilePath,     &ReadWholeFileSkippingRedundant,
			                              &tinygltf::WriteWholeFile, &tinygltf::GetFileSizeInBytes, nullptr };
		}
	} // namespace

	FGltfImportResult FGltfImporter::LoadFromFile(const std::filesystem::path& Path, FGltfImportProgress* OutProgress)
	{
		FGltfImportResult Result;

		std::error_code Error;
		if (!std::filesystem::exists(Path, Error))
		{
			Result.Message = "File does not exist: " + Path.string();
			return Result;
		}

		tinygltf::TinyGLTF Loader;
		tinygltf::Model Model;
		std::string LoadError;
		std::string LoadWarning;

		// Installed so an image stb cannot decode does not fail the whole import; see LoadImageLenient.
		Loader.SetImageLoader(&LoadImageLenient, nullptr);
		Loader.SetFsCallbacks(MakeFsCallbacks());

		// The extension picks the container, but a mislabelled file is common enough that the other form is
		// tried as well before giving up.
		std::string Extension = Path.extension().string();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(),
		               [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });

		const std::string FileName = Path.string();
		const bool bPreferBinary = Extension == ".glb";

		// One opaque call, so this phase can report that it is running but not how far along it is. That is
		// a property of tinygltf's interface rather than a shortcut: it returns only when finished.
		if (OutProgress != nullptr)
		{
			OutProgress->Phase.store(EGltfImportPhase::Parsing);
		}

		bool bLoaded = bPreferBinary ? Loader.LoadBinaryFromFile(&Model, &LoadError, &LoadWarning, FileName)
		                             : Loader.LoadASCIIFromFile(&Model, &LoadError, &LoadWarning, FileName);

		if (!bLoaded)
		{
			// Kept, because it is the error that actually explains the failure. The fallback below almost
			// always fails with a container level complaint ("Invalid magic" for a JSON file read as binary),
			// and reporting that instead would hide the real reason entirely.
			const std::string PrimaryError = LoadError;

			LoadError.clear();
			bLoaded = bPreferBinary ? Loader.LoadASCIIFromFile(&Model, &LoadError, &LoadWarning, FileName)
			                        : Loader.LoadBinaryFromFile(&Model, &LoadError, &LoadWarning, FileName);

			if (!bLoaded)
			{
				LoadError = PrimaryError.empty() ? LoadError : PrimaryError;
			}
		}

		if (!bLoaded)
		{
			Result.Message = LoadError.empty() ? "tinygltf could not parse " + FileName : LoadError;
			if (OutProgress != nullptr)
			{
				// Marked done even on failure, so a reader waiting for the phase to settle is not left
				// looking at Parsing forever.
				OutProgress->Phase.store(EGltfImportPhase::Done);
			}
			return Result;
		}

		// The directory the file sits in, which is what relative image uris resolve against.
		Result = ConvertModel(Model, LoadWarning, Path.parent_path(), OutProgress);
		Result.Scene.SourcePath = FileName;

		if (OutProgress != nullptr)
		{
			OutProgress->Phase.store(EGltfImportPhase::Done);
		}
		return Result;
	}

	FGltfImportResult FGltfImporter::LoadFromString(const std::string& Json, const std::filesystem::path& BaseDirectory)
	{
		FGltfImportResult Result;

		tinygltf::TinyGLTF Loader;
		tinygltf::Model Model;
		std::string LoadError;
		std::string LoadWarning;

		// Same leniency as the file path, so a document exercised in a test behaves like one on disk.
		Loader.SetImageLoader(&LoadImageLenient, nullptr);
		Loader.SetFsCallbacks(MakeFsCallbacks());

		if (!Loader.LoadASCIIFromString(&Model, &LoadError, &LoadWarning, Json.c_str(), static_cast<unsigned int>(Json.size()),
		                                BaseDirectory.string()))
		{
			Result.Message = LoadError.empty() ? "tinygltf could not parse the document" : LoadError;
			return Result;
		}

		return ConvertModel(Model, LoadWarning, BaseDirectory, nullptr);
	}
} // namespace Lime
