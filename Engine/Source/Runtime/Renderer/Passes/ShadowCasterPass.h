// Renders scene depth from the light's point of view, for the lit pass to sample as a shadow map.
//
// Depth only: no colour target and no pixel shader, because the depth buffer is the entire output. That
// also means alpha cutout materials cast solid shadows, which is a limitation rather than an oversight —
// discarding would need a pixel shader on every caster.
//
// The light's frustum is derived from the scene bounds every frame rather than configured. A fixed
// frustum has to be large enough for the biggest scene, which wastes resolution on every smaller one;
// fitting it to the bounds keeps the texels where the geometry is.

#pragma once

#include "Core/Math/Matrix.h"
#include "Core/Reflection/Reflection.h"
#include "Renderer/RenderTypes.h"
#include "Scene/SceneGpuResources.h"

namespace Lime
{
	struct FShadowCasterSettings
	{
		// Pushes the shadow towards the surface to hide self shadowing. Depth precision means a surface
		// tests as very slightly in front of itself, which appears as bands of shadow on lit faces.
		float DepthBias = 0.0015f;
		// Scales the bias with the surface's slope. A face nearly edge-on to the light covers far more depth
		// per texel, so a constant bias that suits flat faces is not enough for those.
		float SlopeScaledBias = 2.0f;
		bool bEnabled = true;
	};

	class FShadowCasterPass final : public TRenderPass<FShadowCasterPass>
	{
	public:
		// Ahead of the lit pass, which reads what this produces. The graph decides the real order; this only
		// matters for the passes not driven by one.
		static constexpr ERenderPassPriority Priority = ERenderPassPriority::Background;

		// Square, and fixed rather than following the viewport. A shadow map's resolution is about how much
		// of the world one texel covers, which has nothing to do with the size of the window.
		static constexpr uint32 ShadowMapSize = 2048;

		const char* GetName() const override { return "ShadowCaster"; }

		void Reflect(FRenderGraphPassTypeDesc& OutType) const override;

		bool Initialize(FRenderer& Renderer) override;
		void Shutdown() override;
		bool Compile(FRenderer& Renderer, const FRenderGraphPassResources& Resources) override;
		void Render(const FFrameContext& Context) override;

		FReflectedRef GetReflectedSettings() override { return MakeReflectedRef(Settings); }

		FShadowCasterSettings& GetSettings() { return Settings; }

		// World to light clip space, as used for the most recent frame. The lit pass needs the same matrix to
		// look a fragment up in the shadow map, and recomputing it there would risk the two disagreeing.
		const FMatrix4x4& GetLightViewProjection() const { return LightViewProjection; }

	private:
		bool CreatePipeline(nvrhi::IFramebuffer* Framebuffer);
		// Fits an orthographic frustum around the scene, looking along the light's direction.
		static FMatrix4x4 ComputeLightViewProjection(const FScene& Scene, const FVector3& LightDirection);

		nvrhi::IDevice* Device = nullptr;
		nvrhi::ShaderHandle VertexShader;
		nvrhi::InputLayoutHandle InputLayout;
		nvrhi::BindingLayoutHandle BindingLayout;
		nvrhi::BindingSetHandle BindingSet;
		nvrhi::GraphicsPipelineHandle Pipeline;
		nvrhi::BufferHandle FrameConstantBuffer;
		nvrhi::BufferHandle DrawConstantBuffer;

		// Its own copy of the mesh buffers rather than sharing the lit pass's. Passes are independent in the
		// graph and either may be absent, so one owning the other's resources would couple them.
		FSceneGpuResources GpuResources;
		FShadowCasterSettings Settings;
		FMatrix4x4 LightViewProjection = FMatrix4x4::Identity();

		// Cached so the pipeline is only rebuilt when the target actually changes.
		nvrhi::IFramebuffer* CurrentFramebuffer = nullptr;
	};
} // namespace Lime

LIME_REFLECT(Lime::FShadowCasterSettings)
{
	LIME_REFLECT_TYPE_NAME("Shadow Caster");
	LIME_PROPERTY(bEnabled, Lime::FProp("Enabled"));
	LIME_PROPERTY(DepthBias, Lime::FProp("Depth Bias").Range(0.0f, 0.02f));
	LIME_PROPERTY(SlopeScaledBias, Lime::FProp("Slope Scaled Bias").Range(0.0f, 8.0f));
}
