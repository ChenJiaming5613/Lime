#include "FrameGraph/FrameGraphDesc.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace Lime
{
	namespace
	{
		FFrameGraphIssue MakeIssue(FFrameGraphIssue::ESeverity Severity, std::string Message)
		{
			FFrameGraphIssue Issue;
			Issue.Severity = Severity;
			Issue.Message = std::move(Message);
			return Issue;
		}
	} // namespace

	void FFrameGraphDesc::Clear()
	{
		Passes.clear();
		Edges.clear();
		GraphOutputs.clear();
		Name = "Untitled";
	}

	const FFramePassInstance* FFrameGraphDesc::FindPass(std::string_view PassName) const
	{
		const auto Found =
		    std::find_if(Passes.begin(), Passes.end(), [PassName](const FFramePassInstance& Pass) { return Pass.Name == PassName; });
		return Found != Passes.end() ? &*Found : nullptr;
	}

	bool FFrameGraphDesc::AddPass(std::string InstanceName, std::string TypeName, const FFramePassTypeRegistry& Types,
	                              FFrameGraphIssue& OutIssue)
	{
		if (InstanceName.empty())
		{
			OutIssue = MakeIssue(FFrameGraphIssue::ESeverity::Error, "A pass needs a name; edges refer to passes by name.");
			return false;
		}

		if (FindPass(InstanceName) != nullptr)
		{
			OutIssue = MakeIssue(FFrameGraphIssue::ESeverity::Error,
			                     "A pass named '" + InstanceName + "' already exists; names have to be unique to be referable.");
			return false;
		}

		if (Types.Find(TypeName) == nullptr)
		{
			OutIssue = MakeIssue(FFrameGraphIssue::ESeverity::Error,
			                     "Unknown pass type '" + TypeName + "', so its inputs and outputs are not known.");
			return false;
		}

		FFramePassInstance Instance;
		Instance.Name = std::move(InstanceName);
		Instance.TypeName = std::move(TypeName);
		Passes.push_back(std::move(Instance));
		return true;
	}

	bool FFrameGraphDesc::RemovePass(std::string_view PassName)
	{
		const auto Found =
		    std::find_if(Passes.begin(), Passes.end(), [PassName](const FFramePassInstance& Pass) { return Pass.Name == PassName; });
		if (Found == Passes.end())
		{
			return false;
		}

		Passes.erase(Found);

		// Edges and outputs naming the removed pass go with it. The rest of this class treats a reference
		// to a missing pass as impossible, so they cannot be left behind.
		Edges.erase(std::remove_if(Edges.begin(), Edges.end(), [PassName](const FFrameGraphEdge& Edge)
		                           { return Edge.From.PassName == PassName || Edge.To.PassName == PassName; }),
		            Edges.end());

		GraphOutputs.erase(std::remove_if(GraphOutputs.begin(), GraphOutputs.end(),
		                                  [PassName](const FFrameGraphResourceRef& Output) { return Output.PassName == PassName; }),
		                   GraphOutputs.end());
		return true;
	}

	bool FFrameGraphDesc::AddEdge(const FFrameGraphEdge& Edge, const FFramePassTypeRegistry& Types, FFrameGraphIssue& OutIssue)
	{
		const FFramePassInstance* FromPass = FindPass(Edge.From.PassName);
		const FFramePassInstance* ToPass = FindPass(Edge.To.PassName);

		if (FromPass == nullptr || ToPass == nullptr)
		{
			const std::string Missing = FromPass == nullptr ? Edge.From.PassName : Edge.To.PassName;
			OutIssue =
			    MakeIssue(FFrameGraphIssue::ESeverity::Warning, "Edge refers to a pass that is not in the graph: '" + Missing + "'.");
			return false;
		}

		if (Edge.From.PassName == Edge.To.PassName)
		{
			OutIssue = MakeIssue(FFrameGraphIssue::ESeverity::Warning, "'" + Edge.From.PassName + "' cannot depend on itself.");
			return false;
		}

		if (Edge.Kind == EFrameEdgeKind::Data)
		{
			// Direction is checked against the type's declarations, not inferred from which end was dragged
			// first: an edge from an input to an output would read backwards at execution time.
			const FFramePassTypeDesc* FromType = Types.Find(FromPass->TypeName);
			const FFramePassTypeDesc* ToType = Types.Find(ToPass->TypeName);
			if (FromType == nullptr || ToType == nullptr)
			{
				OutIssue = MakeIssue(FFrameGraphIssue::ESeverity::Warning, "Edge endpoint has an unknown pass type.");
				return false;
			}

			if (FromType->FindOutput(Edge.From.ResourceName) == nullptr)
			{
				OutIssue = MakeIssue(FFrameGraphIssue::ESeverity::Warning,
				                     "'" + Edge.From.ToString() + "' is not an output of " + FromType->Name + ".");
				return false;
			}

			if (ToType->FindInput(Edge.To.ResourceName) == nullptr)
			{
				OutIssue = MakeIssue(FFrameGraphIssue::ESeverity::Warning,
				                     "'" + Edge.To.ToString() + "' is not an input of " + ToType->Name + ".");
				return false;
			}

			// One producer per input. Two writers into the same input has no defined meaning here, and
			// silently keeping the last one would look like the first edge simply vanished.
			const auto Occupied = std::find_if(Edges.begin(), Edges.end(), [&Edge](const FFrameGraphEdge& Existing)
			                                   { return Existing.Kind == EFrameEdgeKind::Data && Existing.To == Edge.To; });
			if (Occupied != Edges.end())
			{
				OutIssue = MakeIssue(FFrameGraphIssue::ESeverity::Warning,
				                     "'" + Edge.To.ToString() + "' already reads from '" + Occupied->From.ToString() + "'.");
				return false;
			}
		}

		if (std::find(Edges.begin(), Edges.end(), Edge) != Edges.end())
		{
			OutIssue = MakeIssue(FFrameGraphIssue::ESeverity::Warning, "That edge is already in the graph.");
			return false;
		}

		if (WouldCreateCycle(Edge))
		{
			OutIssue = MakeIssue(FFrameGraphIssue::ESeverity::Warning, "'" + Edge.From.PassName + "' to '" + Edge.To.PassName +
			                                                               "' would close a cycle; a frame graph is acyclic.");
			return false;
		}

		Edges.push_back(Edge);
		return true;
	}

	bool FFrameGraphDesc::RemoveEdge(SizeType EdgeIndex)
	{
		if (EdgeIndex >= Edges.size())
		{
			return false;
		}

		Edges.erase(Edges.begin() + static_cast<std::ptrdiff_t>(EdgeIndex));
		return true;
	}

	bool FFrameGraphDesc::ToggleGraphOutput(const FFrameGraphResourceRef& Output, const FFramePassTypeRegistry& Types,
	                                        FFrameGraphIssue& OutIssue)
	{
		const auto Existing = std::find(GraphOutputs.begin(), GraphOutputs.end(), Output);
		if (Existing != GraphOutputs.end())
		{
			GraphOutputs.erase(Existing);
			return false;
		}

		const FFramePassInstance* Pass = FindPass(Output.PassName);
		if (Pass == nullptr)
		{
			OutIssue = MakeIssue(FFrameGraphIssue::ESeverity::Warning, "No pass named '" + Output.PassName + "'.");
			return false;
		}

		// Only an output can be a graph output. Marking an input would claim the graph produces something
		// it merely consumes.
		const FFramePassTypeDesc* Type = Types.Find(Pass->TypeName);
		if (Type == nullptr || Type->FindOutput(Output.ResourceName) == nullptr)
		{
			OutIssue = MakeIssue(FFrameGraphIssue::ESeverity::Warning,
			                     "'" + Output.ToString() + "' is not an output, so it cannot be a graph output.");
			return false;
		}

		GraphOutputs.push_back(Output);
		return true;
	}

	bool FFrameGraphDesc::IsGraphOutput(const FFrameGraphResourceRef& Output) const
	{
		return std::find(GraphOutputs.begin(), GraphOutputs.end(), Output) != GraphOutputs.end();
	}

	bool FFrameGraphDesc::HasPath(std::string_view FromPass, std::string_view ToPass) const
	{
		if (FromPass == ToPass)
		{
			return true;
		}

		// Iterative rather than recursive. The depth is bounded by the pass count, which is small, but an
		// explicit stack costs nothing and removes the question entirely.
		std::vector<std::string> Pending{ std::string(FromPass) };
		std::unordered_set<std::string> Visited;

		while (!Pending.empty())
		{
			const std::string Current = std::move(Pending.back());
			Pending.pop_back();

			if (!Visited.insert(Current).second)
			{
				continue;
			}

			for (const FFrameGraphEdge& Edge : Edges)
			{
				if (Edge.From.PassName != Current)
				{
					continue;
				}
				if (Edge.To.PassName == ToPass)
				{
					return true;
				}
				Pending.push_back(Edge.To.PassName);
			}
		}

		return false;
	}

	bool FFrameGraphDesc::WouldCreateCycle(const FFrameGraphEdge& Edge) const
	{
		// The new edge runs From to To, so it closes a cycle exactly when To already reaches From.
		return HasPath(Edge.To.PassName, Edge.From.PassName);
	}

	std::vector<FFrameGraphIssue> FFrameGraphDesc::Validate(const FFramePassTypeRegistry& Types) const
	{
		std::vector<FFrameGraphIssue> Issues;

		std::unordered_set<std::string> SeenNames;
		for (const FFramePassInstance& Pass : Passes)
		{
			if (!SeenNames.insert(Pass.Name).second)
			{
				Issues.push_back(MakeIssue(FFrameGraphIssue::ESeverity::Error, "Duplicate pass name '" + Pass.Name + "'."));
			}
			if (Types.Find(Pass.TypeName) == nullptr)
			{
				Issues.push_back(
				    MakeIssue(FFrameGraphIssue::ESeverity::Error, "Pass '" + Pass.Name + "' has unknown type '" + Pass.TypeName + "'."));
			}
		}

		for (const FFrameGraphEdge& Edge : Edges)
		{
			const FFramePassInstance* FromPass = FindPass(Edge.From.PassName);
			const FFramePassInstance* ToPass = FindPass(Edge.To.PassName);
			if (FromPass == nullptr || ToPass == nullptr)
			{
				Issues.push_back(MakeIssue(FFrameGraphIssue::ESeverity::Error,
				                           "Edge '" + Edge.From.ToString() + "' to '" + Edge.To.ToString() + "' names a missing pass."));
				continue;
			}

			if (Edge.Kind != EFrameEdgeKind::Data)
			{
				continue;
			}

			const FFramePassTypeDesc* FromType = Types.Find(FromPass->TypeName);
			const FFramePassTypeDesc* ToType = Types.Find(ToPass->TypeName);
			if (FromType != nullptr && FromType->FindOutput(Edge.From.ResourceName) == nullptr)
			{
				Issues.push_back(MakeIssue(FFrameGraphIssue::ESeverity::Error, "'" + Edge.From.ToString() + "' is not an output."));
			}
			if (ToType != nullptr && ToType->FindInput(Edge.To.ResourceName) == nullptr)
			{
				Issues.push_back(MakeIssue(FFrameGraphIssue::ESeverity::Error, "'" + Edge.To.ToString() + "' is not an input."));
			}
		}

		for (const FFrameGraphResourceRef& Output : GraphOutputs)
		{
			const FFramePassInstance* Pass = FindPass(Output.PassName);
			if (Pass == nullptr)
			{
				Issues.push_back(
				    MakeIssue(FFrameGraphIssue::ESeverity::Error, "Graph output '" + Output.ToString() + "' names a missing pass."));
				continue;
			}

			const FFramePassTypeDesc* Type = Types.Find(Pass->TypeName);
			if (Type != nullptr && Type->FindOutput(Output.ResourceName) == nullptr)
			{
				Issues.push_back(MakeIssue(FFrameGraphIssue::ESeverity::Error,
				                           "Graph output '" + Output.ToString() + "' is not an output of " + Type->Name + "."));
			}
		}

		// A graph with nothing marked produces nothing: every pass would be culled by a real frame graph,
		// so this is reported as an error even though the graph is otherwise well formed.
		if (!Passes.empty() && GraphOutputs.empty())
		{
			Issues.push_back(MakeIssue(FFrameGraphIssue::ESeverity::Error, "No graph output is marked, so the graph produces nothing."));
		}

		// Unreachable inputs are worth mentioning but not fatal: a pass reading an unconnected input is a
		// graph still being assembled, which is the normal state while editing.
		for (const FFramePassInstance& Pass : Passes)
		{
			const FFramePassTypeDesc* Type = Types.Find(Pass.TypeName);
			if (Type == nullptr)
			{
				continue;
			}

			for (const FFrameResourceDesc& Input : Type->Inputs)
			{
				const FFrameGraphResourceRef Ref{ Pass.Name, Input.Name };
				const bool bConnected = std::any_of(Edges.begin(), Edges.end(), [&Ref](const FFrameGraphEdge& Edge)
				                                    { return Edge.Kind == EFrameEdgeKind::Data && Edge.To == Ref; });
				if (!bConnected)
				{
					Issues.push_back(
					    MakeIssue(FFrameGraphIssue::ESeverity::Warning, "'" + Ref.ToString() + "' has nothing connected to it."));
				}
			}
		}

		return Issues;
	}
} // namespace Lime
