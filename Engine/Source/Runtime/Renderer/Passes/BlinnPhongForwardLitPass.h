// Forward rendering pass with Blinn-Phong shading.
//
// Named for the technique rather than for what it draws: it is one shading model among several that could
// draw the same scene, and a later deferred or PBR path would be a sibling here rather than a replacement.
//
// Built into the engine rather than supplied by a project, so that a project consisting of nothing but a
// settings file still renders. Registered through RegisterBuiltinRenderPasses() rather than the
// self-registration macro, because this lives in a static library where the linker is free to discard an
// object file whose only content is a registration.

#pragma once

#include "Core/Math/Matrix.h"
#include "Core/Reflection/Reflection.h"
#include "Renderer/RenderTypes.h"

#include "Scene/SceneGpuResources.h"

#include <vector>

namespace Lime
{
	// Lighting values worth tuning at runtime. Reflected so the inspector can build controls for them
	// without this pass knowing anything about the editor.
	struct FBlinnPhongSettings
	{
		// Multiplies the light colour. Zero renders the scene with ambient only.
		float LightIntensity = 3.0f;
		// Flat term standing in for bounced light; without it, faces turned away from the light are black.
		float AmbientStrength = 0.25f;
		// Blinn-Phong exponent. Higher is a tighter, glossier highlight.
		float SpecularPower = 32.0f;
		bool bEnabled = true;
	};

	class FBlinnPhongForwardLitPass final : public TRenderPass<FBlinnPhongForwardLitPass>
	{
	public:
		static constexpr ERenderPassPriority Priority = ERenderPassPriority::Scene;

		const char* GetName() const override { return "BlinnPhongForwardLit"; }

		void Reflect(FRenderGraphPassTypeDesc& OutType) const override;

		bool Initialize(FRenderer& Renderer) override;
		void Shutdown() override;
		bool Compile(FRenderer& Renderer, const FRenderGraphPassResources& Resources) override;
		// Reads the shadow caster's matrix for this frame. Done here rather than in Render, because Render is
		// given no renderer to look the caster up through.
		void OnBeginFrame(FRenderer& Renderer, const FFrameContext& Context) override;
		void Render(const FFrameContext& Context) override;
		void OnFramebufferChanged(nvrhi::IFramebuffer* Framebuffer) override;

		FReflectedRef GetReflectedSettings() override { return MakeReflectedRef(Settings); }

		FBlinnPhongSettings& GetSettings() { return Settings; }

	private:
		bool CreatePipeline(nvrhi::IFramebuffer* Framebuffer);

		nvrhi::IDevice* Device = nullptr;
		nvrhi::ShaderHandle VertexShader;
		nvrhi::ShaderHandle PixelShader;
		nvrhi::InputLayoutHandle InputLayout;
		nvrhi::BindingLayoutHandle BindingLayout;
		nvrhi::GraphicsPipelineHandle Pipeline;
		// Two buffers rather than one: the frame constants are written once per frame while the draw
		// constants change per entity, and a volatile buffer must be written before each draw that reads it.
		nvrhi::BufferHandle FrameConstantBuffer;
		nvrhi::BufferHandle DrawConstantBuffer;

		FSceneGpuResources GpuResources;
		FBlinnPhongSettings Settings;

		// Binding sets keyed by material index.
		//
		// Necessary rather than an optimisation: nvrhi::IDevice::createBindingSet allocates fresh descriptors
		// every call with no caching of its own, and the D3D12 sampler heap is capped at 2048 entries. A scene
		// with a few thousand draws exhausts it within one frame and the device starts failing allocations.
		// Materials are what the binding set actually varies by, so one entry per material is all that is
		// needed however many draws reference it.
		//
		// -1 keys the default material, which is offset by one to keep the index non negative.
		std::vector<nvrhi::BindingSetHandle> MaterialBindingSets;
		// Revision the cache was built for, so it is discarded when the scene changes.
		uint32 CachedSceneRevision = 0;

		// Cached so the pipeline is only rebuilt when the framebuffer layout actually changes.
		nvrhi::IFramebuffer* CurrentFramebuffer = nullptr;

		// The graph's shadow map, or null when no caster is connected. Not owned: the graph allocated it.
		nvrhi::ITexture* ShadowTexture = nullptr;
		// Bound in place of the shadow map when none is connected. A shader cannot declare a resource
		// conditionally, so the binding has to point at something valid; the shader is told to skip the
		// lookup instead.
		nvrhi::TextureHandle FallbackShadowTexture;
		// A comparison sampler, which is what makes SampleCmpLevelZero filter the test results rather than
		// the depths.
		nvrhi::SamplerHandle ShadowSampler;
		// How strongly the lookup darkens, passed to the shader. Zero when nothing is connected.
		float ShadowStrength = 0.0f;
		// The matrix the caster rendered with. Read from the caster rather than recomputed, so the two cannot
		// disagree about where the light was.
		FMatrix4x4 ShadowViewProjection = FMatrix4x4::Identity();

		// Returns the binding set for a material, creating it on first use.
		nvrhi::IBindingSet* GetOrCreateBindingSet(int32 MaterialIndex);
	};
} // namespace Lime

// Reflection must be declared at global scope, so it sits outside the namespace and names the type in
// full.
LIME_REFLECT(Lime::FBlinnPhongSettings)
{
	LIME_REFLECT_TYPE_NAME("Blinn-Phong Lighting");
	LIME_PROPERTY(bEnabled, Lime::FProp("Enabled"));
	LIME_PROPERTY(LightIntensity, Lime::FProp("Light Intensity").Range(0.0f, 10.0f));
	LIME_PROPERTY(AmbientStrength, Lime::FProp("Ambient").Range(0.0f, 1.0f));
	LIME_PROPERTY(SpecularPower, Lime::FProp("Specular Power").Range(1.0f, 128.0f));
}
