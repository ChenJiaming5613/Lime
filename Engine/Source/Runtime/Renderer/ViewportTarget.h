// Offscreen render target used when the editor is active, so the scene can be shown inside a
// viewport panel instead of going straight to the swap chain.
//
// The panel only knows its size while the UI is being built, which happens after the scene has been
// rendered. The requested size is therefore applied at the start of the next frame; resizing the
// panel stretches one frame before the target matches again.

#pragma once

#include "Core/CoreTypes.h"

#include <nvrhi/nvrhi.h>

namespace Lime
{
	class FViewportTarget
	{
	public:
		FViewportTarget() = default;
		~FViewportTarget();

		LIME_NON_COPYABLE(FViewportTarget);
		LIME_NON_MOVABLE(FViewportTarget);

		bool Initialize(nvrhi::IDevice* InDevice, nvrhi::Format InFormat, uint32 Width, uint32 Height);
		void Shutdown();

		// Recreates the texture when the requested size differs. Returns true when a rebuild happened,
		// so callers can invalidate anything bound to the old texture.
		bool ApplyPendingResize();

		// Records the size the viewport panel wants; applied on the next ApplyPendingResize.
		void RequestResize(uint32 Width, uint32 Height);

		bool IsValid() const { return Framebuffer != nullptr; }
		nvrhi::IFramebuffer* GetFramebuffer() const { return Framebuffer; }
		nvrhi::ITexture* GetTexture() const { return Texture; }
		uint32 GetWidth() const { return CurrentWidth; }
		uint32 GetHeight() const { return CurrentHeight; }

		// Clamped so a collapsed panel cannot request a zero sized or absurdly large texture.
		static constexpr uint32 MinSize = 1;
		static constexpr uint32 MaxSize = 16384;
		static uint32 ClampSize(uint32 Value);
		// True when the requested size differs from the current one after clamping.
		static bool NeedsResize(uint32 CurrentWidth, uint32 CurrentHeight, uint32 RequestedWidth, uint32 RequestedHeight);

	private:
		bool CreateResources(uint32 Width, uint32 Height);
		void ReleaseResources();

		nvrhi::IDevice* Device = nullptr;
		nvrhi::TextureHandle Texture;
		nvrhi::FramebufferHandle Framebuffer;
		nvrhi::Format Format = nvrhi::Format::RGBA8_UNORM;
		uint32 CurrentWidth = 0;
		uint32 CurrentHeight = 0;
		uint32 RequestedWidth = 0;
		uint32 RequestedHeight = 0;
	};
} // namespace Lime
