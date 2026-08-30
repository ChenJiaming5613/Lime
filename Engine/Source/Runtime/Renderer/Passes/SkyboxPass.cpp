#include "Renderer/Passes/SkyboxPass.h"

#include "Core/Logging/LogManager.h"
#include "Core/Math/Matrix.h"
#include "Platform/PlatformPaths.h"
#include "Renderer/Renderer.h"

#include "Camera/Camera.h"

#include <nvrhi/utils.h>

#include <algorithm>
#include <cfloat>

#include <stb_image.h>

namespace Lime
{
	namespace
	{
		// Must match FSkyboxConstants in Skybox.hlsl: the camera matrices, the viewport size used to turn
		// the pixel position into a scene texture coordinate, and whether the scene is composited.
		struct FConstants
		{
			FMatrix4x4 Projection = FMatrix4x4::Identity();
			FMatrix4x4 View = FMatrix4x4::Identity();
			FVector2 ViewportSize{ 0.0f, 0.0f };
			float bComposite = 0.0f;
			float Exposure = 1.0f;
		};

		// A unit cube's 12 triangles, counter-clockwise seen from outside. The vertex position is the
		// direction it samples, so no UVs or normals are needed.
		static const FVector3 CubeVertices[] = {
			{ -1.0f, 1.0f, -1.0f },  { -1.0f, -1.0f, -1.0f }, { 1.0f, -1.0f, -1.0f },
			{ 1.0f, -1.0f, -1.0f },  { 1.0f, 1.0f, -1.0f },   { -1.0f, 1.0f, -1.0f },

			{ -1.0f, -1.0f, 1.0f },  { -1.0f, -1.0f, -1.0f }, { -1.0f, 1.0f, -1.0f },
			{ -1.0f, 1.0f, -1.0f },  { -1.0f, 1.0f, 1.0f },   { -1.0f, -1.0f, 1.0f },

			{ 1.0f, -1.0f, -1.0f },  { 1.0f, -1.0f, 1.0f },   { 1.0f, 1.0f, 1.0f },
			{ 1.0f, 1.0f, 1.0f },    { 1.0f, 1.0f, -1.0f },   { 1.0f, -1.0f, -1.0f },

			{ -1.0f, -1.0f, 1.0f },  { -1.0f, 1.0f, 1.0f },   { 1.0f, 1.0f, 1.0f },
			{ 1.0f, 1.0f, 1.0f },    { 1.0f, -1.0f, 1.0f },   { -1.0f, -1.0f, 1.0f },

			{ -1.0f, 1.0f, -1.0f },  { 1.0f, 1.0f, -1.0f },   { 1.0f, 1.0f, 1.0f },
			{ 1.0f, 1.0f, 1.0f },    { -1.0f, 1.0f, 1.0f },   { -1.0f, 1.0f, -1.0f },

			{ -1.0f, -1.0f, -1.0f }, { -1.0f, -1.0f, 1.0f },  { 1.0f, -1.0f, -1.0f },
			{ 1.0f, -1.0f, -1.0f },  { -1.0f, -1.0f, 1.0f },  { 1.0f, -1.0f, 1.0f },
		};
		constexpr uint32 CubeVertexCount = sizeof(CubeVertices) / sizeof(CubeVertices[0]);
	} // namespace

	void FSkyboxPass::Reflect(FRenderGraphPassTypeDesc& OutType) const
	{
		OutType.Description = "Composites an equirectangular skybox behind the scene colour.";

		// The scene colour to composite over. Optional: without it the pass is a plain skybox.
		FRenderGraphResourceDesc SceneColorInput = MakeTextureResource("sceneColor", ERenderGraphResourceVisibility::Input);
		SceneColorInput.bOptional = true;
		SceneColorInput.Description = "Scene colour to composite over the skybox.";
		OutType.Inputs.push_back(std::move(SceneColorInput));

		// The scene depth, used to tell foreground from background. Optional: without it the whole frame
		// is treated as background.
		FRenderGraphResourceDesc SceneDepthInput =
		    MakeTextureResource("sceneDepth", ERenderGraphResourceVisibility::Input, nvrhi::Format::D32);
		SceneDepthInput.bOptional = true;
		SceneDepthInput.Description = "Scene depth; pixels at the far plane are background and show the skybox.";
		OutType.Inputs.push_back(std::move(SceneDepthInput));

		FRenderGraphResourceDesc Output = MakeTextureResource("color", ERenderGraphResourceVisibility::Output);
		Output.Description = "Composited skybox and scene colour.";
		OutType.Outputs.push_back(std::move(Output));
	}

	bool FSkyboxPass::Initialize(FRenderer& Renderer)
	{
		Device = Renderer.GetDevice();
		if (Device == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "FSkyboxPass requires a device");
			return false;
		}

		FShaderLibrary& Shaders = Renderer.GetShaderLibrary();
		VertexShader = Shaders.GetShader("Passes/Skybox.hlsl", "MainVS", nvrhi::ShaderType::Vertex);
		PixelShader = Shaders.GetShader("Passes/Skybox.hlsl", "MainPS", nvrhi::ShaderType::Pixel);
		if (VertexShader == nullptr || PixelShader == nullptr)
		{
			return false;
		}

		ConstantBuffer =
		    Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(FConstants), "SkyboxConstants", 16));
		if (ConstantBuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Failed to create the skybox constant buffer");
			return false;
		}

		// The cube is written once on the first draw and reused from then on; its vertices never change.
		const nvrhi::BufferDesc VertexBufferDesc = nvrhi::BufferDesc()
		                                               .setByteSize(sizeof(CubeVertices))
		                                               .setIsVertexBuffer(true)
		                                               .setInitialState(nvrhi::ResourceStates::VertexBuffer)
		                                               .setKeepInitialState(true)
		                                               .setDebugName("SkyboxCube");
		VertexBuffer = Device->createBuffer(VertexBufferDesc);
		if (VertexBuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Failed to create the skybox cube vertex buffer");
			return false;
		}

		const nvrhi::VertexAttributeDesc PositionAttribute = nvrhi::VertexAttributeDesc()
		                                                         .setName("POSITION")
		                                                         .setFormat(nvrhi::Format::RGB32_FLOAT)
		                                                         .setOffset(0)
		                                                         .setElementStride(sizeof(FVector3));
		InputLayout = Device->createInputLayout(&PositionAttribute, 1, VertexShader);
		if (InputLayout == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createInputLayout failed for the skybox pass");
			return false;
		}

		// Clamped so a sample at the horizon does not wrap to the opposite edge, which would put a seam
		// down the sky.
		Sampler = Device->createSampler(
		    nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::ClampToEdge));
		if (Sampler == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createSampler failed for the skybox pass");
			return false;
		}

		// Point filtered so a depth edge is not smeared by interpolation, which would put a band of skybox
		// pixels around every silhouette.
		DepthSampler = Device->createSampler(
		    nvrhi::SamplerDesc().setAllFilters(false).setAllAddressModes(nvrhi::SamplerAddressMode::ClampToEdge));
		if (DepthSampler == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createSampler failed for the skybox depth sampler");
			return false;
		}

		// Stand-ins bound when the optional graph inputs are not connected. A shader cannot declare a
		// resource conditionally, so the binding always has to point at a valid texture; the compositing
		// flag keeps the shader from ever sampling them in that case, so their contents do not matter.
		FallbackSceneColor = Device->createTexture(nvrhi::TextureDesc()
		                                                .setDimension(nvrhi::TextureDimension::Texture2D)
		                                                .setWidth(1)
		                                                .setHeight(1)
		                                                .setFormat(nvrhi::Format::RGBA8_UNORM)
		                                                .setInitialState(nvrhi::ResourceStates::ShaderResource)
		                                                .setKeepInitialState(true)
		                                                .setDebugName("SkyboxFallbackSceneColor"));
		if (FallbackSceneColor == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createTexture failed for the skybox fallback scene colour");
			return false;
		}

		FallbackSceneDepth = Device->createTexture(nvrhi::TextureDesc()
		                                                .setDimension(nvrhi::TextureDimension::Texture2D)
		                                                .setWidth(1)
		                                                .setHeight(1)
		                                                .setFormat(nvrhi::Format::D32)
		                                                .setIsRenderTarget(true)
		                                                .setIsTypeless(true)
		                                                .setInitialState(nvrhi::ResourceStates::ShaderResource)
		                                                .setKeepInitialState(true)
		                                                .setDebugName("SkyboxFallbackSceneDepth"));
		if (FallbackSceneDepth == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createTexture failed for the skybox fallback scene depth");
			return false;
		}

		const nvrhi::BindingLayoutDesc LayoutDesc = MakeBindingLayoutDesc(nvrhi::ShaderType::All)
		                                                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
		                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
		                                                .addItem(nvrhi::BindingLayoutItem::Sampler(0))
		                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1))
		                                                .addItem(nvrhi::BindingLayoutItem::Sampler(1))
		                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(2))
		                                                .addItem(nvrhi::BindingLayoutItem::Sampler(2));

		BindingLayout = Device->createBindingLayout(LayoutDesc);
		if (BindingLayout == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createBindingLayout failed for the skybox pass");
			return false;
		}

		return true;
	}

	void FSkyboxPass::Shutdown()
	{
		Pipeline = nullptr;
		BindingSet = nullptr;
		BindingLayout = nullptr;
		EnvironmentTexture = nullptr;
		FallbackSceneColor = nullptr;
		FallbackSceneDepth = nullptr;
		DepthSampler = nullptr;
		Sampler = nullptr;
		ConstantBuffer = nullptr;
		VertexBuffer = nullptr;
		InputLayout = nullptr;
		PixelShader = nullptr;
		VertexShader = nullptr;
		SceneColor = nullptr;
		SceneDepth = nullptr;
		PreviousSceneColor = nullptr;
		PreviousSceneDepth = nullptr;
		bComposite = false;
		LoadedPath.clear();
		bCubeUploaded = false;
		CurrentFramebuffer = nullptr;
		Device = nullptr;
	}

	bool FSkyboxPass::Compile(FRenderer& Renderer, const FRenderGraphPassResources& Resources)
	{
		LIME_UNUSED(Renderer);

		nvrhi::IFramebuffer* Framebuffer = Resources.GetFramebuffer();
		if (Framebuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "The skybox pass has no framebuffer, so its colour output is not connected");
			return false;
		}

		// The scene inputs are optional. The binding set names specific textures, so it is rebuilt when
		// either input changes (a graph reallocation after a resize).
		nvrhi::ITexture* NewSceneColor = Resources.FindTexture("sceneColor");
		nvrhi::ITexture* NewSceneDepth = Resources.FindTexture("sceneDepth");
		if (NewSceneColor != SceneColor || NewSceneDepth != SceneDepth)
		{
			SceneColor = NewSceneColor;
			SceneDepth = NewSceneDepth;
			bComposite = SceneColor != nullptr && SceneDepth != nullptr;
			BindingSet = nullptr;
		}

		return CreatePipeline(Framebuffer);
	}

	bool FSkyboxPass::CreatePipeline(nvrhi::IFramebuffer* Framebuffer)
	{
		if (Framebuffer == nullptr)
		{
			return false;
		}

		nvrhi::RenderState RenderState;
		// No depth at all: the cube covers the target, and the compositing happens in the pixel shader by
		// reading the scene depth as a texture rather than testing against it.
		RenderState.depthStencilState.depthTestEnable = false;
		RenderState.depthStencilState.depthWriteEnable = false;
		RenderState.depthStencilState.stencilEnable = false;
		// The cube is seen from the inside, where its outward facing triangles wind the other way, so
		// culling is disabled rather than relying on winding.
		RenderState.rasterState.setCullNone();

		const nvrhi::GraphicsPipelineDesc PipelineDesc = nvrhi::GraphicsPipelineDesc()
		                                                     .setPrimType(nvrhi::PrimitiveType::TriangleList)
		                                                     .setInputLayout(InputLayout)
		                                                     .setVertexShader(VertexShader)
		                                                     .setPixelShader(PixelShader)
		                                                     .addBindingLayout(BindingLayout)
		                                                     .setRenderState(RenderState);

		Pipeline = Device->createGraphicsPipeline(PipelineDesc, Framebuffer);
		if (Pipeline == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createGraphicsPipeline failed for the skybox pass");
			return false;
		}

		CurrentFramebuffer = Framebuffer;
		return true;
	}

	bool FSkyboxPass::EnsureEnvironmentLoaded(nvrhi::ICommandList* CommandList)
	{
		// Reload the environment map only when the path changed.
		if (EnvironmentTexture == nullptr || Settings.EnvironmentMapPath != LoadedPath)
		{
			BindingSet = nullptr;
			EnvironmentTexture = nullptr;
			LoadedPath.clear();

			if (Settings.EnvironmentMapPath.empty())
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_RENDERER, "The skybox environment map path is empty");
				return false;
			}

			const std::filesystem::path Resolved = FPlatformPaths::ResolveAssetPath(Settings.EnvironmentMapPath);
			if (Resolved.empty())
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_RENDERER, "The skybox environment map '{}' was not found",
				                 Settings.EnvironmentMapPath);
				return false;
			}

			int Width = 0;
			int Height = 0;
			int Channels = 0;
			// 4 forces RGBA, so the row pitch is uniform and the shader needs no per-channel special case.
			float* Pixels = stbi_loadf(Resolved.string().c_str(), &Width, &Height, &Channels, 4);
			if (Pixels == nullptr)
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_RENDERER, "Could not decode the skybox environment map '{}': {}",
				                 Settings.EnvironmentMapPath, stbi_failure_reason());
				return false;
			}

			// Report what actually decoded, so a black skybox can be told apart from a failed load versus a
			// mapping bug: a real HDR radiance image has values well above 1.0, while a flat or LDR image
			// does not.
			float MinR = FLT_MAX, MaxR = -FLT_MAX;
			float MinG = FLT_MAX, MaxG = -FLT_MAX;
			float MinB = FLT_MAX, MaxB = -FLT_MAX;
			const size_t PixelCount = static_cast<size_t>(Width) * static_cast<size_t>(Height);
			for (size_t Pixel = 0; Pixel < PixelCount; ++Pixel)
			{
				const float* Component = Pixels + Pixel * 4;
				MinR = std::min(MinR, Component[0]);
				MaxR = std::max(MaxR, Component[0]);
				MinG = std::min(MinG, Component[1]);
				MaxG = std::max(MaxG, Component[1]);
				MinB = std::min(MinB, Component[2]);
				MaxB = std::max(MaxB, Component[2]);
			}
			LIME_LOG_INFO(LIME_LOG_CATEGORY_RENDERER,
			              "Decoded skybox HDR '{}': {}x{}, {} source channel(s), {:.1f} MB float, "
			              "R[{:.3f}, {:.3f}] G[{:.3f}, {:.3f}] B[{:.3f}, {:.3f}]",
			              Resolved.string(), Width, Height, Channels,
			              static_cast<double>(PixelCount) * 4 * sizeof(float) / (1024.0 * 1024.0), MinR, MaxR, MinG, MaxG, MinB, MaxB);

			const nvrhi::TextureDesc Desc = nvrhi::TextureDesc()
			                                    .setDimension(nvrhi::TextureDimension::Texture2D)
			                                    .setWidth(static_cast<uint32>(Width))
			                                    .setHeight(static_cast<uint32>(Height))
			                                    .setFormat(nvrhi::Format::RGBA32_FLOAT)
			                                    .setInitialState(nvrhi::ResourceStates::ShaderResource)
			                                    .setKeepInitialState(true)
			                                    .setDebugName("SkyboxEnvironment");

			EnvironmentTexture = Device->createTexture(Desc);
			if (EnvironmentTexture == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createTexture failed for the skybox environment map ({}x{})", Width, Height);
				stbi_image_free(Pixels);
				return false;
			}

			// Four float channels per pixel.
			CommandList->writeTexture(EnvironmentTexture, 0, 0, Pixels, static_cast<size_t>(Width) * 4 * sizeof(float));
			stbi_image_free(Pixels);

			LoadedPath = Settings.EnvironmentMapPath;
		}

		// Build the binding set once the environment map and the scene inputs (set by Compile) are known.
		if (BindingSet == nullptr)
		{
			const nvrhi::BindingSetDesc BindingSetDesc =
			    nvrhi::BindingSetDesc()
			        .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, ConstantBuffer))
			        .addItem(nvrhi::BindingSetItem::Texture_SRV(0, EnvironmentTexture))
			        .addItem(nvrhi::BindingSetItem::Sampler(0, Sampler))
			        .addItem(nvrhi::BindingSetItem::Texture_SRV(1, SceneColor != nullptr ? SceneColor : FallbackSceneColor.Get()))
			        .addItem(nvrhi::BindingSetItem::Sampler(1, Sampler))
			        .addItem(nvrhi::BindingSetItem::Texture_SRV(2, SceneDepth != nullptr ? SceneDepth : FallbackSceneDepth.Get()))
			        .addItem(nvrhi::BindingSetItem::Sampler(2, DepthSampler));
			BindingSet = Device->createBindingSet(BindingSetDesc, BindingLayout);
			if (BindingSet == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createBindingSet failed for the skybox pass");
				return false;
			}
		}

		return true;
	}

	void FSkyboxPass::Render(const FFrameContext& Context)
	{
		if (!Settings.bEnabled || Context.Camera == nullptr || Context.CommandList == nullptr)
		{
			return;
		}

		nvrhi::IFramebuffer* Framebuffer = Context.Resources != nullptr ? Context.Resources->GetFramebuffer() : Context.Framebuffer;
		if (Framebuffer == nullptr)
		{
			return;
		}

		// Rebuilt when the target changed, which is how the pass also runs outside a graph (Compile builds
		// against the graph's framebuffer, and the renderer may later hand it a different one).
		if (Pipeline == nullptr || CurrentFramebuffer != Framebuffer)
		{
			if (!CreatePipeline(Framebuffer))
			{
				return;
			}
		}

		if (!EnsureEnvironmentLoaded(Context.CommandList))
		{
			// The target keeps its clear colour, which is the visible result of a missing or unreadable map.
			return;
		}

		// The cube is static, so it is written once rather than every frame.
		if (!bCubeUploaded)
		{
			Context.CommandList->writeBuffer(VertexBuffer, CubeVertices, sizeof(CubeVertices));
			bCubeUploaded = true;
		}

		FConstants Constants;
		Constants.Projection = Context.Camera->GetProjectionMatrix();
		Constants.View = Context.Camera->GetViewMatrix();
		Constants.ViewportSize = FVector2{ static_cast<float>(Context.ViewportWidth), static_cast<float>(Context.ViewportHeight) };
		Constants.bComposite = bComposite ? 1.0f : 0.0f;
		Constants.Exposure = Settings.Exposure;
		Context.CommandList->writeBuffer(ConstantBuffer, &Constants, sizeof(Constants));

		const nvrhi::ViewportState ViewportState = nvrhi::ViewportState().addViewportAndScissorRect(
		    nvrhi::Viewport(static_cast<float>(Context.ViewportWidth), static_cast<float>(Context.ViewportHeight)));

		nvrhi::GraphicsState GraphicsState;
		GraphicsState.pipeline = Pipeline;
		GraphicsState.framebuffer = Framebuffer;
		GraphicsState.viewport = ViewportState;
		GraphicsState.addBindingSet(BindingSet);
		GraphicsState.addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(VertexBuffer).setSlot(0));
		Context.CommandList->setGraphicsState(GraphicsState);

		nvrhi::DrawArguments DrawArguments;
		DrawArguments.vertexCount = CubeVertexCount;
		Context.CommandList->draw(DrawArguments);
	}
} // namespace Lime
