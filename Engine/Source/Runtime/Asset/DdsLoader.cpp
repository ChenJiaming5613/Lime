#include "Asset/DdsLoader.h"

#include "Core/Logging/LogManager.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace Lime
{
	namespace
	{
		constexpr uint32 DdsMagic = 0x20534444; // "DDS " little endian.

		// Header layouts from the DDS specification. Sizes are asserted below rather than trusted, because
		// the file records them and a mismatch means the file is not what it claims.
		struct FDdsPixelFormat
		{
			uint32 Size;
			uint32 Flags;
			uint32 FourCC;
			uint32 RgbBitCount;
			uint32 RBitMask;
			uint32 GBitMask;
			uint32 BBitMask;
			uint32 ABitMask;
		};

		struct FDdsHeader
		{
			uint32 Size;
			uint32 Flags;
			uint32 Height;
			uint32 Width;
			uint32 PitchOrLinearSize;
			uint32 Depth;
			uint32 MipMapCount;
			uint32 Reserved1[11];
			FDdsPixelFormat PixelFormat;
			uint32 Caps;
			uint32 Caps2;
			uint32 Caps3;
			uint32 Caps4;
			uint32 Reserved2;
		};

		struct FDdsHeaderDxt10
		{
			uint32 DxgiFormat;
			uint32 ResourceDimension;
			uint32 MiscFlag;
			uint32 ArraySize;
			uint32 MiscFlags2;
		};

		// The file stores these sizes; anything else means the layout is not the documented one.
		static_assert(sizeof(FDdsHeader) == 124, "DDS_HEADER must be 124 bytes");
		static_assert(sizeof(FDdsPixelFormat) == 32, "DDS_PIXELFORMAT must be 32 bytes");
		static_assert(sizeof(FDdsHeaderDxt10) == 20, "DDS_HEADER_DXT10 must be 20 bytes");

		// DDS_PIXELFORMAT flags.
		constexpr uint32 DdsPixelFormatFourCC = 0x4;
		// "DX10", marking the presence of the extension header.
		constexpr uint32 FourCcDx10 = 0x30315844;

		// DDS_HEADER_DXT10 resource dimensions.
		constexpr uint32 DdsDimensionTexture2D = 3;
		constexpr uint32 DdsDimensionTexture1D = 2;
		constexpr uint32 DdsDimensionTexture3D = 4;
		// D3D11_RESOURCE_MISC_TEXTURECUBE.
		constexpr uint32 DdsMiscTextureCube = 0x4;

		// The subset of DXGI_FORMAT this engine can upload. An explicit switch rather than a table indexed
		// by enum value: the latter has to stay in exact sync with the enum's ordering, and breaks without a
		// compile error when a value is inserted.
		EPixelFormat ConvertDxgiFormat(uint32 DxgiFormat)
		{
			switch (DxgiFormat)
			{
				case 28:
					return EPixelFormat::Rgba8Unorm; // R8G8B8A8_UNORM
				case 29:
					return EPixelFormat::Rgba8Srgb; // R8G8B8A8_UNORM_SRGB
				case 87:
					return EPixelFormat::Bgra8Unorm; // B8G8R8A8_UNORM
				case 91:
					return EPixelFormat::Bgra8Srgb; // B8G8R8A8_UNORM_SRGB

				case 71:
					return EPixelFormat::Bc1Unorm; // BC1_UNORM
				case 72:
					return EPixelFormat::Bc1Srgb; // BC1_UNORM_SRGB
				case 74:
					return EPixelFormat::Bc2Unorm;
				case 75:
					return EPixelFormat::Bc2Srgb;
				case 77:
					return EPixelFormat::Bc3Unorm;
				case 78:
					return EPixelFormat::Bc3Srgb;
				case 80:
					return EPixelFormat::Bc4Unorm;
				case 81:
					return EPixelFormat::Bc4Snorm;
				case 83:
					return EPixelFormat::Bc5Unorm;
				case 84:
					return EPixelFormat::Bc5Snorm;
				case 95:
					return EPixelFormat::Bc6HUfloat;
				case 96:
					return EPixelFormat::Bc6HSfloat;
				case 98:
					return EPixelFormat::Bc7Unorm;
				case 99:
					return EPixelFormat::Bc7Srgb;

				default:
					return EPixelFormat::Unknown;
			}
		}

		// Bytes one mip level occupies, and its row pitch.
		//
		// Block compressed levels are measured in 4x4 blocks, and the block count rounds up: a 2x2 level
		// still costs one whole block. Getting that wrong makes every level past the point where a dimension
		// drops below 4 read from the wrong offset, which shows as the smallest mips being garbage.
		void ComputeLevelSize(uint32 Width, uint32 Height, EPixelFormat Format, uint64& OutSize, uint32& OutRowPitch)
		{
			const uint32 BlockSize = GetFormatBlockSize(Format);

			if (IsBlockCompressed(Format))
			{
				const uint64 BlocksWide = std::max<uint64>(1, (static_cast<uint64>(Width) + 3) / 4);
				const uint64 BlocksHigh = std::max<uint64>(1, (static_cast<uint64>(Height) + 3) / 4);
				OutRowPitch = static_cast<uint32>(BlocksWide * BlockSize);
				OutSize = BlocksWide * BlocksHigh * BlockSize;
				return;
			}

			OutRowPitch = Width * BlockSize;
			OutSize = static_cast<uint64>(OutRowPitch) * Height;
		}
	} // namespace

	bool FDdsLoader::HasDdsExtension(const std::filesystem::path& Path)
	{
		std::string Extension = Path.extension().string();
		std::transform(Extension.begin(), Extension.end(), Extension.begin(),
		               [](unsigned char Character) { return static_cast<char>(std::tolower(Character)); });
		return Extension == ".dds";
	}

	bool FDdsLoader::LooksLikeDds(const uint8* Bytes, size_t Size)
	{
		if (Bytes == nullptr || Size < sizeof(uint32))
		{
			return false;
		}

		uint32 Magic = 0;
		std::memcpy(&Magic, Bytes, sizeof(Magic));
		return Magic == DdsMagic;
	}

	FDdsLoadResult FDdsLoader::LoadFromFile(const std::filesystem::path& Path)
	{
		FDdsLoadResult Result;

		std::error_code Error;
		if (!std::filesystem::exists(Path, Error))
		{
			Result.Message = "File does not exist: " + Path.string();
			return Result;
		}

		const uintmax_t FileSize = std::filesystem::file_size(Path, Error);
		if (Error)
		{
			Result.Message = "Could not read the size of " + Path.string();
			return Result;
		}

		std::ifstream File(Path, std::ios::binary);
		if (!File.is_open())
		{
			Result.Message = "Could not open " + Path.string();
			return Result;
		}

		std::vector<uint8> Bytes(static_cast<size_t>(FileSize));
		File.read(reinterpret_cast<char*>(Bytes.data()), static_cast<std::streamsize>(Bytes.size()));
		if (!File)
		{
			Result.Message = "Could not read " + Path.string();
			return Result;
		}

		return LoadFromMemory(Bytes.data(), Bytes.size(), Path.filename().string());
	}

	FDdsLoadResult FDdsLoader::LoadFromMemory(const uint8* Bytes, size_t Size, const std::string& DebugName)
	{
		FDdsLoadResult Result;

		constexpr size_t MinimumSize = sizeof(uint32) + sizeof(FDdsHeader);
		if (Bytes == nullptr || Size < MinimumSize)
		{
			Result.Message = DebugName + ": too small to be a DDS (" + std::to_string(Size) + " bytes)";
			return Result;
		}

		if (!LooksLikeDds(Bytes, Size))
		{
			Result.Message = DebugName + ": not a DDS file (bad magic)";
			return Result;
		}

		FDdsHeader Header{};
		std::memcpy(&Header, Bytes + sizeof(uint32), sizeof(Header));

		// The header records its own size, so a mismatch means the file is not the documented layout.
		if (Header.Size != sizeof(FDdsHeader) || Header.PixelFormat.Size != sizeof(FDdsPixelFormat))
		{
			Result.Message = DebugName + ": unexpected header size, the file may be corrupt";
			return Result;
		}

		const bool bHasDxt10 = (Header.PixelFormat.Flags & DdsPixelFormatFourCC) != 0 && Header.PixelFormat.FourCC == FourCcDx10;

		if (!bHasDxt10)
		{
			// Refused rather than guessed. The legacy path describes D3D9 formats through bit masks, and
			// picking the wrong interpretation yields a texture that looks plausible with its channels
			// swapped, which is far harder to notice than a clear failure.
			Result.Message = DebugName + ": only DDS files with the DX10 extension header are supported. Re-export with "
			                             "'texconv -dx10', or with NVIDIA Texture Tools, which writes that header by default";
			return Result;
		}

		const size_t Dxt10Offset = sizeof(uint32) + sizeof(FDdsHeader);
		if (Size < Dxt10Offset + sizeof(FDdsHeaderDxt10))
		{
			Result.Message = DebugName + ": truncated before the DX10 extension header";
			return Result;
		}

		FDdsHeaderDxt10 Dxt10{};
		std::memcpy(&Dxt10, Bytes + Dxt10Offset, sizeof(Dxt10));

		const EPixelFormat Format = ConvertDxgiFormat(Dxt10.DxgiFormat);
		if (Format == EPixelFormat::Unknown)
		{
			Result.Message = DebugName + ": unsupported DXGI format " + std::to_string(Dxt10.DxgiFormat);
			return Result;
		}

		// Only plain 2D textures. A cube map or a volume needs per slice upload, and accepting one here
		// would mean uploading it with a layout that ignores the extra slices.
		if (Dxt10.ResourceDimension != DdsDimensionTexture2D)
		{
			const char* Kind = Dxt10.ResourceDimension == DdsDimensionTexture1D
			                       ? "1D"
			                       : (Dxt10.ResourceDimension == DdsDimensionTexture3D ? "3D (volume)" : "unknown");
			Result.Message = DebugName + ": only 2D textures are supported, this one is " + Kind;
			return Result;
		}

		if ((Dxt10.MiscFlag & DdsMiscTextureCube) != 0)
		{
			Result.Message = DebugName + ": cube maps are not supported";
			return Result;
		}

		if (Dxt10.ArraySize > 1)
		{
			Result.Message = DebugName + ": texture arrays are not supported (array size " + std::to_string(Dxt10.ArraySize) + ")";
			return Result;
		}

		if (Header.Width == 0 || Header.Height == 0)
		{
			Result.Message = DebugName + ": zero sized image";
			return Result;
		}

		// A file may record zero to mean "one level".
		const uint32 MipCount = std::max<uint32>(1, Header.MipMapCount);

		const size_t PayloadOffset = Dxt10Offset + sizeof(FDdsHeaderDxt10);
		const size_t PayloadSize = Size - PayloadOffset;

		// Levels are walked to build the layout and to check the payload is long enough before any of it is
		// read. A truncated file would otherwise be discovered by writeTexture reading past the buffer.
		std::vector<FImageMipLevel> Mips;
		Mips.reserve(MipCount);

		uint64 Consumed = 0;
		for (uint32 Level = 0; Level < MipCount; ++Level)
		{
			const uint32 LevelWidth = std::max<uint32>(1, Header.Width >> Level);
			const uint32 LevelHeight = std::max<uint32>(1, Header.Height >> Level);

			uint64 LevelSize = 0;
			uint32 RowPitch = 0;
			ComputeLevelSize(LevelWidth, LevelHeight, Format, LevelSize, RowPitch);

			if (Consumed + LevelSize > PayloadSize)
			{
				// Truncated. The levels already gathered are usable, so the image is kept at a reduced mip
				// count rather than discarded; a texture at slightly lower detail beats no texture.
				if (Level == 0)
				{
					Result.Message = DebugName + ": payload is too short even for the first mip level";
					return Result;
				}

				LIME_LOG_WARNING(LIME_LOG_CATEGORY_SCENE, "{}: truncated after {} of {} mip level(s)", DebugName, Level, MipCount);
				break;
			}

			FImageMipLevel Mip;
			Mip.Width = LevelWidth;
			Mip.Height = LevelHeight;
			Mip.Offset = Consumed;
			Mip.Size = LevelSize;
			Mip.RowPitch = RowPitch;
			Mips.push_back(Mip);

			Consumed += LevelSize;
		}

		FImageData& Image = Result.Image;
		Image.Name = DebugName;
		Image.Width = Header.Width;
		Image.Height = Header.Height;
		Image.Format = Format;
		Image.Mips = std::move(Mips);
		// Only the bytes the layout covers, so a file with trailing data does not carry it into memory.
		Image.Pixels.assign(Bytes + PayloadOffset, Bytes + PayloadOffset + Consumed);

		Result.bSucceeded = true;
		return Result;
	}
} // namespace Lime
