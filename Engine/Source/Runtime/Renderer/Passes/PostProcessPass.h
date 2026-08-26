// Full screen post process: exposure and a gamma curve over the scene colour.
//
// Owns no geometry. The vertex shader generates a single screen covering triangle from the vertex id, so
// there is no vertex buffer, no input layout and no mesh to keep in sync with the scene.
//
// Reads its input through the render graph rather than from a fixed target, which is what makes it
// reusable: the same pass works wherever the graph puts it in the chain.

#pragma once

#include "Core/Reflection/Reflection.h"
#include "Renderer/RenderTypes.h"

namespace Lime
{
	struct FPostProcessSettings
	{
		// Multiplies the scene colour before the curve. 1 leaves it unchanged.
		float Exposure = 1.0f;
		// The curve applied on the way out. 2.2 approximates sRGB; 1 disables it.
		float Gamma = 2.2f;
		bool bEnabled = true;
	};

	class FPostProcessPass final : public TRenderPass<FPostProcessPass>
	{
	public:
		static constexpr ERenderPassPriority Priority = ERenderPassPriority::PostProcess;

		const char* GetName() const override { return "PostProcess"; }

		void Reflect(FRenderGraphPassTypeDesc& OutType) const override;

		bool Initialize(FRenderer& Renderer) override;
		void Shutdown() override;
		bool Compile(FRenderer& Renderer, const FRenderGraphPassResources& Resources) override;
		void Render(const FFrameContext& Context) override;

		FReflectedRef GetReflectedSettings() override { return MakeReflectedRef(Settings); }

		FPostProcessSettings& GetSettings() { return Settings; }

	private:
		bool CreatePipeline(nvrhi::IFramebuffer* Framebuffer);

		nvrhi::IDevice* Device = nullptr;
		nvrhi::ShaderHandle VertexShader;
		nvrhi::ShaderHandle PixelShader;
		nvrhi::BindingLayoutHandle BindingLayout;
		nvrhi::BindingSetHandle BindingSet;
		nvrhi::GraphicsPipelineHandle Pipeline;
		nvrhi::BufferHandle ConstantBuffer;
		nvrhi::SamplerHandle Sampler;

		FPostProcessSettings Settings;

		// The texture the binding set was built against. A binding set names a specific texture, so the set
		// has to be rebuilt when the graph reallocates its resources after a resize.
		nvrhi::ITexture* BoundInput = nullptr;
		nvrhi::IFramebuffer* CurrentFramebuffer = nullptr;
	};
} // namespace Lime

LIME_REFLECT(Lime::FPostProcessSettings)
{
	LIME_REFLECT_TYPE_NAME("Post Process");
	LIME_PROPERTY(bEnabled, Lime::FProp("Enabled"));
	LIME_PROPERTY(Exposure, Lime::FProp("Exposure").Range(0.0f, 8.0f));
	LIME_PROPERTY(Gamma, Lime::FProp("Gamma").Range(1.0f, 3.0f));
}
