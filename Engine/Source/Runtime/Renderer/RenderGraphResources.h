// The textures and framebuffers a compiled render graph needs.
//
// Splits the compile from the allocation on purpose. The compile decides what resources exist and which
// pass touches each one, and is pure data. This is where those decisions meet the device, so a resize
// re-runs only this half: the graph's shape did not change, only the size its unpinned resources resolve
// to.
//
// No aliasing and no lifetime tracking. Every resource in the compiled graph gets its own texture for as
// long as the graph is loaded. That costs memory a real render graph would reclaim, and is the right
// trade for now: reuse needs the lifetime analysis to be correct or it produces passes reading a texture
// another pass has already overwritten, which is far harder to diagnose than a larger memory figure.

#pragma once

#include "RenderGraph/RenderGraphCompiler.h"
#include "Renderer/RenderTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Lime
{
	// Everything one pass sees: its framebuffer and the textures bound to its fields.
	//
	// Built once per compile and handed to the pass by pointer during Render, so a pass reads its inputs by
	// the name it declared them under without knowing anything about the graph it sits in.
	class FRenderGraphPassView final : public FRenderGraphPassResources
	{
	public:
		nvrhi::ITexture* FindTexture(std::string_view FieldName) const override;
		nvrhi::IFramebuffer* GetFramebuffer() const override { return Framebuffer; }
		uint32 GetWidth() const override { return Width; }
		uint32 GetHeight() const override { return Height; }

	private:
		friend class FRenderGraphResources;

		// Field name to texture. A map rather than the compiled binding list because a pass looks a field up
		// by name, and the list would need a linear scan per lookup per frame.
		std::unordered_map<std::string, nvrhi::ITexture*> TexturesByField;
		nvrhi::FramebufferHandle Framebuffer;
		uint32 Width = 0;
		uint32 Height = 0;
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
		// keep it. Returns false and releases what it made on the first failure, so a caller never sees a
		// half allocated graph.
		bool Allocate(nvrhi::IDevice* Device, const FRenderGraphCompileResult& Compiled, uint32 Width, uint32 Height);

		void Release();

		bool IsValid() const { return !Views.empty(); }

		// The view for the pass at this position in the compiled execution order.
		const FRenderGraphPassView* FindView(SizeType ExecutionIndex) const;

		// A texture the graph was asked to produce, by output slot. This is what a viewport displays.
		nvrhi::ITexture* FindOutputTexture(SizeType Slot) const;
		SizeType GetOutputCount() const { return OutputTextures.size(); }

		uint32 GetWidth() const { return Width; }
		uint32 GetHeight() const { return Height; }

	private:
		nvrhi::TextureHandle CreateTexture(nvrhi::IDevice* Device, const FCompiledResource& Resource, uint32 DefaultWidth,
		                                   uint32 DefaultHeight);

		std::vector<nvrhi::TextureHandle> Textures;
		std::vector<FRenderGraphPassView> Views;
		std::vector<nvrhi::ITexture*> OutputTextures;
		uint32 Width = 0;
		uint32 Height = 0;
	};
} // namespace Lime
