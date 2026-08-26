// The editor UI as a render pass: Dear ImGui draw lists on top of NVRHI, shared by every backend.
//
// Only imgui_impl_glfw is reused for platform input; drawing lives here so D3D12 and Vulkan run the
// same code path. Implements the 1.92 texture protocol (ImGuiBackendFlags_RendererHasTextures).
//
// A render pass like any other, but not one the scene graph executes. It draws into the swap chain and
// samples what the graph produced, so it has to run after the graph's submission has completed; the
// renderer keeps it as a separate stage for that reason rather than because it is special in kind.
// It declares no graph resources: the target it draws into is the back buffer, which the graph does not
// own.

#pragma once

#include "Renderer/RenderTypes.h"

#include <imgui.h>

#include <unordered_map>

namespace Lime
{
	class FEditorUIPass final : public TRenderPass<FEditorUIPass>
	{
	public:
		// Always drawn last, into the swap chain rather than the scene target.
		static constexpr ERenderPassPriority Priority = ERenderPassPriority::EditorUI;

		const char* GetName() const override { return "EditorUI"; }

		bool Initialize(FRenderer& Renderer) override;
		void Shutdown() override;
		void Render(const FFrameContext& Context) override;
		void OnFramebufferChanged(nvrhi::IFramebuffer* Framebuffer) override;

		// Publishes an engine owned texture so it can be drawn with ImGui::Image. ImGui's own textures
		// arrive through the ImTextureData protocol instead, so the two id ranges are kept apart.
		// Returns ImTextureID_Invalid on failure. Re-registering the same id replaces the binding.
		ImTextureID RegisterTexture(nvrhi::ITexture* Texture, ImTextureID ExistingId = ImTextureID_Invalid);
		void UnregisterTexture(ImTextureID TextureId);

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
		// ImGui managed textures count up from 1; engine owned ones count down from this base so the
		// two allocators can never collide.
		static constexpr ImTextureID ExternalTextureIdBase = 1 << 20;
		ImTextureID NextTextureId = 1;
		ImTextureID NextExternalTextureId = ExternalTextureIdBase;
		uint32 VertexCapacity = 0;
		uint32 IndexCapacity = 0;
	};
} // namespace Lime
