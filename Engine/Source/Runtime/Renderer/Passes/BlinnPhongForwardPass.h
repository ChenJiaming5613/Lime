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

#include "Core/Reflection/Reflection.h"
#include "Renderer/RenderTypes.h"
#include "Scene/SceneGpuResources.h"

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

	class FBlinnPhongForwardPass final : public TRenderPass<FBlinnPhongForwardPass>
	{
	public:
		static constexpr ERenderPassPriority Priority = ERenderPassPriority::Scene;

		const char* GetName() const override { return "BlinnPhongForward"; }

		bool Initialize(FRenderer& Renderer) override;
		void Shutdown() override;
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

		// Cached so the pipeline is only rebuilt when the framebuffer layout actually changes.
		nvrhi::IFramebuffer* CurrentFramebuffer = nullptr;
	};

	// Registers every engine provided pass. Called by FEngine before instantiating the registry, so that
	// built-in and project passes end up in one ordered list.
	void RegisterBuiltinRenderPasses();
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
