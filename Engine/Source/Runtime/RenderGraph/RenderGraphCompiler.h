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
		// Transient resources are created with the graph; imported ones are bound by the engine each frame
		// and never allocated here.
		ERenderGraphResourceSource Source = ERenderGraphResourceSource::Transient;
		// The engine wide slot an imported resource resolves to, e.g. "$BackBuffer". Empty when transient.
		std::string ImportName;

		bool IsImported() const { return Source == ERenderGraphResourceSource::Imported; }
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
		// What to do with the target before this pass writes it. Only read for an output binding.
		//
		// Resolved during the compile rather than at execution time, because it depends on what else writes
		// the same resource: the executor sees one pass at a time and could not decide it.
		ERenderGraphLoadAction LoadAction = ERenderGraphLoadAction::Unspecified;
	};

	struct FCompiledPass
	{
		std::string PassName;
		std::string TypeName;
		std::vector<FCompiledPassBinding> Bindings;

		// Parameter overrides copied from the description, applied to the pass instance at runtime.
		// Null means "run with defaults".
		FJson Settings;

		// True when this pass writes something the graph was asked to output, directly or through others.
		// Passes that do not are culled and never appear here; the flag is kept for diagnostics.
		bool bContributesToOutput = true;

		// True when the engine appended this pass rather than the graph file describing it.
		//
		// The panel and the JSON both work from FRenderGraphDesc, which never sees an injected pass, so this
		// is not what hides it. It is here so diagnostics can say where a pass came from, and so the
		// renderer knows to bind an existing instance instead of building a new one from the factory.
		bool bInjected = false;

		// True when the executor must close and submit the command list before running this pass.
		//
		// Explicit rather than inferred: this is a synchronisation decision, and the graph shape does not
		// imply it. A pass sampling a texture an earlier pass wrote is safe within one command list, since
		// nvrhi tracks that; a pass that has to observe work the engine submitted outside the graph is not.
		bool bBeginsNewSubmission = false;
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
