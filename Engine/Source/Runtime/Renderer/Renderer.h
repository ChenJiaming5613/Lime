// Frame orchestration: clears the back buffer, runs the pass list and drives the ImGui backend.

#pragma once

#include "RHI/ShaderLibrary.h"
#include "Renderer/RenderTypes.h"

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

		// Opens the reused command list and clears the back buffer.
		bool BeginFrame(float DeltaSeconds, double TotalSeconds);
		void RenderPasses();
		// Closes and submits the command list.
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

	private:
		IRenderPass* FindPassByTypeId(FRenderPassTypeId TypeId) const;

		IDeviceManager* DeviceManager = nullptr;
		nvrhi::IDevice* Device = nullptr;
		// Reused across frames; NVRHI object creation is not cheap enough to do per frame.
		nvrhi::CommandListHandle CommandList;
		FShaderLibrary ShaderLibrary;
		std::vector<std::shared_ptr<IRenderPass>> Passes;
		FFrameContext FrameContext;
		FVector4 ClearColor{ 0.06f, 0.07f, 0.09f, 1.0f };
		nvrhi::IFramebuffer* LastFramebuffer = nullptr;
		bool bFrameOpen = false;
	};
} // namespace Lime
