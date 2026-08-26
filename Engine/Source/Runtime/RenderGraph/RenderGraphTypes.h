// Render graph vocabulary: what a pass consumes and produces, and how passes depend on each other.
//
// These are the pipeline level resources, not the per object textures a material samples. A shadow
// caster pass produces one depth target for the whole frame; a hundred meshes rendering into it does
// not make a hundred resources.
//
// Only textures for now. The kind is spelled out as an enum rather than assumed so that adding
// buffers later is a new enumerator instead of a change to every signature that passes one around.
//
// Formats are nvrhi::Format rather than a private enumeration. The alternative was a graph-local enum
// mapped to nvrhi in the renderer, which would have kept this module free of the RHI; that trade was
// not worth making, because linking nvrhi does not require a graphics device and the test binary
// already links LimeRHI. Everything used here (getFormatInfo, FormatToString) is a pure function over
// the enum, so the graph's validation stays testable without one.

#pragma once

#include "Core/CoreTypes.h"

#include <nvrhi/nvrhi.h>

#include <string>

namespace Lime
{
	enum class ERenderGraphResourceKind : uint8
	{
		Texture
	};

	// Whether a field is read, written, or both.
	//
	// InputOutput exists for a pass that reads and writes the same resource in place. It is not the same
	// as declaring an input and an output of the same name: those would be two resources connected by an
	// edge, where this is one.
	enum class ERenderGraphResourceVisibility : uint8
	{
		Input,
		Output,
		InputOutput
	};

	// A resource a pass type declares. Named, because a pass refers to its own resources by name and so
	// does the file: "ShadowCaster.depth" is the whole identity.
	//
	// Unspecified values mean "whatever the graph decides": Format::UNKNOWN takes the graph's default
	// format, and a zero Width or Height takes the graph's size. A pass only pins these down when it
	// genuinely requires a particular value, such as a shadow map that is 2048 square regardless of the
	// window. That distinction is what lets one pass be reused at different resolutions.
	struct FRenderGraphResourceDesc
	{
		std::string Name;
		ERenderGraphResourceKind Kind = ERenderGraphResourceKind::Texture;
		nvrhi::Format Format = nvrhi::Format::UNKNOWN;
		// 0 means the graph's default size. A non-zero value is a hard requirement.
		uint32 Width = 0;
		uint32 Height = 0;
		ERenderGraphResourceVisibility Visibility = ERenderGraphResourceVisibility::Input;
		// When true the pass still runs with this input unconnected, with reduced behaviour: a lit pass
		// with no shadow map draws without shadows rather than failing to compile.
		bool bOptional = false;
		// Shown in the inspector, so a reader can tell what a resource carries without opening code.
		std::string Description;

		// True when the format is a depth or depth-stencil format. Decides whether the resource becomes a
		// depth attachment and whether it needs a depth clear value.
		bool IsDepth() const;
	};

	// Points at one resource on one pass instance.
	//
	// Both parts are required: an edge connects a resource to a resource. A reference with no resource
	// name is malformed rather than meaningful, and IsPassOnly is what the loader uses to reject it.
	struct FRenderGraphResourceRef
	{
		std::string PassName;
		std::string ResourceName;

		bool IsPassOnly() const { return ResourceName.empty(); }

		bool operator==(const FRenderGraphResourceRef& Other) const
		{
			return PassName == Other.PassName && ResourceName == Other.ResourceName;
		}

		// "Pass.resource". Used in diagnostics and in the file, so the two always agree on how a
		// reference reads.
		std::string ToString() const { return IsPassOnly() ? PassName : PassName + "." + ResourceName; }
	};

	struct FRenderGraphEdge
	{
		FRenderGraphResourceRef From;
		FRenderGraphResourceRef To;

		bool operator==(const FRenderGraphEdge& Other) const { return From == Other.From && To == Other.To; }
	};

	// Something wrong with a graph, phrased for a human.
	//
	// Severity decides what the caller does rather than what it prints: a warning means one element was
	// dropped and the rest is usable, an error means the graph cannot be built. Loading reports warnings
	// and continues, which is what lets a hand edited file with one bad edge still open.
	struct FRenderGraphIssue
	{
		enum class ESeverity : uint8
		{
			Warning,
			Error
		};

		ESeverity Severity = ESeverity::Warning;
		std::string Message;

		bool IsError() const { return Severity == ESeverity::Error; }
	};

	// Reconciles the two ends of an edge into one resource description.
	//
	// A property specified on one end and left unspecified on the other is taken from the end that
	// specified it, which is what lets a pass ask for "whatever size the graph is" and still end up with
	// the producer's dimensions. Two ends specifying the same property differently is a genuine conflict
	// rather than something to resolve silently, so it fails and names the property that disagreed.
	//
	// ContextName is the resource being merged, used only to phrase the issue.
	bool MergeResourceDesc(FRenderGraphResourceDesc& InOut, const FRenderGraphResourceDesc& Other, const std::string& ContextName,
	                       FRenderGraphIssue& OutIssue);
} // namespace Lime
