// What kinds of pass exist, and what each one reads and writes.
//
// A pass type is the template; a pass instance in the graph is one use of it. The distinction matters
// because the same type appears more than once in a real pipeline: two post process steps are two
// instances of one type, with different names and different neighbours.
//
// The registry starts empty and is filled from the render passes the renderer has registered, each
// reflecting its own inputs and outputs. It is a lookup table rather than an enum so that changing
// where the entries come from does not change any code that reads them.

#pragma once

#include "RenderGraph/RenderGraphTypes.h"

#include <string_view>
#include <vector>

namespace Lime
{
	struct FRenderGraphPassTypeDesc
	{
		std::string Name;
		// Free text shown in the inspector, so a reader can tell what a pass is for without opening code.
		std::string Description;
		std::vector<FRenderGraphResourceDesc> Inputs;
		std::vector<FRenderGraphResourceDesc> Outputs;

		const FRenderGraphResourceDesc* FindInput(std::string_view ResourceName) const;
		const FRenderGraphResourceDesc* FindOutput(std::string_view ResourceName) const;
	};

	// Lookup for pass types. Not a singleton: the panel owns one, and a test owns its own with whatever
	// types the case needs, which keeps tests from depending on the built-in set staying unchanged.
	//
	// Starts empty. A default constructed registry describes no pass types at all, which is the honest
	// state before the renderer has reflected its passes into it: the alternative was a hard coded set,
	// and a graph validated against invented types says nothing about whether it will run.
	class FRenderGraphPassTypeRegistry
	{
	public:
		void Clear() { Types.clear(); }

		// Later registrations replace an existing type of the same name, so a project can override a
		// built-in without the registry needing a removal step.
		void Register(FRenderGraphPassTypeDesc Type);

		const FRenderGraphPassTypeDesc* Find(std::string_view TypeName) const;
		const std::vector<FRenderGraphPassTypeDesc>& GetAll() const { return Types; }
		bool IsEmpty() const { return Types.empty(); }

	private:
		std::vector<FRenderGraphPassTypeDesc> Types;
	};

	// Convenience for declaring a texture resource, used by passes describing their own I/O and by tests
	// building a registry by hand.
	//
	// Format and size default to unspecified, which means the graph decides; a caller that needs a
	// particular value sets it on the returned description.
	FRenderGraphResourceDesc MakeTextureResource(std::string Name, ERenderGraphResourceVisibility Visibility,
	                                             nvrhi::Format Format = nvrhi::Format::UNKNOWN);
} // namespace Lime
