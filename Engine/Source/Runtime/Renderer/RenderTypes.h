// Shared renderer types.

#pragma once

#include "Core/Math/Vector.h"
#include "RHI/RHITypes.h"

namespace Lime
{
	class FRenderer;

	// Matches the POSITION/COLOR semantics declared in Triangle.hlsl.
	struct FSimpleVertex
	{
		FVector3 Position;
		FVector4 Color;
	};

	struct FFrameContext
	{
		float DeltaSeconds = 0.0f;
		double TotalSeconds = 0.0;
		uint32 ViewportWidth = 0;
		uint32 ViewportHeight = 0;
		nvrhi::IFramebuffer* Framebuffer = nullptr;
		nvrhi::ICommandList* CommandList = nullptr;

		float GetAspectRatio() const
		{
			return ViewportHeight > 0 ? static_cast<float>(ViewportWidth) / static_cast<float>(ViewportHeight) : 1.0f;
		}
	};

	// A unit of rendering work. Resources are created once in Initialize and reused every frame.
	class IRenderPass
	{
	public:
		virtual ~IRenderPass() = default;

		virtual bool Initialize(FRenderer& Renderer) = 0;
		virtual void Shutdown() = 0;
		virtual void Render(const FFrameContext& Context) = 0;
		// Called when the back buffer changes, so pipelines bound to a framebuffer can be rebuilt.
		virtual void OnFramebufferChanged(nvrhi::IFramebuffer* Framebuffer) { LIME_UNUSED(Framebuffer); }
	};
} // namespace Lime
