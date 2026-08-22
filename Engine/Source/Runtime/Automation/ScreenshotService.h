// GPU readback based screenshots.
//
// Reads pixels straight out of the swap chain or the editor's viewport target, which makes captures
// independent of window occlusion, z-order and display scaling. A screen grab cannot offer that.
//
// A capture is requested during the frame and resolved after the frame has been submitted, so the
// image always contains what that frame actually drew. The GPU is waited on before mapping, which
// stalls the pipeline: this is a debug facility and correctness matters more than throughput.

#pragma once

#include "Core/CoreTypes.h"

#include <nvrhi/nvrhi.h>

#include <filesystem>
#include <string>

namespace Lime
{
	class FRenderer;
	class IDeviceManager;

	enum class EScreenshotSource : uint8
	{
		// Everything on screen, editor chrome included.
		BackBuffer = 0,
		// The scene alone, without the editor UI. Only available while the editor is active.
		Viewport
	};

	class FScreenshotService
	{
	public:
		FScreenshotService() = default;
		~FScreenshotService();

		LIME_NON_COPYABLE(FScreenshotService);
		LIME_NON_MOVABLE(FScreenshotService);

		void Initialize(IDeviceManager& InDeviceManager, FRenderer& InRenderer);
		void Shutdown();

		// Captures the given source and writes a PNG. Must be called after the frame was submitted
		// and before the next Present, which is where the back buffer still holds this frame.
		// Returns false and fills OutError on any failure.
		bool Capture(EScreenshotSource Source, const std::filesystem::path& Path, std::string& OutError);

		// Resolves a caller supplied name into an absolute path under Saved/Screenshots. A name with
		// no extension gets .png; absolute paths are used as given.
		static std::filesystem::path ResolveOutputPath(const std::string& Name);

	private:
		// Copies the texture into a staging texture and encodes the mapped rows.
		bool ReadbackAndWrite(nvrhi::ITexture* Texture, const std::filesystem::path& Path, std::string& OutError);

		IDeviceManager* DeviceManager = nullptr;
		FRenderer* Renderer = nullptr;
		// Reused across captures, and recreated when the size or format changes.
		nvrhi::StagingTextureHandle StagingTexture;
		nvrhi::CommandListHandle CommandList;
	};

	const char* ToString(EScreenshotSource Source);
	bool TryParseScreenshotSource(std::string_view Text, EScreenshotSource& OutSource);
} // namespace Lime
