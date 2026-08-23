// Frame orchestration.
//
// The frame runs in two stages so the editor can show the scene inside a viewport panel:
//   Scene stage:     passes below ERenderPassPriority::EditorUI draw into the viewport target when
//                    the editor is enabled, or straight into the back buffer otherwise. In-world UI
//                    belongs here.
//   EditorUI stage:  passes at ERenderPassPriority::EditorUI or above always draw into the back
//                    buffer, so the editor chrome is never part of the scene image.
// Passes themselves only read FFrameContext, so they are unaware of which mode is active.

#pragma once

#include "RHI/ShaderLibrary.h"
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
		FRenderer() = default;
		~FRenderer();

		LIME_NON_COPYABLE(FRenderer);
		LIME_NON_MOVABLE(FRenderer);

		bool Initialize(IDeviceManager& InDeviceManager);
		void Shutdown();

		// Creates the offscreen target and routes scene passes into it. Called by the editor.
		bool EnableOffscreenRendering(uint32 Width, uint32 Height);
		void DisableOffscreenRendering();
		bool IsOffscreenRenderingEnabled() const { return bOffscreenEnabled; }
		FViewportTarget& GetViewportTarget() { return ViewportTarget; }

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

		// Applies a pending viewport resize and prepares per frame state. Call before the stages.
		bool BeginFrame(float DeltaSeconds, double TotalSeconds);
		// Clears the scene target and runs the passes below the EditorUI priority.
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

		// Invoked after the viewport target is recreated, so the editor can rebind its ImGui texture.
		using FViewportResizedDelegate = std::function<void(FViewportTarget&)>;
		void SetViewportResizedDelegate(FViewportResizedDelegate Delegate) { ViewportResizedDelegate = std::move(Delegate); }

	private:
		IRenderPass* FindPassByTypeId(FRenderPassTypeId TypeId) const;
		// Editor UI passes are those at or above the EditorUI priority; everything else is scene work.
		static bool IsEditorUIPass(const IRenderPass& Pass);
		void NotifySceneFramebuffer(nvrhi::IFramebuffer* Framebuffer);
		void NotifyEditorUIFramebuffer(nvrhi::IFramebuffer* Framebuffer);

		IDeviceManager* DeviceManager = nullptr;
		nvrhi::IDevice* Device = nullptr;
		// Reused across frames; NVRHI object creation is not cheap enough to do per frame.
		nvrhi::CommandListHandle CommandList;
		FShaderLibrary ShaderLibrary;
		std::vector<std::shared_ptr<IRenderPass>> Passes;

		FViewportTarget ViewportTarget;
		FViewportResizedDelegate ViewportResizedDelegate;

		FFrameContext SceneContext;
		FFrameContext EditorUIContext;
		// Not owned: the engine holds the scene and outlives the renderer's use of it.
		FScene* Scene = nullptr;
		FVector4 ClearColor{ 0.06f, 0.07f, 0.09f, 1.0f };
		// Tracked separately: the two stages can target different framebuffers.
		nvrhi::IFramebuffer* LastSceneFramebuffer = nullptr;
		nvrhi::IFramebuffer* LastEditorUIFramebuffer = nullptr;
		bool bOffscreenEnabled = false;
		bool bFrameOpen = false;
	};
} // namespace Lime
