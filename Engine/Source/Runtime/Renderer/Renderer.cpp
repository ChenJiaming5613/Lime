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

		// Released before the passes, since the plan holds the same pass instances and its views hold
		// textures the passes may still be bound to.
		ClearRenderGraph();

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

	bool FRenderer::SetRenderGraph(FRenderGraphCompileResult Compiled)
	{
		if (!Compiled.bSucceeded)
		{
			// Kept rather than discarded. Nothing runs either way, but a caller asking why gets the issues
			// instead of an empty result that looks like no graph was ever configured.
			SceneGraphPlan.Reset();
			SceneGraphPlan.Compiled = std::move(Compiled);
			SceneGraphResources.Release();
			GraphResourceWidth = 0;
			GraphResourceHeight = 0;
			return false;
		}

		// Resolved against the passes this build actually has. A compiled graph proves the file was
		// consistent with the reflected types, not that an instance exists to run each entry, and those can
		// differ: a pass whose Initialize failed was never added.
		std::vector<std::shared_ptr<IRenderPass>> Resolved;
		Resolved.reserve(Compiled.ExecutionOrder.size());

		for (const FCompiledPass& CompiledPass : Compiled.ExecutionOrder)
		{
			std::shared_ptr<IRenderPass> Match;
			for (const std::shared_ptr<IRenderPass>& Candidate : Passes)
			{
				if (CompiledPass.TypeName == Candidate->GetTypeName())
				{
					Match = Candidate;
					break;
				}
			}

			if (Match == nullptr)
			{
				// The previous plan is left alone. Replacing half of it would leave the renderer executing a
				// graph that matches neither what was asked for nor what it had.
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Render graph names pass type '{}', which this build has no instance of",
				               CompiledPass.TypeName);
				return false;
			}

			Resolved.push_back(std::move(Match));
		}

		SceneGraphPlan.Compiled = std::move(Compiled);
		SceneGraphPlan.Passes = std::move(Resolved);

		// Forces a rebuild on the next frame, when the target size is known. Allocating here would need a
		// size that BeginFrame has not settled yet.
		SceneGraphResources.Release();
		GraphResourceWidth = 0;
		GraphResourceHeight = 0;

		LIME_LOG_INFO(LIME_LOG_CATEGORY_RENDERER, "Render graph set with {} pass(es)", SceneGraphPlan.Passes.size());
		return true;
	}

	void FRenderer::ClearRenderGraph()
	{
		SceneGraphPlan.Reset();
		SceneGraphResources.Release();
		GraphResourceWidth = 0;
		GraphResourceHeight = 0;
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
		SceneContext.Camera = Camera;
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
		if (!bFrameOpen)
		{
			return;
		}

		// Without a compiled graph the viewport is cleared and nothing else happens. Clearing rather than
		// leaving it is what makes the fallback state unambiguous: a stale image would look like a frozen
		// scene, where black is obviously nothing at all.
		if (!SceneGraphPlan.IsRunnable())
		{
			if (SceneContext.Framebuffer != nullptr)
			{
				CommandList->open();
				nvrhi::utils::ClearColorAttachment(CommandList, SceneContext.Framebuffer, 0, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
				CommandList->close();
				Device->executeCommandList(CommandList);
			}
			return;
		}

		// The resources follow the target size, so a resize has to rebuild them before anything is drawn
		// into them. Done here rather than in BeginFrame because it depends on the size that stage settles.
		const uint32 TargetWidth = SceneContext.ViewportWidth;
		const uint32 TargetHeight = SceneContext.ViewportHeight;
		if (!SceneGraphResources.IsValid() || GraphResourceWidth != TargetWidth || GraphResourceHeight != TargetHeight)
		{
			if (!AllocateRenderGraphResources())
			{
				return;
			}
		}

		CommandList->open();

		// Clears happen per pass inside the executor, against each pass's own attachments. A single clear
		// here would only cover one of the graph's targets.
		ExecuteRenderGraph(SceneGraphPlan, SceneGraphResources, SceneContext, ClearColor);

		PresentRenderGraphOutput();

		CommandList->close();
		Device->executeCommandList(CommandList);
	}

	bool FRenderer::AllocateRenderGraphResources()
	{
		if (!SceneGraphPlan.IsRunnable())
		{
			return false;
		}

		const uint32 TargetWidth = SceneContext.ViewportWidth;
		const uint32 TargetHeight = SceneContext.ViewportHeight;
		if (!SceneGraphResources.Allocate(Device, SceneGraphPlan.Compiled, TargetWidth, TargetHeight))
		{
			return false;
		}

		GraphResourceWidth = TargetWidth;
		GraphResourceHeight = TargetHeight;

		// Each pass is compiled against the resources it will actually see, which is the only point at which
		// the formats and sizes are known. A pass failing here drops the whole graph rather than running with
		// one pass unable to draw: a silently missing pass is harder to notice than a blank viewport.
		for (SizeType Index = 0; Index < SceneGraphPlan.Passes.size(); ++Index)
		{
			const FRenderGraphPassView* View = SceneGraphResources.FindView(Index);
			if (View == nullptr)
			{
				continue;
			}

			if (!SceneGraphPlan.Passes[Index]->Compile(*this, *View))
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Render graph pass '{}' failed to compile; the scene graph is disabled",
				               SceneGraphPlan.Compiled.ExecutionOrder[Index].PassName);
				ClearRenderGraph();
				return false;
			}
		}

		return true;
	}

	void FRenderer::PresentRenderGraphOutput()
	{
		// Only meaningful offscreen: when the scene renders straight to the back buffer there is no viewport
		// texture for the editor to sample.
		if (!bOffscreenEnabled || !ViewportTarget.IsValid())
		{
			return;
		}

		nvrhi::ITexture* Output = SceneGraphResources.FindOutputTexture(0);
		nvrhi::ITexture* Destination = ViewportTarget.GetTexture();
		if (Output == nullptr || Destination == nullptr)
		{
			return;
		}

		// A copy needs both sides to agree on format and size. They can disagree legitimately: the graph may
		// end in a float target while the viewport is 8 bit, and the sizes differ for a frame after a
		// resize. Skipped rather than converted, since a conversion is a pass and belongs in the graph.
		const nvrhi::TextureDesc& SourceDesc = Output->getDesc();
		const nvrhi::TextureDesc& DestinationDesc = Destination->getDesc();
		if (SourceDesc.format != DestinationDesc.format || SourceDesc.width != DestinationDesc.width ||
		    SourceDesc.height != DestinationDesc.height)
		{
			// Reported, because the visible result is a black viewport on a graph that compiled and is
			// running — which looks like a rendering bug rather than a mismatch that was skipped on purpose.
			//
			// A size difference lasts a frame after a resize and is not worth mentioning; a format difference
			// never resolves on its own, so only that is logged. Once per offending format rather than per
			// frame, or the log fills at frame rate while the viewport shows nothing.
			if (SourceDesc.format != DestinationDesc.format && SourceDesc.format != ReportedPresentFormatMismatch)
			{
				ReportedPresentFormatMismatch = SourceDesc.format;
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_RENDERER,
				                 "The graph output '{}' is {} but the viewport is {}, so it cannot be copied and the viewport will stay "
				                 "black. End the graph in a pass that writes the viewport's format.",
				                 SourceDesc.debugName, nvrhi::utils::FormatToString(SourceDesc.format),
				                 nvrhi::utils::FormatToString(DestinationDesc.format));
			}
			return;
		}

		ReportedPresentFormatMismatch = nvrhi::Format::UNKNOWN;
		CommandList->copyTexture(Destination, nvrhi::TextureSlice(), Output, nvrhi::TextureSlice());
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
