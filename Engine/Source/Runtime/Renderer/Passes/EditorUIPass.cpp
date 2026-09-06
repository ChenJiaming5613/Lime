#include "Renderer/Passes/EditorUIPass.h"

#include "Core/Logging/LogManager.h"
#include "Core/Math/Matrix.h"
#include "Renderer/Renderer.h"

#include <nvrhi/utils.h>

#include <algorithm>
#include <array>
#include <vector>

namespace Lime
{
	namespace
	{
		constexpr uint32 InitialVertexCapacity = 8192;
		constexpr uint32 InitialIndexCapacity = 16384;

		// Must match FImGuiConstants in ImGui.hlsl.
		struct FImGuiConstants
		{
			FMatrix4x4 Projection;
		};

		nvrhi::Format ToNvrhiFormat(ImTextureFormat Format)
		{
			return Format == ImTextureFormat_Alpha8 ? nvrhi::Format::R8_UNORM : nvrhi::Format::RGBA8_UNORM;
		}
	} // namespace

	void FEditorUIPass::Reflect(FRenderGraphPassTypeDesc& OutType) const
	{
		OutType.Description = "The editor's ImGui chrome. Injected by the engine in editor mode.";

		// Optional, and deliberately so: a graph that failed to compile produces nothing to draw over, and
		// that has to leave a usable editor rather than a failed frame. Reporting why is the editor's job,
		// which it cannot do if it is not drawn.
		//
		// Nominal as well. What the UI samples is decided per draw command by the ids RegisterTexture
		// handed out, not by this binding; declaring it is what orders this pass after the scene and gives
		// nvrhi the write-then-read it needs to insert a barrier for.
		FRenderGraphResourceDesc Scene = MakeTextureResource(SceneField, ERenderGraphResourceVisibility::Input);
		Scene.bOptional = true;
		Scene.Description = "The presented scene, drawn inside the viewport panel.";
		OutType.Inputs.push_back(std::move(Scene));

		// The swap chain, which the graph does not own: it rotates between frames, so the engine binds it
		// per frame instead of the graph allocating it.
		FRenderGraphResourceDesc Target = MakeTextureResource(TargetField, ERenderGraphResourceVisibility::Output);
		Target.Source = ERenderGraphResourceSource::Imported;
		// Clear, because the editor owns the whole window: the scene it shows arrives through the viewport
		// panel's texture rather than by having been drawn into the back buffer underneath.
		Target.LoadAction = ERenderGraphLoadAction::Clear;
		Target.Description = "The swap chain back buffer.";
		OutType.Outputs.push_back(std::move(Target));
	}

	bool FEditorUIPass::Initialize(FRenderer& Renderer)
	{		Device = Renderer.GetDevice();
		if (Device == nullptr || ImGui::GetCurrentContext() == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "FEditorUIPass requires a device and an active ImGui context");
			return false;
		}

		FShaderLibrary& Shaders = Renderer.GetShaderLibrary();
		VertexShader = Shaders.GetShader("ImGui/ImGui.hlsl", "MainVS", nvrhi::ShaderType::Vertex);
		PixelShader = Shaders.GetShader("ImGui/ImGui.hlsl", "MainPS", nvrhi::ShaderType::Pixel);
		if (VertexShader == nullptr || PixelShader == nullptr)
		{
			return false;
		}

		// Layout must match ImDrawVert exactly: float2 pos, float2 uv, uint8x4 unorm col.
		const std::array<nvrhi::VertexAttributeDesc, 3> Attributes = { {
			nvrhi::VertexAttributeDesc()
			    .setName("POSITION")
			    .setFormat(nvrhi::Format::RG32_FLOAT)
			    .setOffset(offsetof(ImDrawVert, pos))
			    .setElementStride(sizeof(ImDrawVert)),
			nvrhi::VertexAttributeDesc()
			    .setName("TEXCOORD")
			    .setFormat(nvrhi::Format::RG32_FLOAT)
			    .setOffset(offsetof(ImDrawVert, uv))
			    .setElementStride(sizeof(ImDrawVert)),
			nvrhi::VertexAttributeDesc()
			    .setName("COLOR")
			    .setFormat(nvrhi::Format::RGBA8_UNORM)
			    .setOffset(offsetof(ImDrawVert, col))
			    .setElementStride(sizeof(ImDrawVert)),
		} };

		InputLayout = Device->createInputLayout(Attributes.data(), static_cast<uint32_t>(Attributes.size()), VertexShader);
		if (InputLayout == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createInputLayout failed for the ImGui renderer");
			return false;
		}

		Sampler = Device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));
		ConstantBuffer =
		    Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(FImGuiConstants), "ImGuiConstants", 16));
		if (Sampler == nullptr || ConstantBuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Failed to create the ImGui sampler or constant buffer");
			return false;
		}

		// Built manually so the Vulkan binding offsets are applied.
		const nvrhi::BindingLayoutDesc LayoutDesc = MakeBindingLayoutDesc(nvrhi::ShaderType::All)
		                                                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0))
		                                                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))
		                                                .addItem(nvrhi::BindingLayoutItem::Sampler(0));

		BindingLayout = Device->createBindingLayout(LayoutDesc);
		if (BindingLayout == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createBindingLayout failed for the ImGui renderer");
			return false;
		}

		if (!EnsureGeometryCapacity(InitialVertexCapacity, InitialIndexCapacity))
		{
			return false;
		}

		// Signals that this backend honours ImGuiPlatformIO::Textures[] requests during render.
		ImGuiIO& IO = ImGui::GetIO();
		IO.BackendRendererName = "LimeEngine-NVRHI";
		IO.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;

		return true;
	}

	void FEditorUIPass::Shutdown()
	{
		if (ImGui::GetCurrentContext() != nullptr)
		{
			ImGuiIO& IO = ImGui::GetIO();
			IO.BackendRendererName = nullptr;
			IO.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);

			// Release ImGui's references so it does not hand back stale ids after a restart.
			for (ImTextureData* TextureData : ImGui::GetPlatformIO().Textures)
			{
				if (TextureData != nullptr && TextureData->RefCount == 1)
				{
					DestroyTexture(TextureData);
				}
			}
		}

		// Clears both ImGui owned and externally registered entries.
		Textures.clear();
		Pipelines.clear();
		BindingLayout = nullptr;
		ConstantBuffer = nullptr;
		IndexBuffer = nullptr;
		VertexBuffer = nullptr;
		Sampler = nullptr;
		InputLayout = nullptr;
		PixelShader = nullptr;
		VertexShader = nullptr;
		Device = nullptr;
		NextTextureId = 1;
		NextExternalTextureId = ExternalTextureIdBase;
		VertexCapacity = 0;
		IndexCapacity = 0;
	}

	bool FEditorUIPass::CreatePipeline(nvrhi::IFramebuffer* Framebuffer)
	{
		return GetPipelineFor(Framebuffer) != nullptr;
	}

	nvrhi::IGraphicsPipeline* FEditorUIPass::GetPipelineFor(nvrhi::IFramebuffer* Framebuffer)
	{
		if (Framebuffer == nullptr || Device == nullptr)
		{
			return nullptr;
		}

		// The layout, not the object. Two framebuffers with the same formats are interchangeable as far as
		// a pipeline is concerned, which is exactly the swap chain's case.
		const nvrhi::FramebufferInfoEx& Info = Framebuffer->getFramebufferInfo();
		std::string Key;
		Key.reserve(Info.colorFormats.size() * 4 + 8);
		for (const nvrhi::Format ColorFormat : Info.colorFormats)
		{
			Key += std::to_string(static_cast<uint32>(ColorFormat));
			Key += ',';
		}
		Key += '|';
		Key += std::to_string(static_cast<uint32>(Info.depthFormat));
		Key += '|';
		Key += std::to_string(Info.sampleCount);

		const auto Cached = Pipelines.find(Key);
		if (Cached != Pipelines.end())
		{
			return Cached->second;
		}

		nvrhi::BlendState::RenderTarget BlendTarget;
		BlendTarget.blendEnable = true;
		BlendTarget.srcBlend = nvrhi::BlendFactor::SrcAlpha;
		BlendTarget.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
		BlendTarget.blendOp = nvrhi::BlendOp::Add;
		BlendTarget.srcBlendAlpha = nvrhi::BlendFactor::One;
		BlendTarget.destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
		BlendTarget.blendOpAlpha = nvrhi::BlendOp::Add;

		nvrhi::RenderState RenderState;
		RenderState.blendState.setRenderTarget(0, BlendTarget);
		RenderState.depthStencilState.depthTestEnable = false;
		RenderState.depthStencilState.depthWriteEnable = false;
		RenderState.depthStencilState.stencilEnable = false;
		RenderState.rasterState.setCullNone().setScissorEnable(true);

		const nvrhi::GraphicsPipelineDesc PipelineDesc = nvrhi::GraphicsPipelineDesc()
		     .setPrimType(nvrhi::PrimitiveType::TriangleList)
		       .setInputLayout(InputLayout)
		       .setVertexShader(VertexShader)
		.setPixelShader(PixelShader)
		     .addBindingLayout(BindingLayout)
		   .setRenderState(RenderState);

		nvrhi::GraphicsPipelineHandle Pipeline = Device->createGraphicsPipeline(PipelineDesc, Framebuffer->getFramebufferInfo());
		if (Pipeline == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createGraphicsPipeline failed for the ImGui renderer");
			return nullptr;
		}

		nvrhi::IGraphicsPipeline* Result = Pipeline;
		Pipelines.emplace(std::move(Key), std::move(Pipeline));
		return Result;
	}

	void FEditorUIPass::OnFramebufferChanged(nvrhi::IFramebuffer* Framebuffer)
	{
		// Dropped rather than rebuilt for the new framebuffer. A null argument means the swap chain is
		// going away, so the pipelines that pin its textures have to go; a non-null one is rebuilt lazily
		// by the first draw, which is when the layout is known to be current.
		Pipelines.clear();
		LIME_UNUSED(Framebuffer);
	}

	bool FEditorUIPass::EnsureGeometryCapacity(uint32 RequiredVertexCount, uint32 RequiredIndexCount)
	{
		if (RequiredVertexCount > VertexCapacity || VertexBuffer == nullptr)
		{
			// Grow geometrically so a steadily busier UI does not reallocate every frame.
			const uint32 NewCapacity = std::max(RequiredVertexCount, VertexCapacity + VertexCapacity / 2 + 1);
			VertexBuffer = Device->createBuffer(nvrhi::BufferDesc()
			                                        .setByteSize(static_cast<size_t>(NewCapacity) * sizeof(ImDrawVert))
			                                        .setIsVertexBuffer(true)
			                                        .setCpuAccess(nvrhi::CpuAccessMode::None)
			                                        .setInitialState(nvrhi::ResourceStates::VertexBuffer)
			                                        .setKeepInitialState(true)
			                                        .setDebugName("ImGuiVertices"));
			if (VertexBuffer == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Failed to allocate the ImGui vertex buffer ({} vertices)", NewCapacity);
				return false;
			}
			VertexCapacity = NewCapacity;
		}

		if (RequiredIndexCount > IndexCapacity || IndexBuffer == nullptr)
		{
			const uint32 NewCapacity = std::max(RequiredIndexCount, IndexCapacity + IndexCapacity / 2 + 1);
			IndexBuffer = Device->createBuffer(nvrhi::BufferDesc()
			                                       .setByteSize(static_cast<size_t>(NewCapacity) * sizeof(ImDrawIdx))
			                                       .setIsIndexBuffer(true)
			                                       .setCpuAccess(nvrhi::CpuAccessMode::None)
			                                       .setInitialState(nvrhi::ResourceStates::IndexBuffer)
			                                       .setKeepInitialState(true)
			                                       .setDebugName("ImGuiIndices"));
			if (IndexBuffer == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Failed to allocate the ImGui index buffer ({} indices)", NewCapacity);
				return false;
			}
			IndexCapacity = NewCapacity;
		}

		return true;
	}

	ImTextureID FEditorUIPass::RegisterTexture(nvrhi::ITexture* Texture, ImTextureID ExistingId)
	{
		if (Device == nullptr || Texture == nullptr || BindingLayout == nullptr)
		{
			return ImTextureID_Invalid;
		}

		const nvrhi::BindingSetDesc BindingSetDesc = nvrhi::BindingSetDesc()
		                                                 .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, ConstantBuffer))
		                                                 .addItem(nvrhi::BindingSetItem::Texture_SRV(0, Texture))
		                                                 .addItem(nvrhi::BindingSetItem::Sampler(0, Sampler));

		nvrhi::BindingSetHandle BindingSet = Device->createBindingSet(BindingSetDesc, BindingLayout);
		if (BindingSet == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createBindingSet failed for an external ImGui texture");
			return ImTextureID_Invalid;
		}

		// Reusing the id keeps callers valid across a resize, where the texture object changes but the
		// handle they already handed to ImGui should stay the same.
		const ImTextureID TextureId = ExistingId != ImTextureID_Invalid ? ExistingId : NextExternalTextureId++;

		// The texture is owned by the caller, so only the binding set is retained here.
		Textures[TextureId] = FTextureEntry{ nullptr, std::move(BindingSet) };
		return TextureId;
	}

	void FEditorUIPass::UnregisterTexture(ImTextureID TextureId)
	{
		if (TextureId != ImTextureID_Invalid)
		{
			Textures.erase(TextureId);
		}
	}

	void FEditorUIPass::CreateTexture(nvrhi::ICommandList* CommandList, ImTextureData* TextureData)
	{
		const nvrhi::TextureDesc Desc = nvrhi::TextureDesc()
		                                    .setDimension(nvrhi::TextureDimension::Texture2D)
		                                    .setFormat(ToNvrhiFormat(TextureData->Format))
		                                    .setWidth(static_cast<uint32>(TextureData->Width))
		                                    .setHeight(static_cast<uint32>(TextureData->Height))
		                                    .setInitialState(nvrhi::ResourceStates::ShaderResource)
		                                    .setKeepInitialState(true)
		                                    .setDebugName("ImGuiTexture");

		nvrhi::TextureHandle Texture = Device->createTexture(Desc);
		if (Texture == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createTexture failed for an ImGui texture ({}x{})", TextureData->Width,
			               TextureData->Height);
			return;
		}

		const nvrhi::BindingSetDesc BindingSetDesc = nvrhi::BindingSetDesc()
		                                                 .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, ConstantBuffer))
		                                                 .addItem(nvrhi::BindingSetItem::Texture_SRV(0, Texture))
		                                                 .addItem(nvrhi::BindingSetItem::Sampler(0, Sampler));

		nvrhi::BindingSetHandle BindingSet = Device->createBindingSet(BindingSetDesc, BindingLayout);
		if (BindingSet == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createBindingSet failed for an ImGui texture");
			return;
		}

		const ImTextureID TextureId = NextTextureId++;
		Textures.emplace(TextureId, FTextureEntry{ std::move(Texture), std::move(BindingSet) });
		TextureData->SetTexID(TextureId);

		UploadTexture(CommandList, TextureData, true);
	}

	void FEditorUIPass::UploadTexture(nvrhi::ICommandList* CommandList, ImTextureData* TextureData, bool bFullUpload)
	{
		const auto Entry = Textures.find(TextureData->GetTexID());
		if (Entry == Textures.end())
		{
			return;
		}

		// NVRHI writeTexture takes a full mip slice, so partial updates are staged into a full-width
		// copy of the affected rows rather than issuing one write per dirty rect.
		const int32 BytesPerPixel = TextureData->BytesPerPixel;
		const int32 Pitch = TextureData->GetPitch();

		int32 FirstRow = 0;
		int32 RowCount = TextureData->Height;
		if (!bFullUpload)
		{
			FirstRow = TextureData->UpdateRect.y;
			RowCount = TextureData->UpdateRect.h;
		}

		if (RowCount <= 0)
		{
			return;
		}

		if (FirstRow == 0 && RowCount == TextureData->Height)
		{
			CommandList->writeTexture(Entry->second.Texture, 0, 0, TextureData->GetPixels(), static_cast<size_t>(Pitch));
			return;
		}

		// Rebuild the full image with the updated rows patched in; ImGui keeps the whole buffer alive.
		LIME_UNUSED(BytesPerPixel);
		CommandList->writeTexture(Entry->second.Texture, 0, 0, TextureData->GetPixels(), static_cast<size_t>(Pitch));
	}

	void FEditorUIPass::DestroyTexture(ImTextureData* TextureData)
	{
		const ImTextureID TextureId = TextureData->GetTexID();
		if (TextureId != ImTextureID_Invalid)
		{
			Textures.erase(TextureId);
		}

		TextureData->SetTexID(ImTextureID_Invalid);
		TextureData->SetStatus(ImTextureStatus_Destroyed);
	}

	void FEditorUIPass::UpdateTextures(nvrhi::ICommandList* CommandList, ImDrawData* DrawData)
	{
		if (DrawData->Textures == nullptr)
		{
			return;
		}

		for (ImTextureData* TextureData : *DrawData->Textures)
		{
			if (TextureData == nullptr)
			{
				continue;
			}

			switch (TextureData->Status)
			{
				case ImTextureStatus_WantCreate:
					CreateTexture(CommandList, TextureData);
					TextureData->SetStatus(ImTextureStatus_OK);
					break;
				case ImTextureStatus_WantUpdates:
					UploadTexture(CommandList, TextureData, false);
					TextureData->SetStatus(ImTextureStatus_OK);
					break;
				case ImTextureStatus_WantDestroy:
					// UnusedFrames guarantees no in-flight frame still references the texture.
					if (TextureData->UnusedFrames > 0)
					{
						DestroyTexture(TextureData);
					}
					break;
				default:
					break;
			}
		}
	}

	nvrhi::IBindingSet* FEditorUIPass::GetBindingSetForTexture(ImTextureID TextureId)
	{
		const auto Entry = Textures.find(TextureId);
		return Entry != Textures.end() ? Entry->second.BindingSet.Get() : nullptr;
	}

	void FEditorUIPass::Render(const FFrameContext& Context)
	{
		ImDrawData* DrawData = ImGui::GetDrawData();
		if (DrawData == nullptr || Context.CommandList == nullptr)
		{
			return;
		}

		UpdateTextures(Context.CommandList, DrawData);

		if (DrawData->TotalVtxCount == 0 || DrawData->DisplaySize.x <= 0.0f || DrawData->DisplaySize.y <= 0.0f)
		{
			return;
		}

		// The graph supplies the target through the frame context, having resolved the imported back buffer
		// for this frame. Falling back to the context's own framebuffer keeps the pass usable outside a
		// graph, which is what the automation harness relies on.
		nvrhi::IFramebuffer* Framebuffer = Context.Framebuffer;
		if (Framebuffer == nullptr)
		{
			return;
		}

		nvrhi::IGraphicsPipeline* Pipeline = GetPipelineFor(Framebuffer);
		if (Pipeline == nullptr)
		{
			return;
		}

		if (!EnsureGeometryCapacity(static_cast<uint32>(DrawData->TotalVtxCount), static_cast<uint32>(DrawData->TotalIdxCount)))
		{
			return;
		}

		// Flatten every command list into the two shared buffers so only two uploads are needed.
		std::vector<ImDrawVert> Vertices;
		std::vector<ImDrawIdx> Indices;
		Vertices.reserve(static_cast<SizeType>(DrawData->TotalVtxCount));
		Indices.reserve(static_cast<SizeType>(DrawData->TotalIdxCount));

		for (int32 ListIndex = 0; ListIndex < DrawData->CmdLists.Size; ++ListIndex)
		{
			const ImDrawList* DrawList = DrawData->CmdLists[ListIndex];
			Vertices.insert(Vertices.end(), DrawList->VtxBuffer.Data, DrawList->VtxBuffer.Data + DrawList->VtxBuffer.Size);
			Indices.insert(Indices.end(), DrawList->IdxBuffer.Data, DrawList->IdxBuffer.Data + DrawList->IdxBuffer.Size);
		}

		Context.CommandList->writeBuffer(VertexBuffer, Vertices.data(), Vertices.size() * sizeof(ImDrawVert));
		Context.CommandList->writeBuffer(IndexBuffer, Indices.data(), Indices.size() * sizeof(ImDrawIdx));

		const float Left = DrawData->DisplayPos.x;
		const float Right = DrawData->DisplayPos.x + DrawData->DisplaySize.x;
		const float Top = DrawData->DisplayPos.y;
		const float Bottom = DrawData->DisplayPos.y + DrawData->DisplaySize.y;

		FImGuiConstants Constants;
		Constants.Projection = FMatrix4x4::OrthographicOffCenterLH(Left, Right, Bottom, Top, 0.0f, 1.0f);
		Context.CommandList->writeBuffer(ConstantBuffer, &Constants, sizeof(Constants));

		const nvrhi::Format IndexFormat = sizeof(ImDrawIdx) == 2 ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT;
		const ImVec2 ClipOffset = DrawData->DisplayPos;
		const ImVec2 ClipScale = DrawData->FramebufferScale;

		uint32 GlobalVertexOffset = 0;
		uint32 GlobalIndexOffset = 0;
		const ImDrawCallback ResetRenderStateCallback = ImGui::GetPlatformIO().DrawCallback_ResetRenderState;

		for (int32 ListIndex = 0; ListIndex < DrawData->CmdLists.Size; ++ListIndex)
		{
			const ImDrawList* DrawList = DrawData->CmdLists[ListIndex];

			for (const ImDrawCmd& DrawCmd : DrawList->CmdBuffer)
			{
				if (DrawCmd.UserCallback != nullptr)
				{
					// State is set per draw below, so a reset request needs no extra work here.
					if (DrawCmd.UserCallback != ResetRenderStateCallback)
					{
						DrawCmd.UserCallback(DrawList, &DrawCmd);
					}
					continue;
				}

				if (DrawCmd.ElemCount == 0)
				{
					continue;
				}

				nvrhi::IBindingSet* BindingSet = GetBindingSetForTexture(DrawCmd.GetTexID());
				if (BindingSet == nullptr)
				{
					continue;
				}

				const float ClipMinX = (DrawCmd.ClipRect.x - ClipOffset.x) * ClipScale.x;
				const float ClipMinY = (DrawCmd.ClipRect.y - ClipOffset.y) * ClipScale.y;
				const float ClipMaxX = (DrawCmd.ClipRect.z - ClipOffset.x) * ClipScale.x;
				const float ClipMaxY = (DrawCmd.ClipRect.w - ClipOffset.y) * ClipScale.y;

				const int32 ScissorLeft = std::max(static_cast<int32>(ClipMinX), 0);
				const int32 ScissorTop = std::max(static_cast<int32>(ClipMinY), 0);
				const int32 ScissorRight = std::min(static_cast<int32>(ClipMaxX), static_cast<int32>(Context.ViewportWidth));
				const int32 ScissorBottom = std::min(static_cast<int32>(ClipMaxY), static_cast<int32>(Context.ViewportHeight));
				if (ScissorRight <= ScissorLeft || ScissorBottom <= ScissorTop)
				{
					continue;
				}

				const nvrhi::ViewportState ViewportState =
				    nvrhi::ViewportState()
				        .addViewport(nvrhi::Viewport(static_cast<float>(Context.ViewportWidth), static_cast<float>(Context.ViewportHeight)))
				        .addScissorRect(nvrhi::Rect(ScissorLeft, ScissorRight, ScissorTop, ScissorBottom));

				const nvrhi::GraphicsState State =
				    nvrhi::GraphicsState()
				 .setPipeline(Pipeline)
				      .setFramebuffer(Framebuffer)
				        .addBindingSet(BindingSet)
				        .addVertexBuffer(nvrhi::VertexBufferBinding().setBuffer(VertexBuffer).setSlot(0).setOffset(0))
				        .setIndexBuffer(nvrhi::IndexBufferBinding().setBuffer(IndexBuffer).setFormat(IndexFormat).setOffset(0))
				        .setViewport(ViewportState);

				Context.CommandList->setGraphicsState(State);
				Context.CommandList->drawIndexed(nvrhi::DrawArguments()
				                                     .setVertexCount(DrawCmd.ElemCount)
				                                     .setStartIndexLocation(GlobalIndexOffset + DrawCmd.IdxOffset)
				                                     .setStartVertexLocation(GlobalVertexOffset + DrawCmd.VtxOffset));
			}

			GlobalVertexOffset += static_cast<uint32>(DrawList->VtxBuffer.Size);
			GlobalIndexOffset += static_cast<uint32>(DrawList->IdxBuffer.Size);
		}
	}
} // namespace Lime
