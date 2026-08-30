// Frame orchestration.
//
// The scene is drawn by a compiled render graph: what runs, in what order, and into which textures all
// come from the graph a project ships rather than from the order passes happened to register in. A graph
// that fails to compile leaves the scene plan empty, and the frame renders only the editor UI. That is
// the deliberate fallback: rendering nothing is obvious, whereas rendering a partially built graph looks
// like a bug in whatever it drew.
//
// The frame still runs in two stages, because they are two submissions rather than two orderings:
//   Scene stage:     the compiled graph, drawing into its own textures. Its marked output is copied into
//                    the viewport target the editor samples.
//   EditorUI stage:  the editor's own pass, always into the back buffer, so editor chrome is never part
//                    of the scene image. Submitted separately because it samples what the scene wrote.

#pragma once

#include "RHI/ShaderLibrary.h"
#include "RenderGraph/RenderGraphCompiler.h"
#include "Renderer/RenderGraphExecutor.h"
#include "Renderer/RenderGraphResources.h"
#include "Renderer/RenderTypes.h"
#include "Renderer/ViewportTarget.h"

#include <functional>
#include <memory>
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

		// Initializes the pass and inserts it by priority. Returns false when initialization failed,
		// in which case the pass is not retained.
		bool AddPass(std::shared_ptr<IRenderPass> Pass);

		// Typed lookup, so a panel can reach a pass without a construction time dependency.
		// Returns nullptr when the pass is not registered.
		template<typename PassType>
		PassType* FindPass() const
		{
			return static_cast<PassType*>(FindPassByTypeId(PassType::StaticTypeId()));
		}

		const std::vector<std::shared_ptr<IRenderPass>>& GetPasses() const { return Passes; }

		// Adopts a compiled graph as the scene plan, matching each compiled entry to a registered pass.
		//
		// Fails when the graph names a pass this build does not have, which leaves the previous plan in
		// place rather than half replacing it. A caller that has nothing to fall back to calls
		// ClearRenderGraph, which is the state that renders only the editor UI.
		bool SetRenderGraph(FRenderGraphCompileResult Compiled);
		void ClearRenderGraph();
		bool HasRenderGraph() const { return SceneGraphPlan.IsRunnable(); }
		const FRenderGraphCompileResult& GetRenderGraphResult() const { return SceneGraphPlan.Compiled; }

		// Applies a pending viewport resize and prepares per frame state. Call before the stages.
		bool BeginFrame(float DeltaSeconds, double TotalSeconds);
		// Runs the compiled scene graph, then copies its marked output into the viewport target. Does
		// nothing when no graph is loaded.
		void RenderScene();
		// Clears the back buffer and runs the editor UI passes. Separate command list submission,
		// because the editor samples the scene target.
		void RenderEditorUI();
		void EndFrame();

		// Drops framebuffer bound state in every pass so the swap chain can be recreated.
		void ReleaseFramebufferDependentResources();

		IDeviceManager& GetDeviceManager() const { return *DeviceManager; }
		nvrhi::IDevice* GetDevice() const { return Device; }
		FShaderLibrary& GetShaderLibrary() { return ShaderLibrary; }
		nvrhi::ICommandList* GetCommandList() const { return CommandList; }
		const FFrameContext& GetFrameContext() const { return SceneContext; }

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
		// Editor UI passes are those at or above the EditorUI priority; everything else is scene work.
		static bool IsEditorUIPass(const IRenderPass& Pass);
		void NotifySceneFramebuffer(nvrhi::IFramebuffer* Framebuffer);
		void NotifyEditorUIFramebuffer(nvrhi::IFramebuffer* Framebuffer);

		// Allocates the graph's textures at the current target size and gives each pass its Compile call.
		// Re-run after a resize, since the resources that follow the graph's size have to be rebuilt.
		bool AllocateRenderGraphResources();
		// Copies the graph's first marked output into the viewport target, which is what the editor samples.
		// A blit rather than rendering straight into the target, because the graph's own output may be a
		// different format and the pass that produced it does not know what the editor wants.
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
		std::vector<std::shared_ptr<IRenderPass>> Passes;

		// The scene as a compiled graph. Empty when no graph loaded or one failed to compile, in which case
		// the scene stage does nothing at all.
		FRenderGraphPlan SceneGraphPlan;
		FRenderGraphResources SceneGraphResources;
		// Size the resources were built for, so a resize is noticed without asking the device.
		uint32 GraphResourceWidth = 0;
		uint32 GraphResourceHeight = 0;

		std::vector<FViewportTarget> ViewportTargets;
		FViewportResizedDelegate ViewportResizedDelegate;

		FFrameContext SceneContext;
		FFrameContext EditorUIContext;
		// Not owned: the engine holds the scene and outlives the renderer's use of it.
		FScene* Scene = nullptr;
		// Not owned either, for the same reason.
		ICamera* Camera = nullptr;
		FVector4 ClearColor{ 0.06f, 0.07f, 0.09f, 1.0f };
		// Tracked separately: the two stages can target different framebuffers.
		nvrhi::IFramebuffer* LastSceneFramebuffer = nullptr;
		nvrhi::IFramebuffer* LastEditorUIFramebuffer = nullptr;
		bool bOffscreenEnabled = false;
		bool bFrameOpen = false;
	};
} // namespace Lime
