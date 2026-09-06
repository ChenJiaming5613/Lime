// Forward rendering pass with physically based shading.
//
// The BRDF is the one Unreal Engine uses: GGX distribution, height correlated Smith visibility in its
// approximate form, Schlick Fresnel and Lambert diffuse, over a metallic-roughness material. It lives in
// Engine/Shaders/Include/BRDF.hlsli rather than in this pass's shader, so a later deferred path evaluates
// the same one instead of a second implementation that would drift from it.
//
// A sibling of FBlinnPhongForwardLitPass rather than a replacement, sharing its priority: both shade the
// scene into a colour target, and a graph names whichever it wants. Naming both would draw the scene twice.
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
	struct FForwardLitSettings
	{
		// Multiplies the light colour to give the radiance arriving on a surface. Higher than the
		// Blinn-Phong default because a physically based BRDF divides the diffuse term by pi, so the same
		// number produces a dimmer image.
		float LightIntensity = 3.0f;
		// Stands in for image based lighting. Without it, everything facing away from the single light would
		// be black and the model would read as a silhouette rather than as a shape.
		float AmbientStrength = 0.1f;
		// Whether materials that declare a normal map use it. Worth a switch rather than being always on,
		// because it is the control that tells a wrong tangent frame apart from a wrong light: turning it off
		// falls back to the vertex normals, which are independently verifiable.
		bool bEnableNormalMaps = true;
		// Whether the metallic-roughness map is sampled. Off makes every material use its factors alone,
		// which is the quickest way to tell a bad map from bad shading.
		bool bEnableMetallicRoughnessMaps = true;
		// Whether the occlusion map is sampled. Off is useful because occlusion only affects ambient, so its
		// contribution is easy to mistake for something else.
		bool bEnableOcclusionMaps = true;
		bool bEnableEmissive = true;
		bool bEnabled = true;
	};

	class FForwardLitPass final : public TRenderPass<FForwardLitPass>
	{
	public:
		static constexpr ERenderPassPriority Priority = ERenderPassPriority::Scene;

		const char* GetName() const override { return "ForwardLit"; }

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

		FForwardLitSettings& GetSettings() { return Settings; }

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
		FForwardLitSettings Settings;

		// Binding sets keyed by material index.
		//
		// Necessary rather than an optimisation: nvrhi::IDevice::createBindingSet allocates fresh descriptors
		// every call with no caching of its own, and the D3D12 sampler heap is capped at 2048 entries. This
		// pass binds five material textures per set rather than one, so the budget is reached five times
		// sooner and building a set per draw exhausts it almost immediately. Materials are what the set
		// actually varies by, so one entry per material is all that is needed however many draws use it.
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
LIME_REFLECT(Lime::FForwardLitSettings)
{
	LIME_REFLECT_TYPE_NAME("Physically Based Lighting");
	LIME_PROPERTY(bEnabled, Lime::FProp("Enabled"));
	LIME_PROPERTY(LightIntensity, Lime::FProp("Light Intensity").Range(0.0f, 20.0f));
	LIME_PROPERTY(AmbientStrength, Lime::FProp("Ambient").Range(0.0f, 1.0f));
	LIME_PROPERTY(bEnableNormalMaps, Lime::FProp("Normal Maps"));
	LIME_PROPERTY(bEnableMetallicRoughnessMaps, Lime::FProp("Metallic/Roughness Maps"));
	LIME_PROPERTY(bEnableOcclusionMaps, Lime::FProp("Occlusion Maps"));
	LIME_PROPERTY(bEnableEmissive, Lime::FProp("Emissive"));
}
