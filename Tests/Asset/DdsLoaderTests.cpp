// DDS loading tests.
//
// Headers are built byte by byte in memory, so the suite needs no fixture files and runs without a
// graphics device. The layout maths is the part worth pinning down: a wrong mip offset makes the smaller
// levels read from the wrong place, which shows only at distance and is miserable to diagnose from a
// screenshot.

#include "Asset/DdsLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

using namespace Lime;

namespace
{
	// DXGI_FORMAT values used below.
	constexpr uint32 DxgiBc7UnormSrgb = 99;
	constexpr uint32 DxgiBc1Unorm = 71;
	constexpr uint32 DxgiRgba8Unorm = 28;
	// A format the loader does not map, to check it is refused rather than guessed at.
	constexpr uint32 DxgiR1Unorm = 66;

	constexpr uint32 FourCcDx10 = 0x30315844;
	constexpr uint32 DimensionTexture2D = 3;
	constexpr uint32 DimensionTexture3D = 4;
	constexpr uint32 MiscTextureCube = 0x4;

	void Append(std::vector<uint8>& Bytes, uint32 Value)
	{
		const size_t Offset = Bytes.size();
		Bytes.resize(Offset + sizeof(uint32));
		std::memcpy(Bytes.data() + Offset, &Value, sizeof(uint32));
	}

	// Builds a DDS in memory. Every field a test needs to vary is a member, so a case reads as "this file
	// but with a cube map flag" rather than as a wall of byte writes.
	struct FDdsBuilder
	{
		uint32 Width = 4;
		uint32 Height = 4;
		uint32 MipCount = 1;
		uint32 DxgiFormat = DxgiBc7UnormSrgb;
		uint32 Dimension = DimensionTexture2D;
		uint32 MiscFlag = 0;
		uint32 ArraySize = 1;
		bool bWithDx10Header = true;
		uint32 HeaderSize = 124;
		uint32 Magic = 0x20534444;
		// Zero means "exactly what the declared layout needs"; anything else forces that many bytes.
		size_t PayloadOverride = 0;

		size_t ComputePayloadSize() const
		{
			const bool bCompressed = DxgiFormat != DxgiRgba8Unorm;
			const uint32 BlockSize = (DxgiFormat == DxgiBc1Unorm) ? 8 : 16;

			size_t Total = 0;
			for (uint32 Level = 0; Level < std::max<uint32>(1, MipCount); ++Level)
			{
				const uint32 LevelWidth = std::max<uint32>(1, Width >> Level);
				const uint32 LevelHeight = std::max<uint32>(1, Height >> Level);

				if (bCompressed)
				{
					Total += std::max<size_t>(1, (LevelWidth + 3) / 4) * std::max<size_t>(1, (LevelHeight + 3) / 4) * BlockSize;
				}
				else
				{
					Total += static_cast<size_t>(LevelWidth) * LevelHeight * 4;
				}
			}
			return Total;
		}

		std::vector<uint8> Build() const
		{
			std::vector<uint8> Bytes;
			Append(Bytes, Magic);

			// DDS_HEADER
			Append(Bytes, HeaderSize);
			Append(Bytes, 0x1007);
			Append(Bytes, Height);
			Append(Bytes, Width);
			Append(Bytes, 0);
			Append(Bytes, 0);
			Append(Bytes, MipCount);
			for (int32 Index = 0; Index < 11; ++Index)
			{
				Append(Bytes, 0);
			}

			// DDS_PIXELFORMAT. The FourCC flag plus 'DX10' is what marks the extension header.
			Append(Bytes, 32);
			Append(Bytes, bWithDx10Header ? 0x4u : 0x40u);
			Append(Bytes, bWithDx10Header ? FourCcDx10 : 0u);
			Append(Bytes, bWithDx10Header ? 0u : 32u);
			Append(Bytes, 0x00ff0000);
			Append(Bytes, 0x0000ff00);
			Append(Bytes, 0x000000ff);
			Append(Bytes, 0xff000000);
			Append(Bytes, 0x1000);
			Append(Bytes, 0);
			Append(Bytes, 0);
			Append(Bytes, 0);
			Append(Bytes, 0);

			if (bWithDx10Header)
			{
				Append(Bytes, DxgiFormat);
				Append(Bytes, Dimension);
				Append(Bytes, MiscFlag);
				Append(Bytes, ArraySize);
				Append(Bytes, 0);
			}

			const size_t PayloadSize = PayloadOverride > 0 ? PayloadOverride : ComputePayloadSize();
			for (size_t Index = 0; Index < PayloadSize; ++Index)
			{
				Bytes.push_back(static_cast<uint8>(Index & 0xff));
			}

			return Bytes;
		}
	};

	FDdsLoadResult Load(const FDdsBuilder& Builder)
	{
		const std::vector<uint8> Bytes = Builder.Build();
		return FDdsLoader::LoadFromMemory(Bytes.data(), Bytes.size(), "test.dds");
	}
} // namespace

TEST_CASE("A single level BC7 image loads", "[Asset][Dds]")
{
	FDdsBuilder Builder;
	Builder.Width = 8;
	Builder.Height = 8;

	const FDdsLoadResult Result = Load(Builder);

	INFO("loader message: " << Result.Message);
	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Image.IsValid());
	REQUIRE(Result.Image.Width == 8);
	REQUIRE(Result.Image.Height == 8);
	REQUIRE(Result.Image.Format == EPixelFormat::Bc7Srgb);

	SECTION("One level covering the whole image")
	{
		REQUIRE(Result.Image.Mips.size() == 1);
		REQUIRE(Result.Image.Mips[0].Width == 8);
		REQUIRE(Result.Image.Mips[0].Height == 8);
		REQUIRE(Result.Image.Mips[0].Offset == 0);
		// 8x8 is 2x2 blocks of 16 bytes.
		REQUIRE(Result.Image.Mips[0].Size == 64);
		// Row pitch counts blocks, not pixels: two blocks across at 16 bytes each.
		REQUIRE(Result.Image.Mips[0].RowPitch == 32);
	}

	SECTION("sRGB is carried through, since the shader must not decode twice")
	{
		REQUIRE(Result.Image.IsSrgb());
	}
}

TEST_CASE("Mip chain offsets are contiguous and correctly sized", "[Asset][Dds]")
{
	// This is what the loader exists for: every offset has to be the sum of the levels before it, or
	// writeTexture reads the wrong bytes for everything past level 0.
	FDdsBuilder Builder;
	Builder.Width = 16;
	Builder.Height = 16;
	Builder.MipCount = 5; // 16, 8, 4, 2, 1

	const FDdsLoadResult Result = Load(Builder);

	INFO("loader message: " << Result.Message);
	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Image.Mips.size() == 5);

	SECTION("Dimensions halve and stop at one")
	{
		const uint32 Expected[5] = { 16, 8, 4, 2, 1 };
		for (SizeType Level = 0; Level < 5; ++Level)
		{
			REQUIRE(Result.Image.Mips[Level].Width == Expected[Level]);
			REQUIRE(Result.Image.Mips[Level].Height == Expected[Level]);
		}
	}

	SECTION("Block counts round up below four pixels")
	{
		// A 2x2 and a 1x1 level each still occupy one whole 4x4 block. Treating them as smaller is the
		// classic mistake, and it corrupts every offset from that point on.
		REQUIRE(Result.Image.Mips[0].Size == 256); // 4x4 blocks of 16 bytes
		REQUIRE(Result.Image.Mips[1].Size == 64);  // 2x2 blocks
		REQUIRE(Result.Image.Mips[2].Size == 16);  // 1x1 block
		REQUIRE(Result.Image.Mips[3].Size == 16);  // still one block
		REQUIRE(Result.Image.Mips[4].Size == 16);  // still one block
	}

	SECTION("Each offset is the sum of the preceding levels")
	{
		uint64 Expected = 0;
		for (const FImageMipLevel& Mip : Result.Image.Mips)
		{
			REQUIRE(Mip.Offset == Expected);
			Expected += Mip.Size;
		}

		// And the payload holds exactly that much, with nothing left over.
		REQUIRE(Result.Image.Pixels.size() == Expected);
	}

	SECTION("Every level stays inside the buffer")
	{
		for (const FImageMipLevel& Mip : Result.Image.Mips)
		{
			REQUIRE(Mip.Offset + Mip.Size <= Result.Image.Pixels.size());
		}
	}
}

TEST_CASE("BC1 uses half the bytes per block that BC7 does", "[Asset][Dds]")
{
	// BC1 and BC4 pack a block into 8 bytes while every other BC format uses 16. Getting this wrong halves
	// or doubles every offset in the chain.
	FDdsBuilder Builder;
	Builder.Width = 8;
	Builder.Height = 8;
	Builder.DxgiFormat = DxgiBc1Unorm;

	const FDdsLoadResult Result = Load(Builder);

	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Image.Format == EPixelFormat::Bc1Unorm);
	REQUIRE(GetFormatBlockSize(EPixelFormat::Bc1Unorm) == 8);
	REQUIRE(Result.Image.Mips[0].Size == 32); // 2x2 blocks at 8 bytes
	REQUIRE(Result.Image.Mips[0].RowPitch == 16);
}

TEST_CASE("An uncompressed DDS measures its pitch in pixels", "[Asset][Dds]")
{
	FDdsBuilder Builder;
	Builder.DxgiFormat = DxgiRgba8Unorm;

	const FDdsLoadResult Result = Load(Builder);

	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Image.Format == EPixelFormat::Rgba8Unorm);
	REQUIRE_FALSE(IsBlockCompressed(EPixelFormat::Rgba8Unorm));
	REQUIRE(Result.Image.Mips[0].RowPitch == 16); // 4 pixels at 4 bytes
	REQUIRE(Result.Image.Mips[0].Size == 64);
	REQUIRE_FALSE(Result.Image.IsSrgb());
}

TEST_CASE("Unsupported DDS layouts are refused with an explanation", "[Asset][Dds]")
{
	// Each of these would otherwise be accepted and uploaded with a layout that does not describe the data,
	// producing a texture that looks plausible but wrong. A clear failure is far easier to act on.

	SECTION("A legacy header names the tool that can convert it")
	{
		FDdsBuilder Builder;
		Builder.bWithDx10Header = false;

		const FDdsLoadResult Result = Load(Builder);
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE(Result.Message.find("DX10") != std::string::npos);
		// The message has to say what to do about it, not merely that it failed.
		REQUIRE(Result.Message.find("texconv") != std::string::npos);
	}

	SECTION("A volume texture is refused")
	{
		FDdsBuilder Builder;
		Builder.Dimension = DimensionTexture3D;

		const FDdsLoadResult Result = Load(Builder);
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE(Result.Message.find("2D") != std::string::npos);
	}

	SECTION("A cube map is refused")
	{
		FDdsBuilder Builder;
		Builder.MiscFlag = MiscTextureCube;

		const FDdsLoadResult Result = Load(Builder);
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE(Result.Message.find("ube") != std::string::npos);
	}

	SECTION("A texture array is refused")
	{
		FDdsBuilder Builder;
		Builder.ArraySize = 6;

		const FDdsLoadResult Result = Load(Builder);
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE(Result.Message.find("array") != std::string::npos);
	}

	SECTION("An unmapped DXGI format is refused rather than guessed")
	{
		FDdsBuilder Builder;
		Builder.DxgiFormat = DxgiR1Unorm;

		const FDdsLoadResult Result = Load(Builder);
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE(Result.Message.find("66") != std::string::npos);
	}
}

TEST_CASE("Malformed DDS input fails without reading out of bounds", "[Asset][Dds]")
{
	SECTION("Bad magic")
	{
		FDdsBuilder Builder;
		Builder.Magic = 0x12345678;

		const FDdsLoadResult Result = Load(Builder);
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE(Result.Message.find("magic") != std::string::npos);
	}

	SECTION("A header that misreports its own size")
	{
		FDdsBuilder Builder;
		Builder.HeaderSize = 100;

		const FDdsLoadResult Result = Load(Builder);
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE(Result.Message.find("header size") != std::string::npos);
	}

	SECTION("Too short to hold a header")
	{
		const std::vector<uint8> Bytes = { 'D', 'D', 'S', ' ', 1, 2, 3 };
		const FDdsLoadResult Result = FDdsLoader::LoadFromMemory(Bytes.data(), Bytes.size(), "tiny.dds");
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE(Result.Message.find("too small") != std::string::npos);
	}

	SECTION("A null buffer")
	{
		const FDdsLoadResult Result = FDdsLoader::LoadFromMemory(nullptr, 0, "null.dds");
		REQUIRE_FALSE(Result.bSucceeded);
	}

	SECTION("Zero dimensions")
	{
		FDdsBuilder Builder;
		Builder.Width = 0;
		Builder.PayloadOverride = 16;

		const FDdsLoadResult Result = Load(Builder);
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE(Result.Message.find("zero sized") != std::string::npos);
	}

	SECTION("A payload too short for even the first level")
	{
		FDdsBuilder Builder;
		Builder.Width = 64;
		Builder.Height = 64;
		Builder.PayloadOverride = 8;

		const FDdsLoadResult Result = Load(Builder);
		REQUIRE_FALSE(Result.bSucceeded);
		REQUIRE(Result.Message.find("too short") != std::string::npos);
	}

	SECTION("A chain truncated partway keeps the levels that are present")
	{
		// A texture at reduced detail beats no texture, so a file that stops early is used up to the point it
		// stops rather than discarded.
		FDdsBuilder Builder;
		Builder.Width = 16;
		Builder.Height = 16;
		Builder.MipCount = 5;
		// Enough for levels 0 and 1 only: 16x16 is 256 bytes, 8x8 is 64.
		Builder.PayloadOverride = 256 + 64;

		const FDdsLoadResult Result = Load(Builder);
		REQUIRE(Result.bSucceeded);
		REQUIRE(Result.Image.Mips.size() == 2);
		REQUIRE(Result.Image.Pixels.size() == 320);
	}
}

TEST_CASE("Trailing bytes past the mip chain are not carried into memory", "[Asset][Dds]")
{
	// Some tools pad the end of the file. Copying that padding would waste memory proportional to how much
	// there is, on every texture in a scene.
	FDdsBuilder Builder;
	Builder.PayloadOverride = 16 + 4096;

	const FDdsLoadResult Result = Load(Builder);

	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Image.Mips.size() == 1);
	REQUIRE(Result.Image.Pixels.size() == 16);
}

TEST_CASE("A mip count of zero means one level", "[Asset][Dds]")
{
	// The specification allows zero to mean "no mip chain", which is one level rather than none.
	FDdsBuilder Builder;
	Builder.MipCount = 0;
	Builder.PayloadOverride = 16;

	const FDdsLoadResult Result = Load(Builder);

	REQUIRE(Result.bSucceeded);
	REQUIRE(Result.Image.Mips.size() == 1);
}

TEST_CASE("Extension and magic detection", "[Asset][Dds]")
{
	SECTION("The extension test is case insensitive")
	{
		// Asset pipelines are inconsistent about casing, and a missed match sends the file to stb, which
		// cannot read it.
		REQUIRE(FDdsLoader::HasDdsExtension("texture.dds"));
		REQUIRE(FDdsLoader::HasDdsExtension("texture.DDS"));
		REQUIRE(FDdsLoader::HasDdsExtension("path/to/Texture.Dds"));
		REQUIRE_FALSE(FDdsLoader::HasDdsExtension("texture.png"));
		REQUIRE_FALSE(FDdsLoader::HasDdsExtension("texture"));
	}

	SECTION("Magic detection tolerates a short buffer")
	{
		const std::vector<uint8> Good = { 'D', 'D', 'S', ' ' };
		REQUIRE(FDdsLoader::LooksLikeDds(Good.data(), Good.size()));

		const std::vector<uint8> Short = { 'D', 'D' };
		REQUIRE_FALSE(FDdsLoader::LooksLikeDds(Short.data(), Short.size()));
		REQUIRE_FALSE(FDdsLoader::LooksLikeDds(nullptr, 4));
	}
}

TEST_CASE("A missing DDS file is reported rather than throwing", "[Asset][Dds]")
{
	const FDdsLoadResult Result = FDdsLoader::LoadFromFile("does/not/exist.dds");
	REQUIRE_FALSE(Result.bSucceeded);
	REQUIRE(Result.Message.find("does not exist") != std::string::npos);
}

TEST_CASE("Pixel format helpers", "[Asset][Dds]")
{
	SECTION("Block sizes match the format specifications")
	{
		REQUIRE(GetFormatBlockSize(EPixelFormat::Bc1Unorm) == 8);
		REQUIRE(GetFormatBlockSize(EPixelFormat::Bc4Unorm) == 8);
		REQUIRE(GetFormatBlockSize(EPixelFormat::Bc3Unorm) == 16);
		REQUIRE(GetFormatBlockSize(EPixelFormat::Bc7Srgb) == 16);
		REQUIRE(GetFormatBlockSize(EPixelFormat::Rgba8Unorm) == 4);
		REQUIRE(GetFormatBlockSize(EPixelFormat::Unknown) == 0);
	}

	SECTION("sRGB is reported only for the encoded variants")
	{
		REQUIRE(IsSrgbFormat(EPixelFormat::Bc7Srgb));
		REQUIRE(IsSrgbFormat(EPixelFormat::Rgba8Srgb));
		REQUIRE_FALSE(IsSrgbFormat(EPixelFormat::Bc7Unorm));
		REQUIRE_FALSE(IsSrgbFormat(EPixelFormat::Bc5Unorm));
	}

	SECTION("Compression is reported for the block formats only")
	{
		REQUIRE(IsBlockCompressed(EPixelFormat::Bc1Unorm));
		REQUIRE(IsBlockCompressed(EPixelFormat::Bc6HUfloat));
		REQUIRE_FALSE(IsBlockCompressed(EPixelFormat::Rgba8Srgb));
		REQUIRE_FALSE(IsBlockCompressed(EPixelFormat::Unknown));
	}

	SECTION("SetSingleLevel produces a usable one level image")
	{
		// The stb path relies on this, so it has to yield the same shape the DDS path does.
		FImageData Image;
		Image.SetSingleLevel(2, 2, EPixelFormat::Rgba8Srgb, std::vector<uint8>(16, 0xff));

		REQUIRE(Image.IsValid());
		REQUIRE(Image.Mips.size() == 1);
		REQUIRE(Image.Mips[0].RowPitch == 8);
		REQUIRE(Image.Mips[0].Size == 16);
		REQUIRE(Image.IsSrgb());
	}
}
