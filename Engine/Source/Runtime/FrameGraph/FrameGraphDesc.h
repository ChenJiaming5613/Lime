// The graph: pass instances, the edges between them, and which outputs are the graph's own.
//
// This is the single source of truth. The panel draws from it and writes edits back into it, and saving
// serialises it; there is no second copy to drift. That is the whole reason the layout lives elsewhere
// and is recomputed rather than stored.
//
// Identity is by name, not by number. A pass instance is its name, a resource is "Pass.resource", and
// that is what the file contains. Numeric ids exist only while the editor is running, handed out by the
// panel for the node widget's benefit. Storing them would mean a hand edited file could renumber itself
// into a graph whose edges point at the wrong passes, and hand editing is a requirement here.
//
// Every mutation validates and reports rather than trusting the caller. The alternative is the same
// check written once in the panel and once in the loader, which is how the two end up disagreeing.

#pragma once

#include "FrameGraph/FramePassType.h"

#include <string>
#include <string_view>
#include <vector>

namespace Lime
{
	struct FFramePassInstance
	{
		// Unique within the graph; this is how edges refer to it.
		std::string Name;
		std::string TypeName;
	};

	class FFrameGraphDesc
	{
	public:
		// Shown in the inspector and written to the file, so a project can keep several graphs apart.
		const std::string& GetName() const { return Name; }
		void SetName(std::string InName) { Name = std::move(InName); }

		const std::vector<FFramePassInstance>& GetPasses() const { return Passes; }
		const std::vector<FFrameGraphEdge>& GetEdges() const { return Edges; }
		const std::vector<FFrameGraphResourceRef>& GetGraphOutputs() const { return GraphOutputs; }

		bool IsEmpty() const { return Passes.empty(); }

		void Clear();

		const FFramePassInstance* FindPass(std::string_view PassName) const;

		// Adds a pass instance. Fails when the name is taken or the type is unknown, since either would
		// leave edges unable to say which pass they mean.
		bool AddPass(std::string InstanceName, std::string TypeName, const FFramePassTypeRegistry& Types, FFrameGraphIssue& OutIssue);

		// Removes a pass along with every edge touching it and any graph output it declared. Leaving those
		// behind would produce dangling references, which is the one state the rest of this class assumes
		// cannot happen.
		bool RemovePass(std::string_view PassName);

		// Adds an edge after checking that both endpoints exist, that a data edge runs output to input, and
		// that it does not close a cycle.
		bool AddEdge(const FFrameGraphEdge& Edge, const FFramePassTypeRegistry& Types, FFrameGraphIssue& OutIssue);

		bool RemoveEdge(SizeType EdgeIndex);

		// Marks or unmarks one of a pass's outputs as an output of the whole graph. Returns the resulting
		// state so a caller need not query again.
		bool ToggleGraphOutput(const FFrameGraphResourceRef& Output, const FFramePassTypeRegistry& Types, FFrameGraphIssue& OutIssue);
		bool IsGraphOutput(const FFrameGraphResourceRef& Output) const;

		// Whether adding this edge would make the graph cyclic.
		//
		// Not defensive: this is a DAG by definition, and the layering pass walks the graph assuming it
		// terminates. A cycle there is an unbounded recursion with no diagnostic, so it is refused at the
		// point where it would be introduced.
		bool WouldCreateCycle(const FFrameGraphEdge& Edge) const;

		// Everything wrong with the graph as it stands, in one pass, for the inspector to list. Warnings
		// describe elements that could be dropped; errors describe a graph that cannot be built.
		std::vector<FFrameGraphIssue> Validate(const FFramePassTypeRegistry& Types) const;

		// Used by the loader, which has already validated each element and needs to build without paying
		// for the checks a second time.
		void AddPassUnchecked(FFramePassInstance Pass) { Passes.push_back(std::move(Pass)); }
		void AddEdgeUnchecked(FFrameGraphEdge Edge) { Edges.push_back(std::move(Edge)); }
		void AddGraphOutputUnchecked(FFrameGraphResourceRef Output) { GraphOutputs.push_back(std::move(Output)); }

	private:
		// Order dependent successors, used by both the cycle check and the layout.
		bool HasPath(std::string_view FromPass, std::string_view ToPass) const;

		std::string Name = "Untitled";
		std::vector<FFramePassInstance> Passes;
		std::vector<FFrameGraphEdge> Edges;
		std::vector<FFrameGraphResourceRef> GraphOutputs;
	};
} // namespace Lime
