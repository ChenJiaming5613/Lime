// Dear ImGui draw list renderer on top of NVRHI, shared by every backend.
//
// Only imgui_impl_glfw is reused for platform input; drawing lives here so D3D12 and Vulkan run the
// same code path. Implements the 1.92 texture protocol (ImGuiBackendFlags_RendererHasTextures).

#pragma once

#include "Renderer/RenderTypes.h"

#include <imgui.h>

#include <unordered_map>

namespace Lime
{
	class FImGuiRenderer final : public IRenderPass
	{
	public:
		bool Initialize(FRenderer& Renderer) override;
		void Shutdown() override;
		void Render(const FFrameContext& Context) override;
		void OnFramebufferChanged(nvrhi::IFramebuffer* Framebuffer) override;

	private:
		struct FTextureEntry
		{
			nvrhi::TextureHandle Texture;
			nvrhi::BindingSetHandle BindingSet;
		};

		bool CreatePipeline(nvrhi::IFramebuffer* Framebuffer);
		// Handles ImGui's create, update and destroy texture requests for the frame.
		void UpdateTextures(nvrhi::ICommandList* CommandList, ImDrawData* DrawData);
		void CreateTexture(nvrhi::ICommandList* CommandList, ImTextureData* TextureData);
		void UploadTexture(nvrhi::ICommandList* CommandList, ImTextureData* TextureData, bool bFullUpload);
		void DestroyTexture(ImTextureData* TextureData);
		// Grows the geometry buffers by 1.5x when needed instead of reallocating every frame.
		bool EnsureGeometryCapacity(uint32 VertexCount, uint32 IndexCount);
		nvrhi::IBindingSet* GetBindingSetForTexture(ImTextureID TextureId);

		nvrhi::IDevice* Device = nullptr;
		nvrhi::ShaderHandle VertexShader;
		nvrhi::ShaderHandle PixelShader;
		nvrhi::InputLayoutHandle InputLayout;
		nvrhi::BindingLayoutHandle BindingLayout;
		nvrhi::GraphicsPipelineHandle Pipeline;
		nvrhi::SamplerHandle Sampler;
		nvrhi::BufferHandle VertexBuffer;
		nvrhi::BufferHandle IndexBuffer;
		nvrhi::BufferHandle ConstantBuffer;

		std::unordered_map<ImTextureID, FTextureEntry> Textures;
		ImTextureID NextTextureId = 1;
		uint32 VertexCapacity = 0;
		uint32 IndexCapacity = 0;
	};
} // namespace Lime
