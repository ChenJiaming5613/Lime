#include "RenderGraph/RenderGraphDesc.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace Lime
{
	namespace
	{
		FRenderGraphIssue MakeIssue(FRenderGraphIssue::ESeverity Severity, std::string Message)
		{
			FRenderGraphIssue Issue;
			Issue.Severity = Severity;
			Issue.Message = std::move(Message);
			return Issue;
		}
	} // namespace

	void FRenderGraphDesc::Clear()
	{
		Passes.clear();
		Edges.clear();
		GraphOutputs.clear();
		Name = "Untitled";
	}

	const FRenderGraphPassInstance* FRenderGraphDesc::FindPass(std::string_view PassName) const
	{
		const auto Found =
		    std::find_if(Passes.begin(), Passes.end(), [PassName](const FRenderGraphPassInstance& Pass) { return Pass.Name == PassName; });
		return Found != Passes.end() ? &*Found : nullptr;
	}

	bool FRenderGraphDesc::AddPass(std::string InstanceName, std::string TypeName, const FRenderGraphPassTypeRegistry& Types,
	                              FRenderGraphIssue& OutIssue)
	{
		if (InstanceName.empty())
		{
			OutIssue = MakeIssue(FRenderGraphIssue::ESeverity::Error, "A pass needs a name; edges refer to passes by name.");
			return false;
		}

		if (FindPass(InstanceName) != nullptr)
		{
			OutIssue = MakeIssue(FRenderGraphIssue::ESeverity::Error,
			                     "A pass named '" + InstanceName + "' already exists; names have to be unique to be referable.");
			return false;
		}

		if (Types.Find(TypeName) == nullptr)
		{
			OutIssue = MakeIssue(FRenderGraphIssue::ESeverity::Error,
			                     "Unknown pass type '" + TypeName + "', so its inputs and outputs are not known.");
			return false;
		}

		FRenderGraphPassInstance Instance;
		Instance.Name = std::move(InstanceName);
		Instance.TypeName = std::move(TypeName);
		Passes.push_back(std::move(Instance));
		return true;
	}

	bool FRenderGraphDesc::RemovePass(std::string_view PassName)
	{
		const auto Found =
		    std::find_if(Passes.begin(), Passes.end(), [PassName](const FRenderGraphPassInstance& Pass) { return Pass.Name == PassName; });
		if (Found == Passes.end())
		{
			return false;
		}

		Passes.erase(Found);

		// Edges and outputs naming the removed pass go with it. The rest of this class treats a reference
		// to a missing pass as impossible, so they cannot be left behind.
		Edges.erase(std::remove_if(Edges.begin(), Edges.end(), [PassName](const FRenderGraphEdge& Edge)
		                           { return Edge.From.PassName == PassName || Edge.To.PassName == PassName; }),
		            Edges.end());

		GraphOutputs.erase(std::remove_if(GraphOutputs.begin(), GraphOutputs.end(),
		                                  [PassName](const FRenderGraphResourceRef& Output) { return Output.PassName == PassName; }),
		                   GraphOutputs.end());
		return true;
	}

	bool FRenderGraphDesc::AddEdge(const FRenderGraphEdge& Edge, const FRenderGraphPassTypeRegistry& Types, FRenderGraphIssue& OutIssue)
	{
		const FRenderGraphPassInstance* FromPass = FindPass(Edge.From.PassName);
		const FRenderGraphPassInstance* ToPass = FindPass(Edge.To.PassName);

		if (FromPass == nullptr || ToPass == nullptr)
		{
			const std::string Missing = FromPass == nullptr ? Edge.From.PassName : Edge.To.PassName;
			OutIssue =
			    MakeIssue(FRenderGraphIssue::ESeverity::Warning, "Edge refers to a pass that is not in the graph: '" + Missing + "'.");
			return false;
		}

		if (Edge.From.PassName == Edge.To.PassName)
		{
			OutIssue = MakeIssue(FRenderGraphIssue::ESeverity::Warning, "'" + Edge.From.PassName + "' cannot depend on itself.");
			return false;
		}

		// Direction is checked against the type's declarations, not inferred from which end was dragged
		// first: an edge from an input to an output would read backwards at execution time.
		const FRenderGraphPassTypeDesc* FromType = Types.Find(FromPass->TypeName);
		const FRenderGraphPassTypeDesc* ToType = Types.Find(ToPass->TypeName);
		if (FromType == nullptr || ToType == nullptr)
		{
			OutIssue = MakeIssue(FRenderGraphIssue::ESeverity::Warning, "Edge endpoint has an unknown pass type.");
			return false;
		}

		if (FromType->FindOutput(Edge.From.ResourceName) == nullptr)
		{
			OutIssue = MakeIssue(FRenderGraphIssue::ESeverity::Warning,
			                     "'" + Edge.From.ToString() + "' is not an output of " + FromType->Name + ".");
			return false;
		}

		if (ToType->FindInput(Edge.To.ResourceName) == nullptr)
		{
			OutIssue = MakeIssue(FRenderGraphIssue::ESeverity::Warning,
			                     "'" + Edge.To.ToString() + "' is not an input of " + ToType->Name + ".");
			return false;
		}

		// One producer per input. Two writers into the same input has no defined meaning here, and
		// silently keeping the last one would look like the first edge simply vanished.
		const auto Occupied = std::find_if(Edges.begin(), Edges.end(),
		                                   [&Edge](const FRenderGraphEdge& Existing) { return Existing.To == Edge.To; });
		if (Occupied != Edges.end())
		{
			OutIssue = MakeIssue(FRenderGraphIssue::ESeverity::Warning,
			                     "'" + Edge.To.ToString() + "' already reads from '" + Occupied->From.ToString() + "'.");
			return false;
		}

		if (std::find(Edges.begin(), Edges.end(), Edge) != Edges.end())
		{
			OutIssue = MakeIssue(FRenderGraphIssue::ESeverity::Warning, "That edge is already in the graph.");
			return false;
		}

		if (WouldCreateCycle(Edge))
		{
			OutIssue = MakeIssue(FRenderGraphIssue::ESeverity::Warning, "'" + Edge.From.PassName + "' to '" + Edge.To.PassName +
			                                                               "' would close a cycle; a render graph is acyclic.");
			return false;
		}

		Edges.push_back(Edge);
		return true;
	}

	bool FRenderGraphDesc::RemoveEdge(SizeType EdgeIndex)
	{
		if (EdgeIndex >= Edges.size())
		{
			return false;
		}

		Edges.erase(Edges.begin() + static_cast<std::ptrdiff_t>(EdgeIndex));
		return true;
	}

	bool FRenderGraphDesc::ToggleGraphOutput(const FRenderGraphResourceRef& Output, const FRenderGraphPassTypeRegistry& Types,
	                                        FRenderGraphIssue& OutIssue)
	{
		const auto Existing = std::find(GraphOutputs.begin(), GraphOutputs.end(), Output);
		if (Existing != GraphOutputs.end())
		{
			GraphOutputs.erase(Existing);
			return false;
		}

		const FRenderGraphPassInstance* Pass = FindPass(Output.PassName);
		if (Pass == nullptr)
		{
			OutIssue = MakeIssue(FRenderGraphIssue::ESeverity::Warning, "No pass named '" + Output.PassName + "'.");
			return false;
		}

		// Only an output can be a graph output. Marking an input would claim the graph produces something
		// it merely consumes.
		const FRenderGraphPassTypeDesc* Type = Types.Find(Pass->TypeName);
		if (Type == nullptr || Type->FindOutput(Output.ResourceName) == nullptr)
		{
			OutIssue = MakeIssue(FRenderGraphIssue::ESeverity::Warning,
			                     "'" + Output.ToString() + "' is not an output, so it cannot be a graph output.");
			return false;
		}

		GraphOutputs.push_back(Output);
		return true;
	}

	bool FRenderGraphDesc::IsGraphOutput(const FRenderGraphResourceRef& Output) const
	{
		return std::find(GraphOutputs.begin(), GraphOutputs.end(), Output) != GraphOutputs.end();
	}

	int32 FRenderGraphDesc::FindGraphOutputSlot(const FRenderGraphResourceRef& Output) const
	{
		const auto Found = std::find(GraphOutputs.begin(), GraphOutputs.end(), Output);
		return Found == GraphOutputs.end() ? -1 : static_cast<int32>(std::distance(GraphOutputs.begin(), Found));
	}

	bool FRenderGraphDesc::HasPath(std::string_view FromPass, std::string_view ToPass) const
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

			for (const FRenderGraphEdge& Edge : Edges)
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

	bool FRenderGraphDesc::WouldCreateCycle(const FRenderGraphEdge& Edge) const
	{
		// The new edge runs From to To, so it closes a cycle exactly when To already reaches From.
		return HasPath(Edge.To.PassName, Edge.From.PassName);
	}

	std::vector<FRenderGraphIssue> FRenderGraphDesc::Validate(const FRenderGraphPassTypeRegistry& Types) const
	{
		std::vector<FRenderGraphIssue> Issues;

		std::unordered_set<std::string> SeenNames;
		for (const FRenderGraphPassInstance& Pass : Passes)
		{
			if (!SeenNames.insert(Pass.Name).second)
			{
				Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error, "Duplicate pass name '" + Pass.Name + "'."));
			}
			if (Types.Find(Pass.TypeName) == nullptr)
			{
				Issues.push_back(
				    MakeIssue(FRenderGraphIssue::ESeverity::Error, "Pass '" + Pass.Name + "' has unknown type '" + Pass.TypeName + "'."));
			}
		}

		for (const FRenderGraphEdge& Edge : Edges)
		{
			const FRenderGraphPassInstance* FromPass = FindPass(Edge.From.PassName);
			const FRenderGraphPassInstance* ToPass = FindPass(Edge.To.PassName);
			if (FromPass == nullptr || ToPass == nullptr)
			{
				Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error,
				                           "Edge '" + Edge.From.ToString() + "' to '" + Edge.To.ToString() + "' names a missing pass."));
				continue;
			}

			const FRenderGraphPassTypeDesc* FromType = Types.Find(FromPass->TypeName);
			const FRenderGraphPassTypeDesc* ToType = Types.Find(ToPass->TypeName);
			if (FromType != nullptr && FromType->FindOutput(Edge.From.ResourceName) == nullptr)
			{
				Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error, "'" + Edge.From.ToString() + "' is not an output."));
			}
			if (ToType != nullptr && ToType->FindInput(Edge.To.ResourceName) == nullptr)
			{
				Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error, "'" + Edge.To.ToString() + "' is not an input."));
			}
		}

		for (const FRenderGraphResourceRef& Output : GraphOutputs)
		{
			const FRenderGraphPassInstance* Pass = FindPass(Output.PassName);
			if (Pass == nullptr)
			{
				Issues.push_back(
				    MakeIssue(FRenderGraphIssue::ESeverity::Error, "Graph output '" + Output.ToString() + "' names a missing pass."));
				continue;
			}

			const FRenderGraphPassTypeDesc* Type = Types.Find(Pass->TypeName);
			if (Type != nullptr && Type->FindOutput(Output.ResourceName) == nullptr)
			{
				Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error,
				                           "Graph output '" + Output.ToString() + "' is not an output of " + Type->Name + "."));
			}
		}

		// A graph with nothing marked produces nothing: every pass would be culled by a real render graph,
		// so this is reported as an error even though the graph is otherwise well formed.
		if (!Passes.empty() && GraphOutputs.empty())
		{
			Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error, "No graph output is marked, so the graph produces nothing."));
		}

		// Unreachable inputs are worth mentioning but not fatal: a pass reading an unconnected input is a
		// graph still being assembled, which is the normal state while editing. An input the pass declared
		// optional is silent, because leaving it unconnected is a supported configuration rather than an
		// omission.
		for (const FRenderGraphPassInstance& Pass : Passes)
		{
			const FRenderGraphPassTypeDesc* Type = Types.Find(Pass.TypeName);
			if (Type == nullptr)
			{
				continue;
			}

			for (const FRenderGraphResourceDesc& Input : Type->Inputs)
			{
				if (Input.bOptional)
				{
					continue;
				}

				const FRenderGraphResourceRef Ref{ Pass.Name, Input.Name };
				const bool bConnected =
				    std::any_of(Edges.begin(), Edges.end(), [&Ref](const FRenderGraphEdge& Edge) { return Edge.To == Ref; });
				if (!bConnected)
				{
					Issues.push_back(
					    MakeIssue(FRenderGraphIssue::ESeverity::Warning, "'" + Ref.ToString() + "' has nothing connected to it."));
				}
			}
		}

		// Format and size disagreements across an edge. Reported here as well as during compilation so the
		// editor can show them while the graph is being assembled, rather than only at startup.
		for (const FRenderGraphEdge& Edge : Edges)
		{
			const FRenderGraphPassInstance* FromPass = FindPass(Edge.From.PassName);
			const FRenderGraphPassInstance* ToPass = FindPass(Edge.To.PassName);
			if (FromPass == nullptr || ToPass == nullptr)
			{
				continue;
			}

			const FRenderGraphPassTypeDesc* FromType = Types.Find(FromPass->TypeName);
			const FRenderGraphPassTypeDesc* ToType = Types.Find(ToPass->TypeName);
			if (FromType == nullptr || ToType == nullptr)
			{
				continue;
			}

			const FRenderGraphResourceDesc* Produced = FromType->FindOutput(Edge.From.ResourceName);
			const FRenderGraphResourceDesc* Consumed = ToType->FindInput(Edge.To.ResourceName);
			if (Produced == nullptr || Consumed == nullptr)
			{
				continue;
			}

			FRenderGraphResourceDesc Merged = *Produced;
			FRenderGraphIssue MergeIssue;
			if (!MergeResourceDesc(Merged, *Consumed, Edge.From.ToString(), MergeIssue))
			{
				Issues.push_back(std::move(MergeIssue));
			}
		}

		return Issues;
	}
} // namespace Lime
