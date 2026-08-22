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

		if (!ShaderLibrary.Initialize(Device, InDeviceManager.GetBackend(), FPlatformPaths::GetShaderDirectory()))
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
		LastUIFramebuffer = nullptr;
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

	bool FRenderer::IsUIPass(const IRenderPass& Pass)
	{
		return static_cast<int32>(Pass.GetPriority()) >= static_cast<int32>(ERenderPassPriority::UI);
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
		nvrhi::IFramebuffer* StageFramebuffer = IsUIPass(*Pass) ? LastUIFramebuffer : LastSceneFramebuffer;
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
			if (!IsUIPass(*Pass))
			{
				Pass->OnFramebufferChanged(Framebuffer);
			}
		}
		LastSceneFramebuffer = Framebuffer;
	}

	void FRenderer::NotifyUIFramebuffer(nvrhi::IFramebuffer* Framebuffer)
	{
		for (const std::shared_ptr<IRenderPass>& Pass : Passes)
		{
			if (IsUIPass(*Pass))
			{
				Pass->OnFramebufferChanged(Framebuffer);
			}
		}
		LastUIFramebuffer = Framebuffer;
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
		if (BackBuffer != LastUIFramebuffer)
		{
			NotifyUIFramebuffer(BackBuffer);
		}

		SceneContext.DeltaSeconds = DeltaSeconds;
		SceneContext.TotalSeconds = TotalSeconds;
		SceneContext.Framebuffer = SceneFramebuffer;
		SceneContext.CommandList = CommandList;
		SceneContext.bIsOffscreen = bOffscreenEnabled;
		SceneContext.ViewportWidth = bOffscreenEnabled ? ViewportTarget.GetWidth() : DeviceManager->GetBackBufferWidth();
		SceneContext.ViewportHeight = bOffscreenEnabled ? ViewportTarget.GetHeight() : DeviceManager->GetBackBufferHeight();

		UIContext = SceneContext;
		UIContext.Framebuffer = BackBuffer;
		UIContext.bIsOffscreen = false;
		UIContext.ViewportWidth = DeviceManager->GetBackBufferWidth();
		UIContext.ViewportHeight = DeviceManager->GetBackBufferHeight();

		// Passes get to update renderer state, notably the clear colour, before anything is cleared.
		for (const std::shared_ptr<IRenderPass>& Pass : Passes)
		{
			Pass->OnBeginFrame(*this, IsUIPass(*Pass) ? UIContext : SceneContext);
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

		for (const std::shared_ptr<IRenderPass>& Pass : Passes)
		{
			if (!IsUIPass(*Pass))
			{
				Pass->Render(SceneContext);
			}
		}

		CommandList->close();
		Device->executeCommandList(CommandList);
	}

	void FRenderer::RenderUI()
	{
		if (!bFrameOpen || UIContext.Framebuffer == nullptr)
		{
			return;
		}

		// A separate submission from the scene stage: the UI samples the viewport target, so the
		// scene writes have to complete first.
		CommandList->open();

		// When the scene went to an offscreen target the back buffer still holds the previous frame,
		// so it needs its own clear. Otherwise the scene stage already cleared it.
		if (bOffscreenEnabled)
		{
			nvrhi::utils::ClearColorAttachment(CommandList, UIContext.Framebuffer, 0, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
		}

		for (const std::shared_ptr<IRenderPass>& Pass : Passes)
		{
			if (IsUIPass(*Pass))
			{
				Pass->Render(UIContext);
			}
		}

		CommandList->close();
		Device->executeCommandList(CommandList);
	}

	void FRenderer::ReleaseFramebufferDependentResources()
	{
		// Pipelines are created against a framebuffer layout, so they pin the back buffer textures.
		// Only the UI stage is affected when the scene renders offscreen.
		NotifyUIFramebuffer(nullptr);
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
