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

		CommandList = nullptr;
		ShaderLibrary.Shutdown();
		Device = nullptr;
		DeviceManager = nullptr;
		LastFramebuffer = nullptr;
		bFrameOpen = false;
	}

	void FRenderer::AddPass(std::shared_ptr<IRenderPass> Pass)
	{
		if (Pass == nullptr)
		{
			return;
		}

		if (!Pass->Initialize(*this))
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Render pass failed to initialize and was not registered");
			return;
		}

		Passes.push_back(std::move(Pass));
	}

	bool FRenderer::BeginFrame(float DeltaSeconds, double TotalSeconds)
	{
		if (Device == nullptr || DeviceManager == nullptr || bFrameOpen)
		{
			return false;
		}

		nvrhi::IFramebuffer* Framebuffer = DeviceManager->GetCurrentFramebuffer();
		if (Framebuffer == nullptr)
		{
			return false;
		}

		// The swap chain may have been recreated; pipelines are tied to a framebuffer layout.
		if (Framebuffer != LastFramebuffer)
		{
			for (const std::shared_ptr<IRenderPass>& Pass : Passes)
			{
				Pass->OnFramebufferChanged(Framebuffer);
			}
			LastFramebuffer = Framebuffer;
		}

		FrameContext.DeltaSeconds = DeltaSeconds;
		FrameContext.TotalSeconds = TotalSeconds;
		FrameContext.ViewportWidth = DeviceManager->GetBackBufferWidth();
		FrameContext.ViewportHeight = DeviceManager->GetBackBufferHeight();
		FrameContext.Framebuffer = Framebuffer;
		FrameContext.CommandList = CommandList;

		CommandList->open();
		nvrhi::utils::ClearColorAttachment(CommandList, Framebuffer, 0,
		                                   nvrhi::Color(ClearColor.X, ClearColor.Y, ClearColor.Z, ClearColor.W));

		bFrameOpen = true;
		return true;
	}

	void FRenderer::RenderPasses()
	{
		if (!bFrameOpen)
		{
			return;
		}

		for (const std::shared_ptr<IRenderPass>& Pass : Passes)
		{
			Pass->Render(FrameContext);
		}
	}

	void FRenderer::ReleaseFramebufferDependentResources()
	{
		// Pipelines are created against a framebuffer layout, so they pin the back buffer textures.
		for (const std::shared_ptr<IRenderPass>& Pass : Passes)
		{
			Pass->OnFramebufferChanged(nullptr);
		}
		LastFramebuffer = nullptr;
	}

	void FRenderer::EndFrame()
	{
		if (!bFrameOpen)
		{
			return;
		}

		CommandList->close();
		Device->executeCommandList(CommandList);
		bFrameOpen = false;
	}
} // namespace Lime
