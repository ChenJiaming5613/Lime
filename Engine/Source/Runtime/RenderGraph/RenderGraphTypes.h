// Render graph vocabulary: what a pass consumes and produces, and how passes depend on each other.
//
// These are the pipeline level resources, not the per object textures a material samples. A shadow
// caster pass produces one depth target for the whole frame; a hundred meshes rendering into it does
// not make a hundred resources.
//
// Only textures for now. The kind is spelled out as an enum rather than assumed so that adding
// buffers later is a new enumerator instead of a change to every signature that passes one around.

#pragma once

#include "Core/CoreTypes.h"

#include <string>

namespace Lime
{
	enum class EFrameResourceKind : uint8
	{
		Texture
	};

	// A resource a pass type declares. Named, because a pass refers to its own resources by name and so
	// does the file: "ShadowCaster.depth" is the whole identity.
	struct FRenderGraphResourceDesc
	{
		std::string Name;
		EFrameResourceKind Kind = EFrameResourceKind::Texture;
		// Free text for now, shown in the inspector. A real render graph would carry a format enum and a
		// size policy; recording the intent as text keeps the fake graph honest about what it does not
		// yet decide.
		std::string Format;
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
} // namespace Lime
