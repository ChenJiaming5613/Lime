#include "Renderer/ViewportTarget.h"

#include "Core/Logging/LogManager.h"

#include <algorithm>

namespace Lime
{
	FViewportTarget::~FViewportTarget()
	{
		Shutdown();
	}

	uint32 FViewportTarget::ClampSize(uint32 Value)
	{
		return std::clamp(Value, MinSize, MaxSize);
	}

	bool FViewportTarget::NeedsResize(uint32 CurrentWidth, uint32 CurrentHeight, uint32 RequestedWidth, uint32 RequestedHeight)
	{
		// A zero request means "no request yet", which must not trigger a rebuild.
		if (RequestedWidth == 0 || RequestedHeight == 0)
		{
			return false;
		}
		return ClampSize(RequestedWidth) != CurrentWidth || ClampSize(RequestedHeight) != CurrentHeight;
	}

	bool FViewportTarget::Initialize(nvrhi::IDevice* InDevice, nvrhi::Format InFormat, uint32 Width, uint32 Height)
	{
		if (InDevice == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "FViewportTarget requires a valid device");
			return false;
		}

		Device = InDevice;
		Format = InFormat;
		return CreateResources(ClampSize(Width), ClampSize(Height));
	}

	void FViewportTarget::Shutdown()
	{
		ReleaseResources();
		Device = nullptr;
		CurrentWidth = 0;
		CurrentHeight = 0;
		RequestedWidth = 0;
		RequestedHeight = 0;
	}

	void FViewportTarget::RequestResize(uint32 Width, uint32 Height)
	{
		RequestedWidth = Width;
		RequestedHeight = Height;
	}

	bool FViewportTarget::ApplyPendingResize()
	{
		if (Device == nullptr || !NeedsResize(CurrentWidth, CurrentHeight, RequestedWidth, RequestedHeight))
		{
			return false;
		}

		const uint32 NewWidth = ClampSize(RequestedWidth);
		const uint32 NewHeight = ClampSize(RequestedHeight);

		// The old texture may still be referenced by in-flight command lists and by an ImGui binding
		// set, so the GPU has to drain before it is destroyed. NVRHI also defers destruction, hence
		// the explicit garbage collection.
		Device->waitForIdle();
		ReleaseResources();
		Device->runGarbageCollection();

		if (!CreateResources(NewWidth, NewHeight))
		{
			return false;
		}

		LIME_LOG_TRACE(LIME_LOG_CATEGORY_RENDERER, "Viewport target resized to {}x{}", NewWidth, NewHeight);
		return true;
	}

	bool FViewportTarget::CreateResources(uint32 Width, uint32 Height)
	{
		const nvrhi::TextureDesc TextureDesc = nvrhi::TextureDesc()
		                                           .setDimension(nvrhi::TextureDimension::Texture2D)
		                                           .setWidth(Width)
		                                           .setHeight(Height)
		                                           .setFormat(Format)
		                                           .setIsRenderTarget(true)
		                                           // Sampled by the ImGui pass to show the scene in the panel.
		                                           .setInitialState(nvrhi::ResourceStates::ShaderResource)
		                                           .setKeepInitialState(true)
		                                           .setDebugName("ViewportColor");

		Texture = Device->createTexture(TextureDesc);
		if (Texture == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createTexture failed for the viewport target ({}x{})", Width, Height);
			return false;
		}

		Framebuffer = Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(Texture));
		if (Framebuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createFramebuffer failed for the viewport target");
			Texture = nullptr;
			return false;
		}

		CurrentWidth = Width;
		CurrentHeight = Height;
		return true;
	}

	void FViewportTarget::ReleaseResources()
	{
		Framebuffer = nullptr;
		Texture = nullptr;
	}
} // namespace Lime
