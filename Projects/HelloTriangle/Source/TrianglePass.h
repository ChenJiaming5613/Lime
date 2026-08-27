// Draws a single rotating triangle. Registers itself, so nothing else has to reference it.

#pragma once

#include "Renderer/RenderTypes.h"

#include "TriangleSettings.h"

namespace HelloTriangle
{
	class FTrianglePass final : public Lime::TRenderPass<FTrianglePass>
	{
	public:
		static constexpr Lime::ERenderPassPriority Priority = Lime::ERenderPassPriority::Scene;

		const char* GetName() const override { return "Triangle"; }

		// Declares what the pass writes, so a render graph can name it and give it a target.
		//
		// Required rather than optional: rendering is driven by the graph, and a pass that describes no
		// outputs has nowhere to draw and would never be executed.
		void Reflect(Lime::FRenderGraphPassTypeDesc& OutType) const override;

		bool Initialize(Lime::FRenderer& Renderer) override;
		void Shutdown() override;
		bool Compile(Lime::FRenderer& Renderer, const Lime::FRenderGraphPassResources& Resources) override;
		void OnBeginFrame(Lime::FRenderer& Renderer, const Lime::FFrameContext& Context) override;
		void Render(const Lime::FFrameContext& Context) override;
		void OnFramebufferChanged(nvrhi::IFramebuffer* Framebuffer) override;

		// Makes the settings visible to the editor's generic inspector.
		Lime::FReflectedRef GetReflectedSettings() override { return Lime::MakeReflectedRef(Settings); }

		FTriangleSettings& GetSettings() { return Settings; }
		float GetRotationRadians() const { return RotationRadians; }

	private:
		bool CreatePipeline(nvrhi::IFramebuffer* Framebuffer);

		nvrhi::IDevice* Device = nullptr;
		nvrhi::ShaderHandle VertexShader;
		nvrhi::ShaderHandle PixelShader;
		nvrhi::InputLayoutHandle InputLayout;
		nvrhi::BufferHandle VertexBuffer;
		nvrhi::BufferHandle ConstantBuffer;
		nvrhi::BindingLayoutHandle BindingLayout;
		nvrhi::BindingSetHandle BindingSet;
		nvrhi::GraphicsPipelineHandle Pipeline;

		FTriangleSettings Settings;
		float RotationRadians = 0.0f;
	};
} // namespace HelloTriangle
