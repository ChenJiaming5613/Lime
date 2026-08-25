// What kinds of pass exist, and what each one reads and writes.
//
// A pass type is the template; a pass instance in the graph is one use of it. The distinction matters
// because the same type appears more than once in a real pipeline: two post process steps are two
// instances of one type, with different names and different neighbours.
//
// The built-in types are hard coded for now. A real frame graph would fill this from the render passes
// the renderer has registered, each reflecting its own inputs and outputs, which is why the registry is
// a lookup table rather than an enum: replacing the source of the entries should not change any code
// that reads them.

#pragma once

#include "FrameGraph/FrameGraphTypes.h"

#include <string_view>
#include <vector>

namespace Lime
{
	struct FFramePassTypeDesc
	{
		std::string Name;
		// Free text shown in the inspector, so a reader can tell what a pass is for without opening code.
		std::string Description;
		std::vector<FFrameResourceDesc> Inputs;
		std::vector<FFrameResourceDesc> Outputs;

		const FFrameResourceDesc* FindInput(std::string_view ResourceName) const;
		const FFrameResourceDesc* FindOutput(std::string_view ResourceName) const;
	};

	// Lookup for pass types. Not a singleton: the panel owns one, and a test owns its own with whatever
	// types the case needs, which keeps tests from depending on the built-in set staying unchanged.
	class FFramePassTypeRegistry
	{
	public:
		// Populates the built-in types. Called by the constructor, so a default constructed registry is
		// immediately usable; a test wanting an empty one calls Clear.
		FFramePassTypeRegistry();

		void Clear() { Types.clear(); }

		// Later registrations replace an existing type of the same name, so a project can override a
		// built-in without the registry needing a removal step.
		void Register(FFramePassTypeDesc Type);

		const FFramePassTypeDesc* Find(std::string_view TypeName) const;
		const std::vector<FFramePassTypeDesc>& GetAll() const { return Types; }

	private:
		std::vector<FFramePassTypeDesc> Types;
	};
} // namespace Lime
