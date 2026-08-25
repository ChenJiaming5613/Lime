// Frame graph vocabulary: what a pass consumes and produces, and how passes depend on each other.
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

	// Whether an edge carries data or only constrains order.
	//
	// Both exist because a frame graph has two independent reasons for one pass to precede another. A
	// data edge says "this pass reads what that pass wrote", which is most of the graph. An execution
	// edge says "run this one first anyway", which is how a pass with side effects outside the graph, or
	// one whose output nothing reads yet, keeps its place in the order.
	enum class EFrameEdgeKind : uint8
	{
		Data,
		Execution
	};

	// A resource a pass type declares. Named, because a pass refers to its own resources by name and so
	// does the file: "ShadowCaster.depth" is the whole identity.
	struct FFrameResourceDesc
	{
		std::string Name;
		EFrameResourceKind Kind = EFrameResourceKind::Texture;
		// Free text for now, shown in the inspector. A real frame graph would carry a format enum and a
		// size policy; recording the intent as text keeps the fake graph honest about what it does not
		// yet decide.
		std::string Format;
	};

	// Points at one resource on one pass instance.
	//
	// A pass name with an empty resource name means the pass itself, which is what an execution edge
	// connects. Keeping both cases in one type avoids a second edge structure whose only difference is
	// the missing field.
	struct FFrameGraphResourceRef
	{
		std::string PassName;
		std::string ResourceName;

		bool IsPassOnly() const { return ResourceName.empty(); }

		bool operator==(const FFrameGraphResourceRef& Other) const
		{
			return PassName == Other.PassName && ResourceName == Other.ResourceName;
		}

		// "Pass.resource", or just "Pass" for an execution endpoint. Used in diagnostics and in the file,
		// so the two always agree on how a reference reads.
		std::string ToString() const { return IsPassOnly() ? PassName : PassName + "." + ResourceName; }
	};

	struct FFrameGraphEdge
	{
		EFrameEdgeKind Kind = EFrameEdgeKind::Data;
		FFrameGraphResourceRef From;
		FFrameGraphResourceRef To;

		bool operator==(const FFrameGraphEdge& Other) const { return Kind == Other.Kind && From == Other.From && To == Other.To; }
	};

	// Something wrong with a graph, phrased for a human.
	//
	// Severity decides what the caller does rather than what it prints: a warning means one element was
	// dropped and the rest is usable, an error means the graph cannot be built. Loading reports warnings
	// and continues, which is what lets a hand edited file with one bad edge still open.
	struct FFrameGraphIssue
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
