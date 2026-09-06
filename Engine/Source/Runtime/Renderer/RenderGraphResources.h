// The textures and framebuffers a compiled render graph needs.
//
// Splits the compile from the allocation on purpose. The compile decides what resources exist and which
// pass touches each one, and is pure data. This is where those decisions meet the device, so a resize
// re-runs only this half: the graph's shape did not change, only the size its unpinned resources resolve
// to.
//
// Imported resources are the exception to "allocated here". The engine binds them per frame, because the
// swap chain back buffer rotates between frames and is not the graph's to create. That is why the
// framebuffers are cached by their attachments rather than built once: a pass writing an imported target
// needs a different framebuffer each frame, and rebuilding the pipeline for it every time was what made
// the editor UI pass rebuild its pipeline on nearly every frame before the graph owned it.
//
// No aliasing and no lifetime tracking. Every transient resource in the compiled graph gets its own
// texture for as long as the graph is loaded. That costs memory a real render graph would reclaim, and is
// the right trade for now: reuse needs the lifetime analysis to be correct or it produces passes reading a
// texture another pass has already overwritten, which is far harder to diagnose than a larger memory
// figure.

#pragma once

#include "RenderGraph/RenderGraphCompiler.h"
#include "Renderer/RenderTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Lime
{
	class FRenderGraphResources;

	// Everything one pass sees: its framebuffer and the textures bound to its fields.
	//
	// Built once per compile and handed to the pass by pointer during Render, so a pass reads its inputs by
	// the name it declared them under without knowing anything about the graph it sits in.
	//
	// The framebuffer is resolved through the owning resources rather than held, because a pass writing an
	// imported target has a different one per frame. A pass that writes only transient targets still gets
	// the same object every frame, since the cache is keyed on the attachments.
	class FRenderGraphPassView final : public FRenderGraphPassResources
	{
	public:
		nvrhi::ITexture* FindTexture(std::string_view FieldName) const override;
		nvrhi::IFramebuffer* GetFramebuffer() const override;
		uint32 GetWidth() const override { return Width; }
		uint32 GetHeight() const override { return Height; }

		// True when any attachment of this pass is an engine supplied texture, so the framebuffer cannot be
		// assumed stable across frames.
		bool HasImportedAttachment() const { return bHasImportedAttachment; }

		// What to do with each attachment before the pass writes it. The executor applies these rather than
		// clearing unconditionally, which is what allows a pass to draw over an imported target that already
		// holds this frame's work.
		SizeType ColorAttachmentCount() const { return ColorAttachments.size(); }
		ERenderGraphLoadAction GetColorLoadAction(SizeType Slot) const
		{
			return Slot < ColorLoadActions.size() ? ColorLoadActions[Slot] : ERenderGraphLoadAction::Clear;
		}
		ERenderGraphLoadAction GetDepthLoadAction() const { return DepthLoadAction; }

	private:
		friend class FRenderGraphResources;

		// Field name to the index of the resource bound to it. Indices rather than raw pointers because an
		// imported resource's texture changes per frame, and a pointer captured at allocation time would go
		// stale the first time the swap chain rotated.
		std::unordered_map<std::string, SizeType> ResourceByField;
		// Attachments in the order they became colour targets, plus the depth one, as resource indices.
		std::vector<SizeType> ColorAttachments;
		// Parallel to ColorAttachments.
		std::vector<ERenderGraphLoadAction> ColorLoadActions;
		SizeType DepthAttachment = InvalidIndex;
		ERenderGraphLoadAction DepthLoadAction = ERenderGraphLoadAction::Clear;
		// Resolved lazily against the current textures; not owned.
		const FRenderGraphResources* Owner = nullptr;
		uint32 Width = 0;
		uint32 Height = 0;
		bool bHasImportedAttachment = false;

		static constexpr SizeType InvalidIndex = static_cast<SizeType>(-1);
	};

	// Owns the textures and framebuffers for one compiled graph.
	class FRenderGraphResources
	{
	public:
		LIME_NON_COPYABLE(FRenderGraphResources);
		LIME_NON_MOVABLE(FRenderGraphResources);

		FRenderGraphResources() = default;
		~FRenderGraphResources();

		// Creates everything the compiled graph needs at the given size.
		//
		// Width and height resolve the resources that left their size unspecified; those that pinned one
		// keep it. Imported resources are not created: BindImportedTexture supplies them per frame, and a
		// pass reading one before it is bound sees null.
		//
		// Returns false and releases what it made on the first failure, so a caller never sees a half
		// allocated graph.
		bool Allocate(nvrhi::IDevice* Device, const FRenderGraphCompileResult& Compiled, uint32 Width, uint32 Height);

		void Release();

		bool IsValid() const { return !Views.empty(); }

		// Points an imported slot at the texture to use for this frame. Called before the graph executes.
		//
		// Unknown names are ignored rather than reported: the engine offers what it has, and a graph that
		// does not draw into the back buffer has no reason to be told about it.
		void BindImportedTexture(std::string_view ImportName, nvrhi::ITexture* Texture);

		// The view for the pass at this position in the compiled execution order.
		const FRenderGraphPassView* FindView(SizeType ExecutionIndex) const;

		// A texture the graph was asked to produce, by output slot. This is what a viewport displays.
		nvrhi::ITexture* FindOutputTexture(SizeType Slot) const;
		SizeType GetOutputCount() const { return OutputResourceIndices.size(); }

		uint32 GetWidth() const { return Width; }
		uint32 GetHeight() const { return Height; }

	private:
		friend class FRenderGraphPassView;

		nvrhi::TextureHandle CreateTexture(nvrhi::IDevice* Device, const FCompiledResource& Resource, uint32 DefaultWidth,
		uint32 DefaultHeight);

		nvrhi::ITexture* GetTexture(SizeType ResourceIndex) const;
		// Returns the framebuffer for this view's attachments, building and caching it on first use.
		nvrhi::IFramebuffer* ResolveFramebuffer(const FRenderGraphPassView& View) const;

		nvrhi::IDevice* Device = nullptr;
		// Transient textures, indexed alongside Compiled.Resources. Null at an imported index.
		std::vector<nvrhi::TextureHandle> Textures;
		// Engine supplied textures for the current frame, indexed the same way. Not owned.
		std::vector<nvrhi::ITexture*> ImportedTextures;
		// Import name to resource index, so binding does not scan the resource table.
		std::unordered_map<std::string, SizeType> ImportedByName;
		std::vector<FRenderGraphPassView> Views;
		std::vector<SizeType> OutputResourceIndices;

		// Framebuffers keyed on the textures they attach, so a pass writing an imported target gets one per
		// distinct back buffer and a pass writing only transient targets keeps a single one. Mutable because
		// resolving is logically a read: the pass asks for its framebuffer and the cache is an
		// implementation detail of answering.
		mutable std::unordered_map<std::string, nvrhi::FramebufferHandle> FramebufferCache;

		uint32 Width = 0;
		uint32 Height = 0;
	};
} // namespace Lime
