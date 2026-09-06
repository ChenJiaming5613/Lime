#include "Renderer/Renderer.h"
#include "Renderer/RenderPassRegistry.h"

#include "Core/Logging/LogManager.h"
#include "Core/Reflection/JsonArchive.h"
#include "Platform/PlatformPaths.h"
#include "RHI/DeviceManager.h"

#include <nvrhi/utils.h>

#include <algorithm>
#include <string>
#include <unordered_map>

namespace Lime
{
	FRenderer::FRenderer() : ViewportTargets{ std::vector<FViewportTarget>(1) } {}

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

		// Released before the long lived passes, since the plan's resources hold textures the graph
		// passes may still be bound to.
		ClearRenderGraph();

		// Passes are torn down before the command list so their handles are released first.
		for (auto Iterator = Passes.rbegin(); Iterator != Passes.rend(); ++Iterator)
		{
			(*Iterator)->Shutdown();
		}
		Passes.clear();

		ViewportResizedDelegate = nullptr;
		for (auto& ViewportTarget : ViewportTargets)
		{
			ViewportTarget.Shutdown();
		}
		bOffscreenEnabled = false;

		CommandList = nullptr;
		ShaderLibrary.Shutdown();
		Device = nullptr;
		DeviceManager = nullptr;
		LastFramebuffer = nullptr;
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

		bool Failed = false;
		for (auto& ViewportTarget : ViewportTargets)
		{
			// Same format as the back buffer, so the UI pass samples it without a conversion.
			if (!ViewportTarget.Initialize(Device, DeviceManager->GetBackBufferFormat(), Width, Height))
			{
				Failed = true;
				break;
			}

			LIME_LOG_INFO(LIME_LOG_CATEGORY_RENDERER, "Scene renders into the viewport target ({}x{})", ViewportTarget.GetWidth(),
			   ViewportTarget.GetHeight());
		}

		if (Failed) return false;

		bOffscreenEnabled = true;
		// The graph's resources follow the viewport size rather than the back buffer's, so they have to be
		// rebuilt on the next frame.
		GraphResourceWidth = 0;
		GraphResourceHeight = 0;
		return true;
	}

	void FRenderer::DisableOffscreenRendering()
	{
		if (!bOffscreenEnabled)
		{
			return;
		}

		if (Device != nullptr)
		{
			Device->waitForIdle();
		}
		for (auto& ViewportTarget : ViewportTargets)
		{
			ViewportTarget.Shutdown();
		}
		bOffscreenEnabled = false;
		GraphResourceWidth = 0;
		GraphResourceHeight = 0;
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

		// A pass added after the first frame must not wait for a framebuffer change to build its state.
		if (LastFramebuffer != nullptr)
		{
			Pass->OnFramebufferChanged(LastFramebuffer);
		}

		Passes.push_back(std::move(Pass));
		return true;
	}

	bool FRenderer::SetRenderGraph(FRenderGraphCompileResult Compiled)
	{
		if (!Compiled.bSucceeded)
		{
			// Kept rather than discarded. Nothing runs either way, but a caller asking why gets the issues
			// instead of an empty result that looks like no graph was ever configured. The previous plan
			// is torn down so its passes are shut down properly.
			ClearRenderGraph();
			GraphPlan.Compiled = std::move(Compiled);
			return false;
		}

		// One instance per compiled entry, created from the registry's factory. Sharing a single
		// instance across graph instances of the same type aliases their per-source state (a binding
		// set, a pipeline) and the second instance ends up rendering with the first one's resources,
		// which is the bug this resolves. A graph that names an unregistered pass fails here rather
		// than silently substituting.
		std::unordered_map<std::string, FRenderPassRegistration::FFactory> FactoriesByType;
		for (const FRenderPassRegistration& Registration : FRenderPassRegistry::Get().GetRegistrations())
		{
			if (Registration.Factory != nullptr && Registration.Name != nullptr)
			{
				FactoriesByType.emplace(Registration.Name, Registration.Factory);
			}
		}

		std::vector<std::shared_ptr<IRenderPass>> Resolved;
		Resolved.reserve(Compiled.ExecutionOrder.size());

		// Only the instances this call created are torn down on failure. An injected entry is bound to a
		// pass the renderer already owns, and shutting that down would take the editor's ImGui state with
		// it over a graph that merely failed to load.
		std::vector<std::shared_ptr<IRenderPass>> Created;

		const auto Unwind = [&Created]
		{
			for (auto& Pass : Created)
			{
				Pass->Shutdown();
			}
		};

		for (const FCompiledPass& CompiledPass : Compiled.ExecutionOrder)
		{
			// An injected pass is the engine's own long lived instance. Rebuilding it from the factory would
			// give the graph a second editor UI pass with no ImGui state, while the one holding every
			// registered texture id sat unused.
			if (CompiledPass.bInjected)
			{
				std::shared_ptr<IRenderPass> Existing;
				for (const std::shared_ptr<IRenderPass>& Candidate : Passes)
				{
					if (CompiledPass.TypeName == Candidate->GetTypeName())
					{
						Existing = Candidate;
						break;
					}
				}

				if (Existing != nullptr)
				{
					Resolved.push_back(std::move(Existing));
					continue;
				}
				// Falls through to the factory: an injected type the renderer does not hold is still
				// constructible, which is what lets the blit be injected without being registered as
				// permanent.
			}

			const auto FactoryIt = FactoriesByType.find(CompiledPass.TypeName);
			if (FactoryIt == FactoriesByType.end() || FactoryIt->second == nullptr)
			{
				// The previous plan is left alone. Replacing half of it would leave the renderer
				// executing a graph that matches neither what was asked for nor what it had.
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Render graph names pass type '{}', which this build has no factory for",
				      CompiledPass.TypeName);
				Unwind();
				return false;
			}

			std::shared_ptr<IRenderPass> Pass = FactoryIt->second();
			if (Pass == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Factory for pass type '{}' returned null", CompiledPass.TypeName);
				Unwind();
				return false;
			}

			if (!Pass->Initialize(*this))
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Render pass '{}' failed to initialize and was not retained", Pass->GetName());
				Pass->Shutdown();
				Unwind();
				return false;
			}

			if (LastFramebuffer != nullptr)
			{
				Pass->OnFramebufferChanged(LastFramebuffer);
			}

			// Parameter overrides carried by the compiled entry are applied to the freshly initialized
			// instance through reflection. Missing fields keep the pass defaults, and a value that does
			// not fit its field is ignored with a warning.
			if (!CompiledPass.Settings.is_null() && CompiledPass.Settings.is_object())
			{
				const FReflectedRef Ref = Pass->GetReflectedSettings();
				if (Ref.IsValid())
				{
					FJsonArchive::LoadFields(Ref.Type, Ref.Instance, CompiledPass.Settings);
				}
			}

			Created.push_back(Pass);
			Resolved.push_back(std::move(Pass));
		}

		// Install only after the new instances are fully built, so a failure leaves the old plan
		// untouched.
		ClearRenderGraph();
		GraphPlan.Compiled = std::move(Compiled);
		GraphPlan.Passes = std::move(Resolved);

		// Forces a rebuild on the next frame, when the target size is known. Allocating here would need a
		// size that BeginFrame has not settled yet.
		GraphResources.Release();
		GraphResourceWidth = 0;
		GraphResourceHeight = 0;

		LIME_LOG_INFO(LIME_LOG_CATEGORY_RENDERER, "Render graph set with {} pass(es)", GraphPlan.Passes.size());
		return true;
	}

	void FRenderer::ClearRenderGraph()
	{
		// Passes torn down in reverse so anything a later pass holds is released first, then the
		// resources that referenced them.
		//
		// An injected pass is skipped: the renderer owns it through Passes and shuts it down there. Tearing
		// it down here would leave the editor holding texture ids against a pass that had released them.
		const std::vector<FCompiledPass>& Order = GraphPlan.Compiled.ExecutionOrder;
		for (SizeType Reverse = GraphPlan.Passes.size(); Reverse > 0; --Reverse)
		{
			const SizeType Index = Reverse - 1;
			const bool bInjected = Index < Order.size() && Order[Index].bInjected;
			const bool bOwnedElsewhere =
			    bInjected && std::any_of(Passes.begin(), Passes.end(),
			      [&](const std::shared_ptr<IRenderPass>& Candidate) { return Candidate == GraphPlan.Passes[Index]; });

			if (!bOwnedElsewhere)
			{
				GraphPlan.Passes[Index]->Shutdown();
			}
		}

		GraphPlan.Reset();
		GraphResources.Release();
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
		// Graph passes live on the plan, not in the long lived list. The long lived list is searched
		// first so an injected pass resolves to the instance the engine owns.
		for (const std::shared_ptr<IRenderPass>& Pass : GraphPlan.Passes)
		{
			if (Pass->GetTypeId() == TypeId)
			{
				return Pass.get();
			}
		}
		return nullptr;
	}

	IRenderPass* FRenderer::FindGraphPass(std::string_view PassName) const
	{
		const std::vector<FCompiledPass>& Order = GraphPlan.Compiled.ExecutionOrder;
		const SizeType Count = std::min(Order.size(), GraphPlan.Passes.size());
		for (SizeType Index = 0; Index < Count; ++Index)
		{
			if (Order[Index].PassName == PassName)
			{
				return GraphPlan.Passes[Index].get();
			}
		}
		return nullptr;
	}

	std::vector<std::pair<std::string_view, IRenderPass*>> FRenderer::GetGraphPasses() const
	{
		std::vector<std::pair<std::string_view, IRenderPass*>> Result;
		const std::vector<FCompiledPass>& Order = GraphPlan.Compiled.ExecutionOrder;
		const SizeType Count = std::min(Order.size(), GraphPlan.Passes.size());
		Result.reserve(Count);
		for (SizeType Index = 0; Index < Count; ++Index)
		{
			Result.emplace_back(Order[Index].PassName, GraphPlan.Passes[Index].get());
		}
		return Result;
	}

	void FRenderer::NotifyFramebufferChanged(nvrhi::IFramebuffer* Framebuffer)
	{
		for (const std::shared_ptr<IRenderPass>& Pass : Passes)
		{
			Pass->OnFramebufferChanged(Framebuffer);
		}
		for (const std::shared_ptr<IRenderPass>& Pass : GraphPlan.Passes)
		{
			// An injected pass appears in both lists; notifying it twice would drop the state it had just
			// rebuilt from the first call.
			const bool bAlreadyNotified =
			    std::any_of(Passes.begin(), Passes.end(), [&Pass](const std::shared_ptr<IRenderPass>& Candidate) { return Candidate == Pass; });
			if (!bAlreadyNotified)
			{
				Pass->OnFramebufferChanged(Framebuffer);
			}
		}
		LastFramebuffer = Framebuffer;
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
		if (bOffscreenEnabled && GetMainViewportTarget().ApplyPendingResize())
		{
			// The graph's textures follow the viewport, so they are rebuilt before anything draws.
			GraphResourceWidth = 0;
			GraphResourceHeight = 0;
			if (ViewportResizedDelegate != nullptr)
			{
				ViewportResizedDelegate(GetMainViewportTarget());
			}
		}

		FrameContext.DeltaSeconds = DeltaSeconds;
		FrameContext.TotalSeconds = TotalSeconds;
		// The graph resolves each pass's framebuffer from its own attachments; this is the fallback for a
		// pass that declared none.
		FrameContext.Framebuffer = BackBuffer;
		FrameContext.CommandList = CommandList;
		FrameContext.Scene = Scene;
		FrameContext.Camera = Camera;
		FrameContext.bIsOffscreen = bOffscreenEnabled;
		FrameContext.ViewportWidth = bOffscreenEnabled ? GetMainViewportTarget().GetWidth() : DeviceManager->GetBackBufferWidth();
		FrameContext.ViewportHeight = bOffscreenEnabled ? GetMainViewportTarget().GetHeight() : DeviceManager->GetBackBufferHeight();

		// Passes get to update renderer state, notably the clear colour, before anything is cleared.
		for (const std::shared_ptr<IRenderPass>& Pass : Passes)
		{
			Pass->OnBeginFrame(*this, FrameContext);
		}
		for (const std::shared_ptr<IRenderPass>& Pass : GraphPlan.Passes)
		{
			const bool bAlreadyCalled =
			    std::any_of(Passes.begin(), Passes.end(), [&Pass](const std::shared_ptr<IRenderPass>& Candidate) { return Candidate == Pass; });
			if (!bAlreadyCalled)
			{
				Pass->OnBeginFrame(*this, FrameContext);
			}
		}

		bFrameOpen = true;
		return true;
	}

	void FRenderer::BindImportedResources()
	{
		if (DeviceManager == nullptr)
		{
			return;
		}

		// Taken from the current framebuffer's first colour attachment rather than through a dedicated
		// device manager call: the swap chain already exposes the framebuffer, and adding a parallel
		// accessor would give two ways to name the same texture that could disagree after a resize.
		nvrhi::ITexture* BackBuffer = nullptr;
		if (nvrhi::IFramebuffer* Framebuffer = DeviceManager->GetCurrentFramebuffer(); Framebuffer != nullptr)
		{
			const nvrhi::FramebufferDesc& Desc = Framebuffer->getDesc();
			if (!Desc.colorAttachments.empty())
			{
				BackBuffer = Desc.colorAttachments[0].texture;
			}
		}

		// Rebound every frame rather than once, because the swap chain rotates: a texture captured at
		// allocation time would be the wrong back buffer within two frames.
		GraphResources.BindImportedTexture(BackBufferImport, BackBuffer);
	}

	void FRenderer::RenderFrame()
	{
		if (!bFrameOpen)
		{
			return;
		}

		// Without a runnable plan there is nothing at all to draw, not even the editor: the UI pass is part
		// of the plan now. Clearing the back buffer is what keeps the fallback unambiguous, since a stale
		// image would look like a frozen frame where black is obviously nothing.
		if (!GraphPlan.IsRunnable())
		{
			if (nvrhi::IFramebuffer* BackBuffer = DeviceManager->GetCurrentFramebuffer(); BackBuffer != nullptr)
			{
				CommandList->open();
				nvrhi::utils::ClearColorAttachment(CommandList, BackBuffer, 0, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
				CommandList->close();
				Device->executeCommandList(CommandList);
			}
			return;
		}

		// The resources follow the target size, so a resize has to rebuild them before anything is drawn
		// into them. Done here rather than in BeginFrame because it depends on the size that stage settles.
		const uint32 TargetWidth = FrameContext.ViewportWidth;
		const uint32 TargetHeight = FrameContext.ViewportHeight;
		if (!GraphResources.IsValid() || GraphResourceWidth != TargetWidth || GraphResourceHeight != TargetHeight)
		{
			if (!AllocateRenderGraphResources())
			{
				return;
			}
		}

		BindImportedResources();

		CommandList->open();

		// Clears happen per pass inside the executor, against each pass's own attachments and according to
		// the load action the compile resolved for each one.
		//
		// The present copies run as part of the same walk: a pass that has to observe them asks for a
		// submission boundary, which is the callback below. Everything else stays in one command list, where
		// nvrhi's own tracking covers the write-then-read.
		const FRenderGraphSubmitFunc Submit = [this]
		{
			// The viewport targets are what the editor samples, so they have to hold this frame's scene
			// before the UI pass reads them. Copied at the boundary rather than after the walk, because
			// after would be a frame late for anything drawing them.
			for (SizeType Slot = 0; Slot < GraphResources.GetOutputCount(); ++Slot)
			{
				PresentRenderGraphOutput(Slot);
			}

			CommandList->close();
			Device->executeCommandList(CommandList);
			CommandList->open();
		};

		ExecuteRenderGraph(GraphPlan, GraphResources, FrameContext, ClearColor, Submit);

		// Run again for the frames where no pass asked for a boundary, so a project without an editor still
		// gets its output into the viewport target.
		for (SizeType Slot = 0; Slot < GraphResources.GetOutputCount(); ++Slot)
		{
			PresentRenderGraphOutput(Slot);
		}

		CommandList->close();
		Device->executeCommandList(CommandList);
	}

	bool FRenderer::AllocateRenderGraphResources()
	{
		if (!GraphPlan.IsRunnable())
		{
			return false;
		}

		const uint32 TargetWidth = FrameContext.ViewportWidth;
		const uint32 TargetHeight = FrameContext.ViewportHeight;
		if (!GraphResources.Allocate(Device, GraphPlan.Compiled, TargetWidth, TargetHeight))
		{
			return false;
		}

		GraphResourceWidth = TargetWidth;
		GraphResourceHeight = TargetHeight;

		// Bound before Compile, since a pass may build a binding set against an imported target and would
		// otherwise see null.
		BindImportedResources();

		// Each pass is compiled against the resources it will actually see, which is the only point at which
		// the formats and sizes are known. A pass failing here drops the whole graph rather than running with
		// one pass unable to draw: a silently missing pass is harder to notice than a blank viewport.
		for (SizeType Index = 0; Index < GraphPlan.Passes.size(); ++Index)
		{
			const FRenderGraphPassView* View = GraphResources.FindView(Index);
			if (View == nullptr)
			{
				continue;
			}

			if (!GraphPlan.Passes[Index]->Compile(*this, *View))
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Render graph pass '{}' failed to compile; the graph is disabled",
				      GraphPlan.Compiled.ExecutionOrder[Index].PassName);
				ClearRenderGraph();
				return false;
			}
		}

		// One viewport target per graph output, so a secondary viewport panel has something to display.
		//
		// Every target is (re)created at the main viewport's size, not just the ones that are new. They all
		// have to match the graph's textures or PresentRenderGraphOutput skips the copy as a size mismatch,
		// and a target left at the size it was first made would then never receive an image again: the
		// panel would show its initial contents forever, which reads as a pass that stopped working rather
		// than as a target that was never resized.
		//
		// Only the main target is driven by a panel. The rest follow it, which is what SubmitViewportSize
		// relies on: feeding several sizes back would make resizing one panel rebuild the whole graph.
		const SizeType Count = GraphResources.GetOutputCount();
		if (bOffscreenEnabled && Count > 1)
		{
			const uint32 MainWidth = GetMainViewportTarget().GetWidth();
			const uint32 MainHeight = GetMainViewportTarget().GetHeight();

			ViewportTargets.resize(Count);

			// From 1: index 0 is the main target, which the editor owns and which already carries the size
			// the panel asked for. Re-initializing it here would drop the texture the editor's ImGui binding
			// set still names.
			for (SizeType Index = 1; Index < Count; ++Index)
			{
				if (!FViewportTarget::NeedsResize(ViewportTargets[Index].GetWidth(), ViewportTargets[Index].GetHeight(), MainWidth,
				   MainHeight))
				{
					continue;
				}

				// Same format as the back buffer, so the UI pass samples it without a conversion.
				if (!ViewportTargets[Index].Initialize(Device, DeviceManager->GetBackBufferFormat(), MainWidth, MainHeight))
				{
					continue;
				}

				// The texture object changed, so whatever the editor bound to it has to be rebuilt. The
				// delegate is what refreshes every panel's ImGui binding, so a secondary viewport recovers
				// on the same frame rather than sampling a released texture.
				if (ViewportResizedDelegate != nullptr)
				{
					ViewportResizedDelegate(ViewportTargets[Index]);
				}

				LIME_LOG_TRACE(LIME_LOG_CATEGORY_RENDERER, "Viewport target {} resized to {}x{}", Index,
				      ViewportTargets[Index].GetWidth(), ViewportTargets[Index].GetHeight());
			}
		}

		return true;
	}

	void FRenderer::PresentRenderGraphOutput(SizeType Slot)
	{
		// Only meaningful offscreen: when the graph writes the back buffer directly there is no viewport
		// texture for the editor to sample.
		if (!bOffscreenEnabled || Slot >= ViewportTargets.size() || !ViewportTargets[Slot].IsValid())
		{
			return;
		}

		nvrhi::ITexture* Output = GraphResources.FindOutputTexture(Slot);
		nvrhi::ITexture* Destination = ViewportTargets[Slot].GetTexture();
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

	void FRenderer::ReleaseFramebufferDependentResources()
	{
		// Pipelines are created against a framebuffer layout, so they pin the back buffer textures.
		NotifyFramebufferChanged(nullptr);

		// The graph's framebuffer cache holds references to the back buffer for any pass that writes it, so
		// releasing the pipelines alone would not be enough to let the swap chain be recreated.
		GraphResources.Release();
		GraphResourceWidth = 0;
		GraphResourceHeight = 0;
	}

	void FRenderer::EndFrame()
	{
		bFrameOpen = false;
	}
} // namespace Lime
