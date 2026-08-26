#include "Renderer/RenderGraphResources.h"

#include "Core/Logging/LogManager.h"

#include <nvrhi/utils.h>

#include <utility>

namespace Lime
{
	namespace
	{
		// Depth is cleared to the far plane. Matches the value the executor clears with and the [0, 1] range
		// PerspectiveFovLH produces; a mismatch costs D3D12 its fast clear path and it warns about it.
		constexpr float FarPlaneDepth = 1.0f;
	} // namespace

	nvrhi::ITexture* FRenderGraphPassView::FindTexture(std::string_view FieldName) const
	{
		// Constructed from string_view because the map is keyed on std::string and heterogeneous lookup is
		// not enabled for it; the cost is a small allocation on a path that runs a handful of times a frame.
		const auto Found = TexturesByField.find(std::string(FieldName));
		return Found != TexturesByField.end() ? Found->second : nullptr;
	}

	FRenderGraphResources::~FRenderGraphResources()
	{
		Release();
	}

	void FRenderGraphResources::Release()
	{
		// Views first: they hold raw pointers into Textures, so releasing the handles while a view still
		// referenced them would leave danglers behind for however long the object lived.
		Views.clear();
		OutputTextures.clear();
		Textures.clear();
		Width = 0;
		Height = 0;
	}

	nvrhi::TextureHandle FRenderGraphResources::CreateTexture(nvrhi::IDevice* Device, const FCompiledResource& Resource,
	                                                          uint32 DefaultWidth, uint32 DefaultHeight)
	{
		// A resource that pinned its size keeps it; one that left it at zero follows the graph. That is what
		// lets a shadow map stay 2048 square while the colour targets track the viewport.
		const uint32 ResolvedWidth = Resource.Width > 0 ? Resource.Width : DefaultWidth;
		const uint32 ResolvedHeight = Resource.Height > 0 ? Resource.Height : DefaultHeight;

		// The graph's default format when the passes left it open. Depth and colour need different
		// defaults, and which one applies is already known from the format the passes did agree on.
		nvrhi::Format ResolvedFormat = Resource.Format;
		if (ResolvedFormat == nvrhi::Format::UNKNOWN)
		{
			ResolvedFormat = Resource.bIsDepth ? nvrhi::Format::D32 : nvrhi::Format::RGBA8_UNORM;
		}

		nvrhi::TextureDesc Desc = nvrhi::TextureDesc()
		                              .setDimension(nvrhi::TextureDimension::Texture2D)
		                              .setWidth(ResolvedWidth)
		                              .setHeight(ResolvedHeight)
		                              .setFormat(ResolvedFormat)
		                              .setDebugName(Resource.Name);

		// Written by a pass, so it has to be attachable. Every resource in a compiled graph is produced by
		// some pass, so this is always true; it is set from the flag rather than unconditionally so the
		// intent stays visible.
		Desc.setIsRenderTarget(Resource.bUsedAsRenderTarget);

		if (Resource.bIsDepth)
		{
			// isTypeless is required rather than an optimisation. A depth resource is viewed through two
			// incompatible view types, a depth stencil view for writing and a shader view for reading, and
			// only a typeless resource can carry both. Creating it as a plain depth format makes the D3D12
			// backend fault while building descriptors.
			Desc.setIsTypeless(true);
			Desc.setInitialState(nvrhi::ResourceStates::DepthWrite);
			Desc.setClearValue(nvrhi::Color(FarPlaneDepth));
		}
		else
		{
			// Read by a later pass or sampled by the editor to show the scene, so it starts where a reader
			// expects it. nvrhi tracks the transitions from here on.
			Desc.setInitialState(nvrhi::ResourceStates::ShaderResource);
		}

		Desc.setKeepInitialState(true);

		nvrhi::TextureHandle Texture = Device->createTexture(Desc);
		if (Texture == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createTexture failed for render graph resource '{}' ({}x{}, {})", Resource.Name,
			               ResolvedWidth, ResolvedHeight, nvrhi::utils::FormatToString(ResolvedFormat));
		}
		return Texture;
	}

	bool FRenderGraphResources::Allocate(nvrhi::IDevice* Device, const FRenderGraphCompileResult& Compiled, uint32 InWidth, uint32 InHeight)
	{
		Release();

		if (Device == nullptr || !Compiled.bSucceeded)
		{
			return false;
		}

		if (InWidth == 0 || InHeight == 0)
		{
			// Not an error worth logging every frame: the viewport reports no size until it has been laid
			// out, and the caller retries once it has one.
			return false;
		}

		Width = InWidth;
		Height = InHeight;

		Textures.reserve(Compiled.Resources.size());
		for (const FCompiledResource& Resource : Compiled.Resources)
		{
			nvrhi::TextureHandle Texture = CreateTexture(Device, Resource, InWidth, InHeight);
			if (Texture == nullptr)
			{
				Release();
				return false;
			}
			Textures.push_back(std::move(Texture));
		}

		Views.resize(Compiled.ExecutionOrder.size());
		for (SizeType PassIndex = 0; PassIndex < Compiled.ExecutionOrder.size(); ++PassIndex)
		{
			const FCompiledPass& Pass = Compiled.ExecutionOrder[PassIndex];
			FRenderGraphPassView& View = Views[PassIndex];
			View.Width = InWidth;
			View.Height = InHeight;

			nvrhi::FramebufferDesc FramebufferDesc;
			bool bHasAttachment = false;

			for (const FCompiledPassBinding& Binding : Pass.Bindings)
			{
				nvrhi::ITexture* Texture = Textures[Binding.ResourceIndex];
				View.TexturesByField.emplace(Binding.FieldName, Texture);

				if (Binding.Visibility == ERenderGraphResourceVisibility::Input)
				{
					continue;
				}

				// Outputs become attachments. Depth goes to the depth slot and everything else is appended
				// as a colour target, which is what makes the attachment order match the order the pass
				// declared its outputs in: a pixel shader writes to SV_Target0 expecting the first one.
				if (Compiled.Resources[Binding.ResourceIndex].bIsDepth)
				{
					FramebufferDesc.setDepthAttachment(Texture);
				}
				else
				{
					FramebufferDesc.addColorAttachment(Texture);
				}
				bHasAttachment = true;
			}

			if (!bHasAttachment)
			{
				// A pass with no outputs draws into whatever target the caller sets, which is how the editor
				// UI pass works. Leaving the framebuffer null is what tells the executor that.
				continue;
			}

			View.Framebuffer = Device->createFramebuffer(FramebufferDesc);
			if (View.Framebuffer == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createFramebuffer failed for render graph pass '{}'", Pass.PassName);
				Release();
				return false;
			}
		}

		OutputTextures.reserve(Compiled.OutputResourceIndices.size());
		for (const SizeType ResourceIndex : Compiled.OutputResourceIndices)
		{
			OutputTextures.push_back(Textures[ResourceIndex]);
		}

		LIME_LOG_INFO(LIME_LOG_CATEGORY_RENDERER, "Render graph resources allocated: {} texture(s), {} pass(es), {}x{}", Textures.size(),
		              Views.size(), InWidth, InHeight);
		return true;
	}

	const FRenderGraphPassView* FRenderGraphResources::FindView(SizeType ExecutionIndex) const
	{
		return ExecutionIndex < Views.size() ? &Views[ExecutionIndex] : nullptr;
	}

	nvrhi::ITexture* FRenderGraphResources::FindOutputTexture(SizeType Slot) const
	{
		return Slot < OutputTextures.size() ? OutputTextures[Slot] : nullptr;
	}
} // namespace Lime
