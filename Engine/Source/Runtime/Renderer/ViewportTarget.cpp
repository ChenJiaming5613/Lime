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

		// A depth buffer is required for 3D scenes: without one, triangles are drawn in submission
		// order and closer surfaces get overwritten by farther ones. It is created alongside the colour
		// target so the two can never disagree on size.
		//
		// isTypeless matters and is not optional. A depth resource is viewed through two incompatible
		// view types, a depth stencil view for writing and a shader view for reading, and only a
		// typeless resource can carry both. Creating it as a plain depth format makes the D3D12 backend
		// fault while building descriptors. This follows Donut's GBuffer, which is written against the
		// same NVRHI.
		const nvrhi::TextureDesc DepthDesc = nvrhi::TextureDesc()
		                                         .setDimension(nvrhi::TextureDimension::Texture2D)
		                                         .setWidth(Width)
		                                         .setHeight(Height)
		                                         .setFormat(DepthFormat)
		                                         .setIsRenderTarget(true)
		                                         .setIsTypeless(true)
		                                         .setInitialState(nvrhi::ResourceStates::DepthWrite)
		                                         .setKeepInitialState(true)
		                                         // Must match the value RenderScene clears with, or D3D12
		                                         // loses the fast clear path and warns about it.
		                                         .setClearValue(nvrhi::Color(1.0f))
		                                         .setDebugName("ViewportDepth");

		DepthTexture = Device->createTexture(DepthDesc);
		if (DepthTexture == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createTexture failed for the viewport depth buffer ({}x{})", Width, Height);
			Texture = nullptr;
			return false;
		}

		Framebuffer = Device->createFramebuffer(
		    nvrhi::FramebufferDesc().addColorAttachment(Texture).setDepthAttachment(DepthTexture));
		if (Framebuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createFramebuffer failed for the viewport target");
			DepthTexture = nullptr;
			Texture = nullptr;
			return false;
		}

		CurrentWidth = Width;
		CurrentHeight = Height;
		return true;
	}

	void FViewportTarget::ReleaseResources()
	{
		// Framebuffer first: it references both textures, so releasing it last would leave the handles
		// alive until the framebuffer itself went away.
		Framebuffer = nullptr;
		DepthTexture = nullptr;
		Texture = nullptr;
	}
} // namespace Lime
