// Frame orchestration.
//
// The frame is one compiled render graph. What runs, in what order, and into which textures all come from
// the graph a project ships, plus whatever the engine injected: in editor mode the UI pass, and in both
// modes a blit that puts the graph's output where it can be seen. A graph that fails to compile leaves the
// plan empty, and the frame renders only the injected passes. That is the deliberate fallback: rendering
// nothing is obvious, whereas rendering a partially built graph looks like a bug in whatever it drew.
//
// There is no separate editor stage any more. The editor UI is a pass in the graph like any other, ordered
// by its declared dependency on the presented scene and writing the back buffer as an imported resource.
// Submission boundaries are still possible, but they are asked for by a pass rather than implied by a
// priority.

#pragma once

#include "RHI/ShaderLibrary.h"
#include "RenderGraph/RenderGraphCompiler.h"
#include "RenderGraph/RenderGraphInjection.h"
#include "Renderer/RenderGraphExecutor.h"
#include "Renderer/RenderGraphResources.h"
#include "Renderer/RenderTypes.h"
#include "Renderer/ViewportTarget.h"

#include <functional>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace Lime
{
	class IDeviceManager;

	class FRenderer
	{
	public:
		FRenderer();
		~FRenderer();

		LIME_NON_COPYABLE(FRenderer);
		LIME_NON_MOVABLE(FRenderer);

		// The import names the engine binds every frame. A graph reaches these through a pass that declares
		// an imported resource; they are named here so the pass and the binding cannot drift apart.
		static constexpr const char* BackBufferImport = "$BackBuffer";

		bool Initialize(IDeviceManager& InDeviceManager);
		void Shutdown();

		// Creates the offscreen target and routes scene passes into it. Called by the editor.
		bool EnableOffscreenRendering(uint32 Width, uint32 Height);
		void DisableOffscreenRendering();
		bool IsOffscreenRenderingEnabled() const { return bOffscreenEnabled; }
		FViewportTarget& GetMainViewportTarget() { return ViewportTargets[0]; }
		FViewportTarget* GetViewportTarget(SizeType Index = 0)
		{
			if (Index >= ViewportTargets.size()) return nullptr;
			return &ViewportTargets[Index];
		}

		// Initializes the pass and retains it. Returns false when initialization failed, in which case the
		// pass is not kept.
		//
		// These are the long lived instances: a pass whose state must survive a graph recompile lives here
		// and is bound into the plan by type rather than built from the factory each time. The editor UI
		// pass is the reason that exists — rebuilding it would drop every ImTextureID it handed out.
		bool AddPass(std::shared_ptr<IRenderPass> Pass);

		// Typed lookup, so a panel can reach a pass without a construction time dependency.
		// Returns nullptr when the pass is not registered.
		template<typename PassType>
		PassType* FindPass() const
		{
			return static_cast<PassType*>(FindPassByTypeId(PassType::StaticTypeId()));
		}

		const std::vector<std::shared_ptr<IRenderPass>>& GetPasses() const { return Passes; }

		// Resolves a graph pass by its instance name in the graph (e.g. "DebugVisualizer2" rather than the
		// type name). Two graph instances of the same type are distinct, so this is how the render graph
		// panel and the automation commands address a specific one. Returns nullptr when no graph is
		// loaded or the name is not in it.
		IRenderPass* FindGraphPass(std::string_view PassName) const;

		// The running graph's passes as (instance name, pass) pairs, in execution order. Empty when no
		// graph is loaded. The names are what FindGraphPass and the render graph panel address passes by.
		std::vector<std::pair<std::string_view, IRenderPass*>> GetGraphPasses() const;

		// Adopts a compiled graph as the frame plan, matching each compiled entry to a pass.
		//
		// Injected entries are bound to an existing long lived instance; described ones are built from the
		// registry's factory. Fails when the graph names a pass this build does not have, which leaves the
		// previous plan in place rather than half replacing it. A caller that has nothing to fall back to
		// calls ClearRenderGraph.
		bool SetRenderGraph(FRenderGraphCompileResult Compiled);
		void ClearRenderGraph();
		bool HasRenderGraph() const { return GraphPlan.IsRunnable(); }
		const FRenderGraphCompileResult& GetRenderGraphResult() const { return GraphPlan.Compiled; }

		// Applies a pending viewport resize and prepares per frame state. Call before RenderFrame.
		bool BeginFrame(float DeltaSeconds, double TotalSeconds);
		// Runs the whole compiled graph: the project's passes and the engine's injected ones, in one order.
		void RenderFrame();
		void EndFrame();

		// Drops framebuffer bound state in every pass so the swap chain can be recreated.
		void ReleaseFramebufferDependentResources();

		IDeviceManager& GetDeviceManager() const { return *DeviceManager; }
		nvrhi::IDevice* GetDevice() const { return Device; }
		FShaderLibrary& GetShaderLibrary() { return ShaderLibrary; }
		nvrhi::ICommandList* GetCommandList() const { return CommandList; }
		const FFrameContext& GetFrameContext() const { return FrameContext; }

		void SetClearColor(const FVector4& Color) { ClearColor = Color; }
		const FVector4& GetClearColor() const { return ClearColor; }

		// The scene handed to every pass through the frame context. Null means no scene is loaded, which
		// is a normal state: a project without a configured scene still runs.
		void SetScene(FScene* InScene) { Scene = InScene; }
		FScene* GetScene() const { return Scene; }

		// The camera scene passes render through. Null is valid: a pass that needs one skips its work.
		void SetCamera(ICamera* InCamera) { Camera = InCamera; }
		ICamera* GetCamera() const { return Camera; }

		// Invoked after the viewport target is recreated, so the editor can rebind its ImGui texture.
		using FViewportResizedDelegate = std::function<void(FViewportTarget&)>;
		void SetViewportResizedDelegate(FViewportResizedDelegate Delegate) { ViewportResizedDelegate = std::move(Delegate); }

	private:
		IRenderPass* FindPassByTypeId(FRenderPassTypeId TypeId) const;
		void NotifyFramebufferChanged(nvrhi::IFramebuffer* Framebuffer);

		// Allocates the graph's textures at the current target size and gives each pass its Compile call.
		// Re-run after a resize, since the resources that follow the graph's size have to be rebuilt.
		bool AllocateRenderGraphResources();
		// Points the graph's imported slots at this frame's textures. Done per frame because the back
		// buffer rotates.
		void BindImportedResources();
		// Copies the graph's marked outputs into the viewport targets the editor samples.
		//
		// Still a copy, and still here rather than in the graph, because the two sides may legitimately
		// disagree on format: the graph can end in a float target while the viewport is 8 bit. Keeping it
		// also gives ImGui a texture whose identity only changes on resize.
		void PresentRenderGraphOutput(SizeType Slot);

		// The output format last reported as unable to reach the viewport. Remembered so the warning is
		// logged once rather than every frame, and reset on a successful copy so a later mismatch is
		// reported again.
		nvrhi::Format ReportedPresentFormatMismatch = nvrhi::Format::UNKNOWN;

		IDeviceManager* DeviceManager = nullptr;
		nvrhi::IDevice* Device = nullptr;
		// Reused across frames; NVRHI object creation is not cheap enough to do per frame.
		nvrhi::CommandListHandle CommandList;
		FShaderLibrary ShaderLibrary;
		// Long lived instances, bound into the plan when the graph names their type.
		std::vector<std::shared_ptr<IRenderPass>> Passes;

		// The frame as a compiled graph. Empty when no graph loaded or one failed to compile, in which case
		// only the injected passes run.
		FRenderGraphPlan GraphPlan;
		FRenderGraphResources GraphResources;
		// Size the resources were built for, so a resize is noticed without asking the device.
		uint32 GraphResourceWidth = 0;
		uint32 GraphResourceHeight = 0;

		std::vector<FViewportTarget> ViewportTargets;
		FViewportResizedDelegate ViewportResizedDelegate;

		FFrameContext FrameContext;
		// Not owned: the engine holds the scene and outlives the renderer's use of it.
		FScene* Scene = nullptr;
		// Not owned either, for the same reason.
		ICamera* Camera = nullptr;
		FVector4 ClearColor{ 0.06f, 0.07f, 0.09f, 1.0f };
		nvrhi::IFramebuffer* LastFramebuffer = nullptr;
		bool bOffscreenEnabled = false;
		bool bFrameOpen = false;
	};
} // namespace Lime
