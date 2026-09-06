#include "Renderer/RenderGraphResources.h"

#include "Core/Logging/LogManager.h"

#include <nvrhi/utils.h>

#include <cstdio>
#include <utility>
#include <vector>

namespace Lime
{
	namespace
	{
		// Depth is cleared to the far plane. Matches the value the executor clears with and the [0, 1] range
		// PerspectiveFovLH produces; a mismatch costs D3D12 its fast clear path and it warns about it.
		constexpr float FarPlaneDepth = 1.0f;

		// Identity of a framebuffer, as the addresses of the textures it attaches.
		//
		// Pointers rather than resource indices, because two frames can bind different textures to the same
		// index: that is exactly what the swap chain does, and keying on the index would hand back a
		// framebuffer built against the previous back buffer.
		std::string MakeFramebufferKey(const std::vector<nvrhi::ITexture*>& ColorTextures, nvrhi::ITexture* DepthTexture)
		{
			std::string Key;
			Key.reserve((ColorTextures.size() + 1) * (sizeof(void*) * 2 + 1));

			const auto Append = [&Key](const void* Pointer)
			{
				char Buffer[sizeof(void*) * 2 + 2] = {};
				std::snprintf(Buffer, sizeof(Buffer), "%p|", Pointer);
				Key += Buffer;
			};

			for (nvrhi::ITexture* Texture : ColorTextures)
			{
				Append(Texture);
			}
			Append(DepthTexture);
			return Key;
		}
	} // namespace

	nvrhi::ITexture* FRenderGraphPassView::FindTexture(std::string_view FieldName) const
	{
		// Constructed from string_view because the map is keyed on std::string and heterogeneous lookup is
		// not enabled for it; the cost is a small allocation on a path that runs a handful of times a frame.
		const auto Found = ResourceByField.find(std::string(FieldName));
		if (Found == ResourceByField.end() || Owner == nullptr)
		{
			return nullptr;
		}
		return Owner->GetTexture(Found->second);
	}

	nvrhi::IFramebuffer* FRenderGraphPassView::GetFramebuffer() const
	{
		if (Owner == nullptr || (ColorAttachments.empty() && DepthAttachment == InvalidIndex))
		{
			// A pass with no outputs draws into whatever target the caller sets. Null is what tells the
			// executor that.
			return nullptr;
		}
		return Owner->ResolveFramebuffer(*this);
	}

	FRenderGraphResources::~FRenderGraphResources()
	{
		Release();
	}

	void FRenderGraphResources::Release()
	{
		// Views first: they reach textures through the owner, so clearing them before the handles go keeps
		// anything that outlives this object from resolving a released texture.
		Views.clear();
		// Before the textures, since a framebuffer holds references to its attachments.
		FramebufferCache.clear();
		OutputResourceIndices.clear();
		ImportedByName.clear();
		ImportedTextures.clear();
		Textures.clear();
		Device = nullptr;
		Width = 0;
		Height = 0;
	}

	nvrhi::ITexture* FRenderGraphResources::GetTexture(SizeType ResourceIndex) const
	{
		if (ResourceIndex < Textures.size() && Textures[ResourceIndex] != nullptr)
		{
			return Textures[ResourceIndex];
		}
		// Imported slots have no handle of their own; null until the engine binds one this frame.
		return ResourceIndex < ImportedTextures.size() ? ImportedTextures[ResourceIndex] : nullptr;
	}

	void FRenderGraphResources::BindImportedTexture(std::string_view ImportName, nvrhi::ITexture* Texture)
	{
		const auto Found = ImportedByName.find(std::string(ImportName));
		if (Found == ImportedByName.end())
		{
			return;
		}
		ImportedTextures[Found->second] = Texture;
	}

	nvrhi::IFramebuffer* FRenderGraphResources::ResolveFramebuffer(const FRenderGraphPassView& View) const
	{
		if (Device == nullptr)
		{
			return nullptr;
		}

		std::vector<nvrhi::ITexture*> ColorTextures;
		ColorTextures.reserve(View.ColorAttachments.size());
		for (const SizeType ResourceIndex : View.ColorAttachments)
		{
			nvrhi::ITexture* Texture = GetTexture(ResourceIndex);
			if (Texture == nullptr)
			{
				// An imported target the engine has not bound this frame. Nothing can be drawn, and building a
				// framebuffer with a hole in it would fail validation instead of skipping the pass.
				return nullptr;
			}
			ColorTextures.push_back(Texture);
		}

		nvrhi::ITexture* DepthTexture =
		    View.DepthAttachment != FRenderGraphPassView::InvalidIndex ? GetTexture(View.DepthAttachment) : nullptr;
		if (View.DepthAttachment != FRenderGraphPassView::InvalidIndex && DepthTexture == nullptr)
		{
			return nullptr;
		}

		const std::string Key = MakeFramebufferKey(ColorTextures, DepthTexture);
		const auto Cached = FramebufferCache.find(Key);
		if (Cached != FramebufferCache.end())
		{
			return Cached->second;
		}

		nvrhi::FramebufferDesc Desc;
		for (nvrhi::ITexture* Texture : ColorTextures)
		{
			Desc.addColorAttachment(Texture);
		}
		if (DepthTexture != nullptr)
		{
			Desc.setDepthAttachment(DepthTexture);
		}

		nvrhi::FramebufferHandle Framebuffer = Device->createFramebuffer(Desc);
		if (Framebuffer == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createFramebuffer failed for a render graph pass");
			return nullptr;
		}

		nvrhi::IFramebuffer* Result = Framebuffer;
		FramebufferCache.emplace(Key, std::move(Framebuffer));
		return Result;
	}

	nvrhi::TextureHandle FRenderGraphResources::CreateTexture(nvrhi::IDevice* InDevice, const FCompiledResource& Resource,
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

		nvrhi::TextureHandle Texture = InDevice->createTexture(Desc);
		if (Texture == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "createTexture failed for render graph resource '{}' ({}x{}, {})", Resource.Name,
			             ResolvedWidth, ResolvedHeight, nvrhi::utils::FormatToString(ResolvedFormat));
		}
		return Texture;
	}

	bool FRenderGraphResources::Allocate(nvrhi::IDevice* InDevice, const FRenderGraphCompileResult& Compiled, uint32 InWidth,
	          uint32 InHeight)
	{
		Release();

		if (InDevice == nullptr || !Compiled.bSucceeded)
		{
			return false;
		}

		if (InWidth == 0 || InHeight == 0)
		{
			// Not an error worth logging every frame: the viewport reports no size until it has been laid
			// out, and the caller retries once it has one.
			return false;
		}

		Device = InDevice;
		Width = InWidth;
		Height = InHeight;

		Textures.resize(Compiled.Resources.size());
		ImportedTextures.assign(Compiled.Resources.size(), nullptr);

		for (SizeType Index = 0; Index < Compiled.Resources.size(); ++Index)
		{
			const FCompiledResource& Resource = Compiled.Resources[Index];

			// Imported resources are the engine's. Creating one here would allocate a texture that the first
			// BindImportedTexture immediately shadows.
			if (Resource.IsImported())
			{
				ImportedByName.emplace(Resource.ImportName, Index);
				continue;
			}

			nvrhi::TextureHandle Texture = CreateTexture(InDevice, Resource, InWidth, InHeight);
			if (Texture == nullptr)
			{
				Release();
				return false;
			}
			Textures[Index] = std::move(Texture);
		}

		Views.resize(Compiled.ExecutionOrder.size());
		for (SizeType PassIndex = 0; PassIndex < Compiled.ExecutionOrder.size(); ++PassIndex)
		{
			const FCompiledPass& Pass = Compiled.ExecutionOrder[PassIndex];
			FRenderGraphPassView& View = Views[PassIndex];
			View.Owner = this;
			View.Width = InWidth;
			View.Height = InHeight;

			for (const FCompiledPassBinding& Binding : Pass.Bindings)
			{
				View.ResourceByField.emplace(Binding.FieldName, Binding.ResourceIndex);

				if (Binding.Visibility == ERenderGraphResourceVisibility::Input)
				{
					continue;
				}

				if (Compiled.Resources[Binding.ResourceIndex].IsImported())
				{
					View.bHasImportedAttachment = true;
				}

				// Outputs become attachments. Depth goes to the depth slot and everything else is appended
				// as a colour target, which is what makes the attachment order match the order the pass
				// declared its outputs in: a pixel shader writes to SV_Target0 expecting the first one.
				//
				// Unspecified resolves to Clear so that a pass which said nothing behaves as it always has.
				const ERenderGraphLoadAction LoadAction =
				    Binding.LoadAction != ERenderGraphLoadAction::Unspecified ? Binding.LoadAction : ERenderGraphLoadAction::Clear;

				if (Compiled.Resources[Binding.ResourceIndex].bIsDepth)
				{
					View.DepthAttachment = Binding.ResourceIndex;
					View.DepthLoadAction = LoadAction;
				}
				else
				{
					View.ColorAttachments.push_back(Binding.ResourceIndex);
					View.ColorLoadActions.push_back(LoadAction);
				}
			}
		}

		OutputResourceIndices = Compiled.OutputResourceIndices;

		LIME_LOG_TRACE(LIME_LOG_CATEGORY_RENDERER, "Render graph resources allocated: {} texture(s), {} pass(es), {}x{}",
		 Textures.size(), Views.size(), InWidth, InHeight);
		return true;
	}

	const FRenderGraphPassView* FRenderGraphResources::FindView(SizeType ExecutionIndex) const
	{
		return ExecutionIndex < Views.size() ? &Views[ExecutionIndex] : nullptr;
	}

	nvrhi::ITexture* FRenderGraphResources::FindOutputTexture(SizeType Slot) const
	{
		return Slot < OutputResourceIndices.size() ? GetTexture(OutputResourceIndices[Slot]) : nullptr;
	}
} // namespace Lime
