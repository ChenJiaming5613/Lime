// Turns a described graph into something executable: an order to run the passes in, and the resources
// they need.
//
// Separate from FRenderGraphDesc because the two answer different questions. The description is what the
// file says and what the editor edits, and stays usable while half assembled. A compile result is a
// commitment: it either describes a graph that can run, or it explains why not. Keeping them apart is
// what lets the editor show a work in progress without the renderer ever seeing one.
//
// Deliberately free of GPU calls. Everything here is decidable from the description and the pass
// reflections, so the ordering, the culling and the format negotiation are all covered by unit tests
// that need no device. Allocating the textures this produces is the renderer's job.

#pragma once

#include "RenderGraph/RenderGraphDesc.h"
#include "RenderGraph/RenderGraphPassType.h"

#include <string>
#include <vector>

namespace Lime
{
	// One resource the graph needs, after both ends of every edge connecting it have been reconciled.
	//
	// An edge does not create two resources that get copied into each other; it means the producer and the
	// consumer are talking about one resource. So a chain of passes sharing a texture produces a single
	// entry here, named after the field that writes it.
	struct FCompiledResource
	{
		// "Pass.field" of the producing end, which is unique and tells a reader where it came from.
		std::string Name;
		ERenderGraphResourceKind Kind = ERenderGraphResourceKind::Texture;
		nvrhi::Format Format = nvrhi::Format::UNKNOWN;
		// 0 means the graph's size. Resolved against the default only when the texture is created, so a
		// resize does not require recompiling.
		uint32 Width = 0;
		uint32 Height = 0;
		bool bIsDepth = false;
		// Accumulated across every pass touching the resource, which is what the bind flags are derived
		// from: a texture written by one pass and read by the next needs both.
		bool bUsedAsRenderTarget = false;
		bool bUsedAsShaderResource = false;
	};

	// One field of one pass, bound to a resource.
	//
	// The resource is an index rather than a name so that execution does not compare strings. Resolving it
	// here is the whole point of compiling: the field name is what the pass knows, the index is what the
	// frame loop uses.
	struct FCompiledPassBinding
	{
		// The name the pass itself uses, which is how it asks for the resource at execution time.
		std::string FieldName;
		SizeType ResourceIndex = 0;
		ERenderGraphResourceVisibility Visibility = ERenderGraphResourceVisibility::Input;
	};

	struct FCompiledPass
	{
		std::string PassName;
		std::string TypeName;
		std::vector<FCompiledPassBinding> Bindings;

		// True when this pass writes something the graph was asked to output, directly or through others.
		// Passes that do not are culled and never appear here; the flag is kept for diagnostics.
		bool bContributesToOutput = true;
	};

	struct FRenderGraphCompileResult
	{
		bool bSucceeded = false;
		std::vector<FRenderGraphIssue> Issues;
		// Topologically ordered and culled: running these in sequence satisfies every dependency.
		std::vector<FCompiledPass> ExecutionOrder;
		std::vector<FCompiledResource> Resources;
		// Resource indices the graph was asked to produce, in the order they were marked. A viewport binds
		// to one of these.
		std::vector<SizeType> OutputResourceIndices;

		SizeType CountErrors() const;
		const FCompiledResource* FindResource(std::string_view Name) const;
	};

	// Compiles a description against the pass types it names.
	//
	// Fails rather than producing a partial result: a caller that got half a graph would render something
	// wrong, which is harder to notice than rendering nothing. The issues explain what was wrong.
	FRenderGraphCompileResult CompileRenderGraph(const FRenderGraphDesc& Graph, const FRenderGraphPassTypeRegistry& Types);
} // namespace Lime
