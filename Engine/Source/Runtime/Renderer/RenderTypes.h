// Shared renderer types.

#pragma once

#include "Core/Math/Vector.h"
#include "Core/Reflection/Reflection.h"
#include "RHI/RHITypes.h"

namespace Lime
{
	class FRenderer;

	// Explicit ordering, so a pass ends up in the right place regardless of when it registers.
	// Passes are sorted by this value and ties keep registration order.
	enum class ERenderPassPriority : int32
	{
		Background = -1000,
		// Default for project passes.
		Scene = 0,
		PostProcess = 1000,
		Overlay = 2000,
		// The editor's ImGui pass, always drawn last.
		UI = 3000
	};

	// Vertex layout used by the simple pipelines; matches POSITION/COLOR semantics in HLSL.
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

	// Opaque per-class identity, used to look a pass up by type without RTTI.
	using FRenderPassTypeId = const void*;

	// A reflected object exposed by a pass: its meta type plus the address of the instance. Kept as a
	// plain pair rather than a meta_any because meta_any does not expose the underlying pointer.
	struct FReflectedRef
	{
		entt::meta_type Type;
		void* Instance = nullptr;

		bool IsValid() const { return static_cast<bool>(Type) && Instance != nullptr; }
	};

	// A unit of rendering work. Resources are created once in Initialize and reused every frame.
	class IRenderPass
	{
	public:
		virtual ~IRenderPass() = default;

		virtual const char* GetName() const = 0;
		// Identifies the concrete class. TRenderPass supplies this automatically.
		virtual FRenderPassTypeId GetTypeId() const = 0;

		virtual bool Initialize(FRenderer& Renderer) = 0;
		virtual void Shutdown() = 0;
		virtual void Render(const FFrameContext& Context) = 0;
		// Called when the back buffer changes, so pipelines bound to a framebuffer can be rebuilt.
		virtual void OnFramebufferChanged(nvrhi::IFramebuffer* Framebuffer) { LIME_UNUSED(Framebuffer); }

		// Exposes the pass settings so the editor can generate controls without knowing the concrete
		// type. Invalid by default, in which case the pass does not appear in the inspector.
		virtual FReflectedRef GetReflectedSettings() { return {}; }
	};

	// CRTP helper providing the type identity. Passes derive from this rather than IRenderPass.
	template<typename DerivedType>
	class TRenderPass : public IRenderPass
	{
	public:
		// The address of a function local static is unique per instantiation and stable across calls.
		static FRenderPassTypeId StaticTypeId()
		{
			static const char Marker = 0;
			return &Marker;
		}

		FRenderPassTypeId GetTypeId() const override { return StaticTypeId(); }
	};

	// Builds the reflected reference for a settings object; registers the type on first use.
	template<typename SettingsType>
	FReflectedRef MakeReflectedRef(SettingsType& Settings)
	{
		static_assert(bHasReflection<SettingsType>, "MakeReflectedRef requires a LIME_REFLECT declaration");
		return FReflectedRef{ Reflect<SettingsType>(), &Settings };
	}
} // namespace Lime
