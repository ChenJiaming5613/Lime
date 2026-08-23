#include "Renderer/Renderer.h"

#include "Core/Logging/LogManager.h"
#include "Platform/PlatformPaths.h"
#include "RHI/DeviceManager.h"

#include <nvrhi/utils.h>

namespace Lime
{
	FRenderer::~FRenderer()
	{
		Shutdown();
	}

	bool FRenderer::Initialize(IDeviceManager& InDeviceManager)
	{
		DeviceManager = &InDeviceManager;
		Device = InDeviceManager.GetDevice();
		if (Device == nullptr)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RENDERER, "FRenderer requires an initialized device");
			return false;
		}

		if (!ShaderLibrary.Initialize(Device, InDeviceManager.GetBackend(), FPlatformPaths::GetEngineShaderDirectory()))
		{
			return false;
		}

		CommandList = Device->createCommandList();
		if (CommandList == nullptr)
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_RENDERER, "createCommandList failed");
			return false;
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_RENDERER, "Renderer initialized ({})", ToString(InDeviceManager.GetBackend()));
		return true;
	}

	void FRenderer::Shutdown()
	{
		if (Device == nullptr)
		{
			return;
		}

		// Passes are torn down before the command list so their handles are released first.
		for (auto Iterator = Passes.rbegin(); Iterator != Passes.rend(); ++Iterator)
		{
			(*Iterator)->Shutdown();
		}
		Passes.clear();

		ViewportResizedDelegate = nullptr;
		ViewportTarget.Shutdown();
		bOffscreenEnabled = false;

		CommandList = nullptr;
		ShaderLibrary.Shutdown();
		Device = nullptr;
		DeviceManager = nullptr;
		LastSceneFramebuffer = nullptr;
		LastEditorUIFramebuffer = nullptr;
		bFrameOpen = false;
	}

	bool FRenderer::EnableOffscreenRendering(uint32 Width, uint32 Height)
	{
		if (Device == nullptr || DeviceManager == nullptr)
		{
			return false;
		}
		if (bOffscreenEnabled)
		{
			return true;
		}

		// Same format as the back buffer, so the UI pass samples it without a conversion.
		if (!ViewportTarget.Initialize(Device, DeviceManager->GetBackBufferFormat(), Width, Height))
		{
			return false;
		}

		bOffscreenEnabled = true;
		// Scene passes were built against the back buffer layout and must be rebuilt.
		NotifySceneFramebuffer(nullptr);

		LIME_LOG_INFO(LIME_LOG_CATEGORY_RENDERER, "Scene renders into the viewport target ({}x{})", ViewportTarget.GetWidth(),
		              ViewportTarget.GetHeight());
		return true;
	}

	void FRenderer::DisableOffscreenRendering()
	{
		if (!bOffscreenEnabled)
		{
			return;
		}

		NotifySceneFramebuffer(nullptr);
		if (Device != nullptr)
		{
			Device->waitForIdle();
		}
		ViewportTarget.Shutdown();
		bOffscreenEnabled = false;
	}

	bool FRenderer::IsEditorUIPass(const IRenderPass& Pass)
	{
		return static_cast<int32>(Pass.GetPriority()) >= static_cast<int32>(ERenderPassPriority::EditorUI);
	}

	bool FRenderer::AddPass(std::shared_ptr<IRenderPass> Pass)
	{
		if (Pass == nullptr)
		{
			return false;
		}

		if (!Pass->Initialize(*this))
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Render pass '{}' failed to initialize and was not registered", Pass->GetName());
			return false;
		}

		// A pass added after the first frame must not wait for a framebuffer change to build its
		// pipeline, so it is given the framebuffer of the stage it belongs to.
		nvrhi::IFramebuffer* StageFramebuffer = IsEditorUIPass(*Pass) ? LastEditorUIFramebuffer : LastSceneFramebuffer;
		if (StageFramebuffer != nullptr)
		{
			Pass->OnFramebufferChanged(StageFramebuffer);
		}

		Passes.push_back(std::move(Pass));
		return true;
	}

	IRenderPass* FRenderer::FindPassByTypeId(FRenderPassTypeId TypeId) const
	{
		for (const std::shared_ptr<IRenderPass>& Pass : Passes)
		{
			if (Pass->GetTypeId() == TypeId)
			{
				return Pass.get();
			}
		}
		return nullptr;
	}

	void FRenderer::NotifySceneFramebuffer(nvrhi::IFramebuffer* Framebuffer)
	{
		for (const std::shared_ptr<IRenderPass>& Pass : Passes)
		{
			if (!IsEditorUIPass(*Pass))
			{
				Pass->OnFramebufferChanged(Framebuffer);
			}
		}
		LastSceneFramebuffer = Framebuffer;
	}

	void FRenderer::NotifyEditorUIFramebuffer(nvrhi::IFramebuffer* Framebuffer)
	{
		for (const std::shared_ptr<IRenderPass>& Pass : Passes)
		{
			if (IsEditorUIPass(*Pass))
			{
				Pass->OnFramebufferChanged(Framebuffer);
			}
		}
		LastEditorUIFramebuffer = Framebuffer;
	}

	bool FRenderer::BeginFrame(float DeltaSeconds, double TotalSeconds)
	{
		if (Device == nullptr || DeviceManager == nullptr || bFrameOpen)
		{
			return false;
		}

		nvrhi::IFramebuffer* BackBuffer = DeviceManager->GetCurrentFramebuffer();
		if (BackBuffer == nullptr)
		{
			return false;
		}

		// A resize requested by the viewport panel last frame is applied here, before anything binds
		// the target.
		if (bOffscreenEnabled && ViewportTarget.ApplyPendingResize())
		{
			NotifySceneFramebuffer(nullptr);
			if (ViewportResizedDelegate != nullptr)
			{
				ViewportResizedDelegate(ViewportTarget);
			}
		}

		nvrhi::IFramebuffer* SceneFramebuffer = bOffscreenEnabled ? ViewportTarget.GetFramebuffer() : BackBuffer;
		if (SceneFramebuffer == nullptr)
		{
			return false;
		}

		// Pipelines are tied to a framebuffer layout, so each stage is notified independently.
		if (SceneFramebuffer != LastSceneFramebuffer)
		{
			NotifySceneFramebuffer(SceneFramebuffer);
		}
		if (BackBuffer != LastEditorUIFramebuffer)
		{
			NotifyEditorUIFramebuffer(BackBuffer);
		}

		SceneContext.DeltaSeconds = DeltaSeconds;
		SceneContext.TotalSeconds = TotalSeconds;
		SceneContext.Framebuffer = SceneFramebuffer;
		SceneContext.CommandList = CommandList;
		SceneContext.Scene = Scene;
		SceneContext.bIsOffscreen = bOffscreenEnabled;
		SceneContext.ViewportWidth = bOffscreenEnabled ? ViewportTarget.GetWidth() : DeviceManager->GetBackBufferWidth();
		SceneContext.ViewportHeight = bOffscreenEnabled ? ViewportTarget.GetHeight() : DeviceManager->GetBackBufferHeight();

		EditorUIContext = SceneContext;
		EditorUIContext.Framebuffer = BackBuffer;
		EditorUIContext.bIsOffscreen = false;
		EditorUIContext.ViewportWidth = DeviceManager->GetBackBufferWidth();
		EditorUIContext.ViewportHeight = DeviceManager->GetBackBufferHeight();

		// Passes get to update renderer state, notably the clear colour, before anything is cleared.
		for (const std::shared_ptr<IRenderPass>& Pass : Passes)
		{
			Pass->OnBeginFrame(*this, IsEditorUIPass(*Pass) ? EditorUIContext : SceneContext);
		}

		bFrameOpen = true;
		return true;
	}

	void FRenderer::RenderScene()
	{
		if (!bFrameOpen || SceneContext.Framebuffer == nullptr)
		{
			return;
		}

		CommandList->open();
		nvrhi::utils::ClearColorAttachment(CommandList, SceneContext.Framebuffer, 0,
		                                   nvrhi::Color(ClearColor.X, ClearColor.Y, ClearColor.Z, ClearColor.W));

		// Depth is cleared to the far plane. 1.0 matches the [0, 1] range PerspectiveFovLH produces and
		// the clearValue the depth textures were created with, which is what keeps D3D12 on its fast
		// clear path. ClearDepthStencilAttachment is a no-op when the framebuffer has no depth, so this
		// stays correct if a target is ever built without one.
		nvrhi::utils::ClearDepthStencilAttachment(CommandList, SceneContext.Framebuffer, 1.0f, 0);

		for (const std::shared_ptr<IRenderPass>& Pass : Passes)
		{
			if (!IsEditorUIPass(*Pass))
			{
				Pass->Render(SceneContext);
			}
		}

		CommandList->close();
		Device->executeCommandList(CommandList);
	}

	void FRenderer::RenderEditorUI()
	{
		if (!bFrameOpen || EditorUIContext.Framebuffer == nullptr)
		{
			return;
		}

		// A separate submission from the scene stage: the editor samples the viewport target, so the
		// scene writes have to complete first.
		CommandList->open();

		// When the scene went to an offscreen target the back buffer still holds the previous frame,
		// so it needs its own clear. Otherwise the scene stage already cleared it.
		if (bOffscreenEnabled)
		{
			nvrhi::utils::ClearColorAttachment(CommandList, EditorUIContext.Framebuffer, 0, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
		}

		for (const std::shared_ptr<IRenderPass>& Pass : Passes)
		{
			if (IsEditorUIPass(*Pass))
			{
				Pass->Render(EditorUIContext);
			}
		}

		CommandList->close();
		Device->executeCommandList(CommandList);
	}

	void FRenderer::ReleaseFramebufferDependentResources()
	{
		// Pipelines are created against a framebuffer layout, so they pin the back buffer textures.
		// Only the editor UI stage is affected when the scene renders offscreen.
		NotifyEditorUIFramebuffer(nullptr);
		if (!bOffscreenEnabled)
		{
			NotifySceneFramebuffer(nullptr);
		}
	}

	void FRenderer::EndFrame()
	{
		bFrameOpen = false;
	}
} // namespace Lime
