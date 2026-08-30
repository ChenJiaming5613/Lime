#include "Automation/ScreenshotService.h"

#include "Core/Logging/LogManager.h"
#include "Platform/PlatformPaths.h"
#include "RHI/DeviceManager.h"
#include "Renderer/Renderer.h"

// Declarations only. The implementation is compiled into the tinygltf target, which already defines
// STB_IMAGE_WRITE_IMPLEMENTATION, so no second copy is needed here.
#include <spdlog/fmt/fmt.h>

#include <fstream>
#include <stb_image_write.h>
#include <vector>

namespace Lime
{
	namespace
	{
		// Channel order differs per backend: D3D12 uses an RGBA swap chain while Vulkan usually picks
		// BGRA. PNG is always RGBA, so blue and red are exchanged for the BGRA formats.
		bool NeedsChannelSwizzle(nvrhi::Format Format)
		{
			return Format == nvrhi::Format::BGRA8_UNORM || Format == nvrhi::Format::SBGRA8_UNORM;
		}

		bool IsSupportedFormat(nvrhi::Format Format)
		{
			switch (Format)
			{
				case nvrhi::Format::RGBA8_UNORM:
				case nvrhi::Format::SRGBA8_UNORM:
				case nvrhi::Format::BGRA8_UNORM:
				case nvrhi::Format::SBGRA8_UNORM:
					return true;
				default:
					return false;
			}
		}

		// stb is given a write callback rather than a filename: its own file path handling goes through
		// fopen with the ANSI code page, which cannot represent every path std::filesystem can.
		void WriteToStream(void* Context, void* Data, int Size)
		{
			auto* Stream = static_cast<std::ofstream*>(Context);
			Stream->write(static_cast<const char*>(Data), Size);
		}

		void WriteToVector(void* Context, void* Data, int Size)
		{
			auto* Buffer = static_cast<std::vector<uint8>*>(Context);
			const auto* Bytes = static_cast<const uint8*>(Data);
			Buffer->insert(Buffer->end(), Bytes, Bytes + Size);
		}
	} // namespace

	const char* ToString(EScreenshotSource Source)
	{
		switch (Source)
		{
			case EScreenshotSource::BackBuffer:
				return "backBuffer";
			case EScreenshotSource::Viewport:
				return "viewport";
		}
		return "backBuffer";
	}

	bool TryParseScreenshotSource(std::string_view Text, EScreenshotSource& OutSource)
	{
		if (Text == "backBuffer" || Text == "backbuffer" || Text == "window")
		{
			OutSource = EScreenshotSource::BackBuffer;
			return true;
		}
		if (Text == "viewport" || Text == "scene")
		{
			OutSource = EScreenshotSource::Viewport;
			return true;
		}
		return false;
	}

	FScreenshotService::~FScreenshotService()
	{
		Shutdown();
	}

	void FScreenshotService::Initialize(IDeviceManager& InDeviceManager, FRenderer& InRenderer)
	{
		DeviceManager = &InDeviceManager;
		Renderer = &InRenderer;
	}

	void FScreenshotService::Shutdown()
	{
		StagingTexture = nullptr;
		CommandList = nullptr;
		DeviceManager = nullptr;
		Renderer = nullptr;
	}

	std::filesystem::path FScreenshotService::ResolveOutputPath(const std::string& Name)
	{
		std::filesystem::path Requested(Name.empty() ? "Screenshot.png" : Name);
		if (!Requested.has_extension())
		{
			Requested.replace_extension(".png");
		}
		if (Requested.is_absolute())
		{
			return Requested;
		}
		return FPlatformPaths::GetSavedDirectory() / "Screenshots" / Requested;
	}

	nvrhi::ITexture* FScreenshotService::ResolveTexture(EScreenshotSource Source, std::string& OutError) const
	{
		if (DeviceManager == nullptr || Renderer == nullptr)
		{
			OutError = "The screenshot service is not initialized";
			return nullptr;
		}

		nvrhi::ITexture* Texture = nullptr;
		if (Source == EScreenshotSource::Viewport)
		{
			if (!Renderer->IsOffscreenRenderingEnabled())
			{
				OutError = "The viewport target only exists while the editor is active; capture the back buffer instead";
				return nullptr;
			}
			Texture = Renderer->GetMainViewportTarget().GetTexture();
		}
		else
		{
			// The framebuffer is used rather than a raw back buffer accessor, so the texture always
			// belongs to the image the current frame rendered into.
			nvrhi::IFramebuffer* Framebuffer = DeviceManager->GetCurrentFramebuffer();
			if (Framebuffer != nullptr && !Framebuffer->getDesc().colorAttachments.empty())
			{
				Texture = Framebuffer->getDesc().colorAttachments[0].texture;
			}
		}

		if (Texture == nullptr)
		{
			OutError = fmt::format("No texture available for source '{}'", ToString(Source));
		}
		return Texture;
	}

	bool FScreenshotService::Capture(EScreenshotSource Source, const std::filesystem::path& Path, std::string& OutError)
	{
		nvrhi::ITexture* Texture = ResolveTexture(Source, OutError);
		if (Texture == nullptr)
		{
			return false;
		}

		FImage Image;
		if (!Readback(Texture, Image, OutError))
		{
			return false;
		}

		std::error_code ErrorCode;
		std::filesystem::create_directories(Path.parent_path(), ErrorCode);

		std::ofstream Stream(Path, std::ios::binary | std::ios::trunc);
		if (!Stream)
		{
			OutError = fmt::format("Could not open '{}' for writing", FPlatformPaths::ToUtf8(Path));
			return false;
		}

		if (stbi_write_png_to_func(&WriteToStream, &Stream, static_cast<int>(Image.Width), static_cast<int>(Image.Height), 4,
		                           Image.Pixels.data(), static_cast<int>(Image.Width * 4)) == 0)
		{
			OutError = "PNG encoding failed";
			return false;
		}

		Stream.close();
		if (!Stream)
		{
			OutError = fmt::format("Could not write '{}'", FPlatformPaths::ToUtf8(Path));
			return false;
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_AUTOMATION, "Captured {}x{} to '{}'", Image.Width, Image.Height, FPlatformPaths::ToUtf8(Path));
		return true;
	}

	bool FScreenshotService::CaptureToPng(EScreenshotSource Source, std::vector<uint8>& OutPng, uint32& OutWidth, uint32& OutHeight,
	                                      std::string& OutError)
	{
		nvrhi::ITexture* Texture = ResolveTexture(Source, OutError);
		if (Texture == nullptr)
		{
			return false;
		}

		FImage Image;
		if (!Readback(Texture, Image, OutError))
		{
			return false;
		}

		if (!EncodePng(Image, OutPng, OutError))
		{
			return false;
		}

		OutWidth = Image.Width;
		OutHeight = Image.Height;
		return true;
	}

	bool FScreenshotService::EncodePng(const FImage& Image, std::vector<uint8>& OutPng, std::string& OutError)
	{
		OutPng.clear();
		// PNG of an opaque screenshot lands well under a quarter of the raw size, so this only avoids
		// the first few reallocations rather than trying to predict the result.
		OutPng.reserve(Image.Pixels.size() / 4);

		if (stbi_write_png_to_func(&WriteToVector, &OutPng, static_cast<int>(Image.Width), static_cast<int>(Image.Height), 4,
		                           Image.Pixels.data(), static_cast<int>(Image.Width * 4)) == 0)
		{
			OutError = "PNG encoding failed";
			return false;
		}
		return true;
	}

	bool FScreenshotService::Readback(nvrhi::ITexture* Texture, FImage& OutImage, std::string& OutError)
	{
		nvrhi::IDevice* Device = Renderer->GetDevice();
		if (Device == nullptr)
		{
			OutError = "No device";
			return false;
		}

		const nvrhi::TextureDesc& SourceDesc = Texture->getDesc();
		if (!IsSupportedFormat(SourceDesc.format))
		{
			OutError = fmt::format("Unsupported texture format for capture ({})", static_cast<int32>(SourceDesc.format));
			return false;
		}
		if (SourceDesc.width == 0 || SourceDesc.height == 0)
		{
			OutError = "The texture has a zero dimension";
			return false;
		}

		// The staging texture is cached, so a resize is the only reason to recreate it.
		const bool bNeedsStaging = StagingTexture == nullptr || StagingTexture->getDesc().width != SourceDesc.width ||
		                           StagingTexture->getDesc().height != SourceDesc.height ||
		                           StagingTexture->getDesc().format != SourceDesc.format;
		if (bNeedsStaging)
		{
			const nvrhi::TextureDesc StagingDesc = nvrhi::TextureDesc()
			                                           .setDimension(nvrhi::TextureDimension::Texture2D)
			                                           .setWidth(SourceDesc.width)
			                                           .setHeight(SourceDesc.height)
			                                           .setFormat(SourceDesc.format)
			                                           .setDebugName("ScreenshotStaging");

			StagingTexture = Device->createStagingTexture(StagingDesc, nvrhi::CpuAccessMode::Read);
			if (StagingTexture == nullptr)
			{
				OutError = "createStagingTexture failed";
				return false;
			}
		}

		if (CommandList == nullptr)
		{
			CommandList = Device->createCommandList();
			if (CommandList == nullptr)
			{
				OutError = "createCommandList failed";
				return false;
			}
		}

		// Both the back buffer and the viewport target are created with keepInitialState, so NVRHI
		// inserts the transition to CopySource and back on its own.
		CommandList->open();
		CommandList->copyTexture(StagingTexture, nvrhi::TextureSlice(), Texture, nvrhi::TextureSlice());
		CommandList->close();
		Device->executeCommandList(CommandList);

		// Mapping requires the copy to have completed. A full idle is heavier than a fence wait but
		// keeps this path free of per backend synchronization code.
		Device->waitForIdle();

		SizeType RowPitch = 0;
		const uint8* Mapped = static_cast<const uint8*>(
		    Device->mapStagingTexture(StagingTexture, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &RowPitch));
		if (Mapped == nullptr)
		{
			OutError = "mapStagingTexture returned null";
			return false;
		}

		const uint32 Width = SourceDesc.width;
		const uint32 Height = SourceDesc.height;
		const bool bSwizzle = NeedsChannelSwizzle(SourceDesc.format);

		// Copied into a tightly packed buffer: the mapped rows carry backend specific padding, and
		// alpha is forced opaque because the UI leaves parts of the back buffer translucent.
		OutImage.Pixels.resize(static_cast<SizeType>(Width) * Height * 4);
		OutImage.Width = Width;
		OutImage.Height = Height;

		for (uint32 Row = 0; Row < Height; ++Row)
		{
			const uint8* SourceRow = Mapped + static_cast<SizeType>(Row) * RowPitch;
			uint8* DestRow = OutImage.Pixels.data() + static_cast<SizeType>(Row) * Width * 4;

			for (uint32 Column = 0; Column < Width; ++Column)
			{
				const uint8* SourcePixel = SourceRow + static_cast<SizeType>(Column) * 4;
				uint8* DestPixel = DestRow + static_cast<SizeType>(Column) * 4;

				DestPixel[0] = bSwizzle ? SourcePixel[2] : SourcePixel[0];
				DestPixel[1] = SourcePixel[1];
				DestPixel[2] = bSwizzle ? SourcePixel[0] : SourcePixel[2];
				DestPixel[3] = 0xFF;
			}
		}

		Device->unmapStagingTexture(StagingTexture);
		return true;
	}
} // namespace Lime
