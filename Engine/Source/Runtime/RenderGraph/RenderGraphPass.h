// The pass abstraction: what the graph executes, and what one pass sees of the graph.
//
// Lives here rather than in LimeRenderer because the graph is what calls every one of these methods.
// A pass declares its resources in this module's vocabulary, is compiled against sizes this module's
// compiler negotiated, and is executed in an order this module produced; having the base class sit above
// all of that meant the graph could describe a pass but never own one, which is what kept generic passes
// like the blit out of this module.
//
// Nothing here needs a renderer, a scene or a camera: they travel through as forward declared pointers,
// so only the passes that dereference them take on those dependencies. That is what allows this module
// to define the base class without depending on LimeRenderer, and keeps the dependency running one way.

#pragma once

#include "Core/Reflection/Reflection.h"
#include "RenderGraph/RenderGraphPassType.h"

#include <string_view>

namespace Lime
{
	class FRenderer;
	// Forward declared so this header stays free of a dependency on the scene module, which would
	// otherwise make LimeRenderGraph depend on LimeScene and close a cycle.
	class FScene;
	// Likewise forward declared. Only a pointer travels through the frame context, so this module needs
	// no link dependency on the camera module; the passes that dereference it already have one.
	class ICamera;

	// What a pass sees of the resources the graph allocated for it.
	//
	// Looked up by the pass's own field name, not by the name it has in the graph. That is deliberate: a
	// pass asking for "shadowDepth" works the same whether the graph calls the instance "ShadowCaster" or
	// something else, which is what allows one pass type to appear in a graph more than once.
	//
	// An unconnected optional input returns null, and a pass that declared one is expected to check.
	class FRenderGraphPassResources
	{
	public:
		virtual ~FRenderGraphPassResources() = default;

		virtual nvrhi::ITexture* FindTexture(std::string_view FieldName) const = 0;
		// The framebuffer built from this pass's outputs, or null when it declared none.
		virtual nvrhi::IFramebuffer* GetFramebuffer() const = 0;
		// Size of the graph's default target, which is what a pass uses to set its viewport when it did not
		// pin its own dimensions.
		virtual uint32 GetWidth() const = 0;
		virtual uint32 GetHeight() const = 0;
	};

	// Explicit ordering, so a pass ends up in the right place regardless of when it registers.
	// Passes are sorted by this value and ties keep registration order.
	//
	// Ordering only. Which target a pass draws into and when the frame is submitted are decided by the
	// graph it sits in, not by this value: an injected pass writing the back buffer is ordered by its
	// dependencies like any other. In-world UI therefore belongs below EditorUI purely so it sorts before
	// the editor chrome in the registry listing.
	enum class ERenderPassPriority : int32
	{
		Background = -1000,
		// Default for project passes.
		Scene = 0,
		PostProcess = 1000,
		// Scene space overlays such as gizmos or in-world UI.
		Overlay = 2000,
		// The editor's ImGui pass, which the engine injects last.
		EditorUI = 3000
	};

	struct FFrameContext
	{
		float DeltaSeconds = 0.0f;
		double TotalSeconds = 0.0;
		uint32 ViewportWidth = 0;
		uint32 ViewportHeight = 0;
		nvrhi::IFramebuffer* Framebuffer = nullptr;
		nvrhi::ICommandList* CommandList = nullptr;
		// The scene being rendered, or null when none is loaded. Passed through the frame context
		// rather than injected into each pass, so it travels the same path as the framebuffer and the
		// timing data and no pass needs to manage its lifetime.
		FScene* Scene = nullptr;
		// The camera the scene is viewed through, or null when none is set. Travels the same path as the
		// scene: a pass reads it per frame rather than holding a reference, so a camera can be swapped
		// between frames without notifying anything.
		ICamera* Camera = nullptr;
		// True when the scene renders into the editor viewport instead of the swap chain. Passes only
		// need this if they care about the distinction; the framebuffer and size already differ.
		bool bIsOffscreen = false;
		// The resources the graph allocated for the pass being executed, or null when the pass is running
		// outside a graph. Set per pass rather than per frame, since each sees only its own fields.
		const FRenderGraphPassResources* Resources = nullptr;

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
	//
	// Also the base class for everything the render graph executes. A pass declares what it reads and
	// writes through Reflect, is given the negotiated sizes and formats through Compile, and reaches its
	// resources by field name during Render. A pass that declares nothing still works: it draws into
	// whatever target the graph hands it.
	class IRenderPass
	{
	public:
		virtual ~IRenderPass() = default;

		// The instance name, used in logs and in the inspector.
		virtual const char* GetName() const = 0;
		// Identifies the concrete class. TRenderPass supplies this automatically.
		virtual FRenderPassTypeId GetTypeId() const = 0;
		// Decides the registration order. TRenderPass forwards the static Priority member, so a pass
		// declares it in exactly one place.
		virtual ERenderPassPriority GetPriority() const = 0;

		// The type name a render graph file refers to this pass by.
		//
		// Defaults to the instance name because for a built-in pass the two are the same. They are distinct
		// concepts: a graph may hold several instances of one type, each with its own name, so a pass that
		// can be instantiated more than once overrides this.
		virtual const char* GetTypeName() const { return GetName(); }

		// Describes the resources this pass reads and writes.
		//
		// Called while the graph is compiled, possibly more than once, so it must stay cheap: no device
		// calls, no allocation beyond the description itself. A pass that declares nothing takes part in
		// the graph purely for its ordering, drawing into whatever it is given.
		virtual void Reflect(FRenderGraphPassTypeDesc& OutType) const { LIME_UNUSED(OutType); }

		virtual bool Initialize(FRenderer& Renderer) = 0;
		virtual void Shutdown() = 0;

		// Called once per compile, after the graph has resolved every size and format this pass will see.
		//
		// This is where a pass builds anything that depends on those: a pipeline is compiled against a
		// framebuffer's formats, so it cannot be created before the graph has decided them. Returning false
		// fails the compile, which puts the engine into its fallback rather than rendering incorrectly.
		virtual bool Compile(FRenderer& Renderer, const FRenderGraphPassResources& Resources)
		{
			LIME_UNUSED(Renderer);
			LIME_UNUSED(Resources);
			return true;
		}

		// Runs before the render targets are cleared, so a pass can update renderer wide state such as
		// the clear colour. Doing that from Render would be one frame late.
		virtual void OnBeginFrame(FRenderer& Renderer, const FFrameContext& Context)
		{
			LIME_UNUSED(Renderer);
			LIME_UNUSED(Context);
		}
		virtual void Render(const FFrameContext& Context) = 0;
		// Called when the back buffer changes, so pipelines bound to a framebuffer can be rebuilt.
		virtual void OnFramebufferChanged(nvrhi::IFramebuffer* Framebuffer) { LIME_UNUSED(Framebuffer); }

		// Exposes the pass settings so the editor can generate controls without knowing the concrete
		// type. Invalid by default, in which case the pass does not appear in the inspector.
		virtual FReflectedRef GetReflectedSettings() { return {}; }
	};

	// CRTP helper providing the type identity and the priority. Passes derive from this rather than
	// IRenderPass, and declare `static constexpr ERenderPassPriority Priority`.
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
		ERenderPassPriority GetPriority() const override { return DerivedType::Priority; }
	};

	// Builds the reflected reference for a settings object; registers the type on first use.
	template<typename SettingsType>
	FReflectedRef MakeReflectedRef(SettingsType& Settings)
	{
		static_assert(bHasReflection<SettingsType>, "MakeReflectedRef requires a LIME_REFLECT declaration");
		return FReflectedRef{ Reflect<SettingsType>(), &Settings };
	}
} // namespace Lime
