// Skybox pass: composites an equirectangular environment map behind the scene.
//
// Renders a cube whose vertex positions are the sampling directions, then draws the scene over it: the
// scene colour and depth arrive as graph inputs, and where the depth says the scene wrote geometry the
// scene colour wins, everywhere else the skybox shows. Both inputs are optional — without them the pass is
// a plain skybox, which is how it behaves when dropped into a graph with no forward lit pass feeding it.
//
// The environment map is not a graph resource: it is an asset the pass loads itself (decoded by stb_image
// and uploaded as RGBA32F), reloaded only when the configured path changes.

#pragma once

#include "Core/Reflection/Reflection.h"
#include "Renderer/RenderTypes.h"

#include <string>

namespace Lime
{
	struct FSkyboxSettings
	{
		bool bEnabled = true;
		// Multiplies the skybox radiance before it is written; 1 leaves it unchanged.
		float Exposure = 1.0f;
		// Path to an equirectangular .hdr, resolved through FPlatformPaths::ResolveAssetPath: an absolute
		// path is used as given, a relative one is tried against the project and the shared Assets directory.
		std::string EnvironmentMapPath = "EnvironmentMaps/sunny_country_road_4k.hdr";
	};

	class FSkyboxPass final : public TRenderPass<FSkyboxPass>
	{
	public:
		// Before the scene, so it ends up behind everything when a graph orders passes by stage.
		static constexpr ERenderPassPriority Priority = ERenderPassPriority::Background;

		const char* GetName() const override { return "Skybox"; }

		void Reflect(FRenderGraphPassTypeDesc& OutType) const override;

		bool Initialize(FRenderer& Renderer) override;
		void Shutdown() override;
		bool Compile(FRenderer& Renderer, const FRenderGraphPassResources& Resources) override;
		void Render(const FFrameContext& Context) override;

		FReflectedRef GetReflectedSettings() override { return MakeReflectedRef(Settings); }

		FSkyboxSettings& GetSettings() { return Settings; }

	private:
		bool CreatePipeline(nvrhi::IFramebuffer* Framebuffer);
		// Loads the environment map and uploads it, only when the path changed since the last successful
		// load, and rebuilds the binding set. Returns false when the map is not available.
		bool EnsureEnvironmentLoaded(nvrhi::ICommandList* CommandList);

		nvrhi::IDevice* Device = nullptr;
		nvrhi::ShaderHandle VertexShader;
		nvrhi::ShaderHandle PixelShader;
		nvrhi::BindingLayoutHandle BindingLayout;
		nvrhi::BindingSetHandle BindingSet;
		nvrhi::GraphicsPipelineHandle Pipeline;
		nvrhi::BufferHandle ConstantBuffer;
		// The cube this pass draws; its vertex positions are the sampling directions.
		nvrhi::BufferHandle VertexBuffer;
		nvrhi::InputLayoutHandle InputLayout;
		// Linear sampler for the environment map and the scene colour.
		nvrhi::SamplerHandle Sampler;
		// Point sampler for the scene depth, which must not be interpolated.
		nvrhi::SamplerHandle DepthSampler;
		nvrhi::TextureHandle EnvironmentTexture;
		// 1x1 stand-ins bound when the optional graph inputs are not connected.
		nvrhi::TextureHandle FallbackSceneColor;
		nvrhi::TextureHandle FallbackSceneDepth;

		FSkyboxSettings Settings;

		// The graph's scene colour and depth, or null when not connected. Set by Compile; the binding set
		// is rebuilt when they change, since a binding set names one specific texture.
		nvrhi::ITexture* SceneColor = nullptr;
		nvrhi::ITexture* SceneDepth = nullptr;
		nvrhi::ITexture* PreviousSceneColor = nullptr;
		nvrhi::ITexture* PreviousSceneDepth = nullptr;
		// True when both the scene colour and depth are connected, which enables compositing.
		bool bComposite = false;

		// The path the environment texture was last loaded from, so a change is noticed without reloading
		// every frame.
		std::string LoadedPath;
		// The cube vertices and the fallback textures are written once, on the first draw.
		bool bCubeUploaded = false;
		nvrhi::IFramebuffer* CurrentFramebuffer = nullptr;
	};
} // namespace Lime

LIME_REFLECT(Lime::FSkyboxSettings)
{
	LIME_REFLECT_TYPE_NAME("Skybox");
	LIME_PROPERTY(bEnabled, Lime::FProp("Enabled"));
	LIME_PROPERTY(Exposure, Lime::FProp("Exposure").Range(0.0f, 8.0f));
	LIME_PROPERTY(EnvironmentMapPath,
	              Lime::FProp("Environment Map").Tooltip("Path to an equirectangular .hdr, relative to the Assets directory"));
}
