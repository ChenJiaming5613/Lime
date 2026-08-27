#include "RenderGraph/RenderGraphCompiler.h"

#include <algorithm>
#include <map>
#include <unordered_map>
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

		// Key for a field lookup. The separator cannot appear in a pass name, so "A|out" is unambiguous.
		std::string MakeFieldKey(const std::string& PassName, const std::string& FieldName)
		{
			return PassName + "|" + FieldName;
		}

		// Everything the compile needs about one pass, gathered once so the stages below do not repeatedly
		// look types up by name.
		struct FPassContext
		{
			const FRenderGraphPassInstance* Instance = nullptr;
			const FRenderGraphPassTypeDesc* Type = nullptr;
		};

		// Builds the per pass context, reporting any pass whose type is unknown.
		//
		// An unknown type is fatal rather than skippable: the pass declares no fields, so every edge
		// touching it would look like it referenced a missing resource and the real cause would be buried
		// under consequences.
		bool GatherPasses(const FRenderGraphDesc& Graph, const FRenderGraphPassTypeRegistry& Types, std::vector<FPassContext>& OutContexts,
		                  std::unordered_map<std::string, SizeType>& OutIndexByName, std::vector<FRenderGraphIssue>& OutIssues)
		{
			bool bOk = true;
			const std::vector<FRenderGraphPassInstance>& Passes = Graph.GetPasses();
			OutContexts.resize(Passes.size());

			for (SizeType Index = 0; Index < Passes.size(); ++Index)
			{
				const FRenderGraphPassInstance& Pass = Passes[Index];
				OutContexts[Index].Instance = &Pass;
				OutContexts[Index].Type = Types.Find(Pass.TypeName);

				if (OutContexts[Index].Type == nullptr)
				{
					OutIssues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error,
					                              "Pass '" + Pass.Name + "' has unknown type '" + Pass.TypeName + "'."));
					bOk = false;
				}

				if (!OutIndexByName.emplace(Pass.Name, Index).second)
				{
					OutIssues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error, "Duplicate pass name '" + Pass.Name + "'."));
					bOk = false;
				}
			}

			return bOk;
		}

		// Orders the passes so every producer precedes its consumers.
		//
		// Kahn's algorithm, taking the lowest index whenever several passes are ready. That tie break is
		// what makes the order depend only on the file rather than on which edge happened to be listed
		// first, so the same graph always executes the same way.
		//
		// A cycle leaves passes unvisited, which is reported rather than worked around: unlike the layout,
		// which draws a cycle as best it can, an execution order containing one has no meaning.
		bool TopologicalOrder(const FRenderGraphDesc& Graph, const std::unordered_map<std::string, SizeType>& IndexByName,
		                      std::vector<SizeType>& OutOrder, std::vector<FRenderGraphIssue>& OutIssues)
		{
			const SizeType Count = Graph.GetPasses().size();
			std::vector<std::vector<SizeType>> Successors(Count);
			std::vector<SizeType> Remaining(Count, 0);

			// Duplicate edges between the same pair would inflate the in-degree and stall the walk, so each
			// dependency is counted once however many resources connect the two passes.
			std::unordered_set<std::string> SeenPairs;
			for (const FRenderGraphEdge& Edge : Graph.GetEdges())
			{
				const auto From = IndexByName.find(Edge.From.PassName);
				const auto To = IndexByName.find(Edge.To.PassName);
				if (From == IndexByName.end() || To == IndexByName.end() || From->second == To->second)
				{
					continue;
				}

				if (!SeenPairs.insert(MakeFieldKey(Edge.From.PassName, Edge.To.PassName)).second)
				{
					continue;
				}

				Successors[From->second].push_back(To->second);
				++Remaining[To->second];
			}

			std::vector<SizeType> Ready;
			for (SizeType Index = 0; Index < Count; ++Index)
			{
				if (Remaining[Index] == 0)
				{
					Ready.push_back(Index);
				}
			}

			OutOrder.clear();
			OutOrder.reserve(Count);
			while (!Ready.empty())
			{
				const auto Next = std::min_element(Ready.begin(), Ready.end());
				const SizeType Current = *Next;
				Ready.erase(Next);
				OutOrder.push_back(Current);

				for (const SizeType Successor : Successors[Current])
				{
					if (--Remaining[Successor] == 0)
					{
						Ready.push_back(Successor);
					}
				}
			}

			if (OutOrder.size() != Count)
			{
				// Names the passes still held up, since that set is the cycle plus whatever sits downstream
				// of it, and is more useful than saying only that one exists.
				std::string Stuck;
				for (SizeType Index = 0; Index < Count; ++Index)
				{
					if (Remaining[Index] > 0)
					{
						if (!Stuck.empty())
						{
							Stuck += ", ";
						}
						Stuck += Graph.GetPasses()[Index].Name;
					}
				}
				OutIssues.push_back(
				    MakeIssue(FRenderGraphIssue::ESeverity::Error, "The graph contains a cycle involving: " + Stuck + "."));
				return false;
			}

			return true;
		}

		// Assigns every field of every pass to a resource, merging the two ends of each edge into one.
		//
		// Walked in execution order so a resource is created by the pass that writes it before any reader is
		// seen, which is what makes the producing field the natural name.
		bool BuildResources(const FRenderGraphDesc& Graph, const std::vector<FPassContext>& Contexts, const std::vector<SizeType>& Order,
		                    std::vector<FCompiledResource>& OutResources, std::unordered_map<std::string, SizeType>& OutResourceByField,
		                    std::vector<FRenderGraphIssue>& OutIssues)
		{
			bool bOk = true;

			// Which field produces the resource a consumer field reads, taken from the edges.
			std::unordered_map<std::string, FRenderGraphResourceRef> ProducerOf;
			for (const FRenderGraphEdge& Edge : Graph.GetEdges())
			{
				const std::string Key = MakeFieldKey(Edge.To.PassName, Edge.To.ResourceName);
				const auto Existing = ProducerOf.find(Key);
				if (Existing != ProducerOf.end())
				{
					// Two producers for one input has no defined meaning: the reader would see whichever ran
					// last, which is not something the graph states anywhere.
					OutIssues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error,
					                              "'" + Edge.To.ToString() + "' is written by both '" + Existing->second.ToString() +
					                                  "' and '" + Edge.From.ToString() + "'."));
					bOk = false;
					continue;
				}
				ProducerOf.emplace(Key, Edge.From);
			}

			const auto DeclareResource = [&OutResources](const FRenderGraphResourceDesc& Desc, const std::string& Name)
			{
				FCompiledResource Resource;
				Resource.Name = Name;
				Resource.Kind = Desc.Kind;
				Resource.Format = Desc.Format;
				Resource.Width = Desc.Width;
				Resource.Height = Desc.Height;
				Resource.bIsDepth = Desc.IsDepth();
				OutResources.push_back(std::move(Resource));
				return OutResources.size() - 1;
			};

			for (const SizeType PassIndex : Order)
			{
				const FPassContext& Context = Contexts[PassIndex];
				if (Context.Type == nullptr)
				{
					continue;
				}

				// Outputs first: a pass that reads and writes in the same frame still produces its own
				// resources, and a reader further along needs them to exist.
				for (const FRenderGraphResourceDesc& Output : Context.Type->Outputs)
				{
					const std::string Field = MakeFieldKey(Context.Instance->Name, Output.Name);
					const std::string ResourceName = Context.Instance->Name + "." + Output.Name;
					const SizeType Index = DeclareResource(Output, ResourceName);
					OutResources[Index].bUsedAsRenderTarget = true;
					OutResourceByField.emplace(Field, Index);
				}

				for (const FRenderGraphResourceDesc& Input : Context.Type->Inputs)
				{
					const std::string Field = MakeFieldKey(Context.Instance->Name, Input.Name);
					const auto Producer = ProducerOf.find(Field);
					if (Producer == ProducerOf.end())
					{
						// Nothing connected. Whether that is acceptable is decided by the satisfaction check,
						// which knows about optional inputs; here it simply has no resource to bind.
						continue;
					}

					const auto Existing = OutResourceByField.find(MakeFieldKey(Producer->second.PassName, Producer->second.ResourceName));
					if (Existing == OutResourceByField.end())
					{
						OutIssues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error,
						                              "'" + Producer->second.ToString() + "' is not an output of any pass, so '" +
						                                  Context.Instance->Name + "." + Input.Name + "' has nothing to read."));
						bOk = false;
						continue;
					}

					// The consumer's declaration is reconciled with the resource as it stands, so a reader
					// asking for a specific format against a producer that left it open pins it down, and a
					// genuine disagreement is reported rather than silently resolved.
					FCompiledResource& Resource = OutResources[Existing->second];
					FRenderGraphResourceDesc Merged;
					Merged.Kind = Resource.Kind;
					Merged.Format = Resource.Format;
					Merged.Width = Resource.Width;
					Merged.Height = Resource.Height;

					FRenderGraphIssue MergeIssue;
					if (!MergeResourceDesc(Merged, Input, Resource.Name, MergeIssue))
					{
						OutIssues.push_back(std::move(MergeIssue));
						bOk = false;
						continue;
					}

					Resource.Format = Merged.Format;
					Resource.Width = Merged.Width;
					Resource.Height = Merged.Height;
					Resource.bIsDepth = Merged.IsDepth();
					Resource.bUsedAsShaderResource = true;
					OutResourceByField.emplace(Field, Existing->second);
				}
			}

			return bOk;
		}

		// Reports inputs that are neither connected nor optional.
		//
		// A pass declaring an input it cannot run without, left unconnected, would read whatever was bound
		// last or nothing at all. That is a configuration error rather than something to discover on the
		// GPU, so it stops the compile.
		bool CheckInputsSatisfied(const std::vector<FPassContext>& Contexts, const std::vector<SizeType>& Order,
		                          const std::unordered_map<std::string, SizeType>& ResourceByField,
		                          std::vector<FRenderGraphIssue>& OutIssues)
		{
			bool bOk = true;
			for (const SizeType PassIndex : Order)
			{
				const FPassContext& Context = Contexts[PassIndex];
				if (Context.Type == nullptr)
				{
					continue;
				}

				for (const FRenderGraphResourceDesc& Input : Context.Type->Inputs)
				{
					if (Input.bOptional)
					{
						continue;
					}

					if (ResourceByField.find(MakeFieldKey(Context.Instance->Name, Input.Name)) == ResourceByField.end())
					{
						OutIssues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error,
						                              "'" + Context.Instance->Name + "." + Input.Name +
						                                  "' is required but nothing is connected to it."));
						bOk = false;
					}
				}
			}

			return bOk;
		}

		// The set of passes that can reach a graph output, walked backwards from the marked resources.
		//
		// This is what decides whether a pass runs. A pass whose results nothing asks for is work with no
		// observable effect, so it is dropped rather than executed; that is also the answer to what happens
		// when a pass is left unconnected in the editor.
		std::unordered_set<SizeType> FindContributingPasses(const FRenderGraphDesc& Graph,
		                                                    const std::unordered_map<std::string, SizeType>& IndexByName)
		{
			std::unordered_map<SizeType, std::vector<SizeType>> Predecessors;
			for (const FRenderGraphEdge& Edge : Graph.GetEdges())
			{
				const auto From = IndexByName.find(Edge.From.PassName);
				const auto To = IndexByName.find(Edge.To.PassName);
				if (From == IndexByName.end() || To == IndexByName.end())
				{
					continue;
				}
				Predecessors[To->second].push_back(From->second);
			}

			std::unordered_set<SizeType> Contributing;
			std::vector<SizeType> Pending;
			for (const FRenderGraphResourceRef& Output : Graph.GetGraphOutputs())
			{
				const auto Found = IndexByName.find(Output.PassName);
				if (Found != IndexByName.end() && Contributing.insert(Found->second).second)
				{
					Pending.push_back(Found->second);
				}
			}

			while (!Pending.empty())
			{
				const SizeType Current = Pending.back();
				Pending.pop_back();

				const auto Found = Predecessors.find(Current);
				if (Found == Predecessors.end())
				{
					continue;
				}

				for (const SizeType Predecessor : Found->second)
				{
					if (Contributing.insert(Predecessor).second)
					{
						Pending.push_back(Predecessor);
					}
				}
			}

			return Contributing;
		}
	} // namespace

	SizeType FRenderGraphCompileResult::CountErrors() const
	{
		return static_cast<SizeType>(
		    std::count_if(Issues.begin(), Issues.end(), [](const FRenderGraphIssue& Issue) { return Issue.IsError(); }));
	}

	const FCompiledResource* FRenderGraphCompileResult::FindResource(std::string_view Name) const
	{
		const auto Found =
		    std::find_if(Resources.begin(), Resources.end(), [Name](const FCompiledResource& Resource) { return Resource.Name == Name; });
		return Found != Resources.end() ? &*Found : nullptr;
	}

	FRenderGraphCompileResult CompileRenderGraph(const FRenderGraphDesc& Graph, const FRenderGraphPassTypeRegistry& Types)
	{
		FRenderGraphCompileResult Result;

		if (Graph.GetPasses().empty())
		{
			Result.Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error, "The graph has no passes."));
			return Result;
		}

		// Checked before anything else: with nothing marked, the cull below would drop every pass and the
		// result would be an empty graph that looks like it compiled.
		if (Graph.GetGraphOutputs().empty())
		{
			Result.Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error, "No graph output is marked, so the graph produces nothing."));
			return Result;
		}

		std::vector<FPassContext> Contexts;
		std::unordered_map<std::string, SizeType> IndexByName;
		if (!GatherPasses(Graph, Types, Contexts, IndexByName, Result.Issues))
		{
			return Result;
		}

		std::vector<SizeType> Order;
		if (!TopologicalOrder(Graph, IndexByName, Order, Result.Issues))
		{
			return Result;
		}

		// The graph outputs have to name real outputs before the resource table is trusted to contain them.
		bool bOutputsValid = true;
		for (const FRenderGraphResourceRef& Output : Graph.GetGraphOutputs())
		{
			const auto Found = IndexByName.find(Output.PassName);
			if (Found == IndexByName.end())
			{
				Result.Issues.push_back(
				    MakeIssue(FRenderGraphIssue::ESeverity::Error, "Graph output '" + Output.ToString() + "' names a missing pass."));
				bOutputsValid = false;
				continue;
			}

			const FRenderGraphPassTypeDesc* Type = Contexts[Found->second].Type;
			if (Type != nullptr && Type->FindOutput(Output.ResourceName) == nullptr)
			{
				Result.Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error,
				                                  "Graph output '" + Output.ToString() + "' is not an output of " + Type->Name + "."));
				bOutputsValid = false;
			}
		}

		if (!bOutputsValid)
		{
			return Result;
		}

		std::unordered_map<std::string, SizeType> ResourceByField;
		const bool bResourcesOk = BuildResources(Graph, Contexts, Order, Result.Resources, ResourceByField, Result.Issues);
		const bool bInputsOk = CheckInputsSatisfied(Contexts, Order, ResourceByField, Result.Issues);
		if (!bResourcesOk || !bInputsOk)
		{
			return Result;
		}

		const std::unordered_set<SizeType> Contributing = FindContributingPasses(Graph, IndexByName);

		for (const SizeType PassIndex : Order)
		{
			const FPassContext& Context = Contexts[PassIndex];
			if (Contributing.find(PassIndex) == Contributing.end())
			{
				// Reported so the omission is visible: a pass silently not running looks like a pass that
				// ran and did nothing, and the two are debugged very differently.
				Result.Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Warning,
				                                  "Pass '" + Context.Instance->Name +
				                                      "' does not reach a graph output and will not be executed."));
				continue;
			}

			FCompiledPass Compiled;
			Compiled.PassName = Context.Instance->Name;
			Compiled.TypeName = Context.Instance->TypeName;

			// Bindings are listed outputs first, matching the order the resources were declared in, so a
			// reader of the compiled result sees a pass the same way the pass declared itself.
			for (const FRenderGraphResourceDesc& Output : Context.Type->Outputs)
			{
				const auto Found = ResourceByField.find(MakeFieldKey(Context.Instance->Name, Output.Name));
				if (Found != ResourceByField.end())
				{
					Compiled.Bindings.push_back(FCompiledPassBinding{ Output.Name, Found->second, ERenderGraphResourceVisibility::Output });
				}
			}

			for (const FRenderGraphResourceDesc& Input : Context.Type->Inputs)
			{
				const auto Found = ResourceByField.find(MakeFieldKey(Context.Instance->Name, Input.Name));
				if (Found != ResourceByField.end())
				{
					Compiled.Bindings.push_back(FCompiledPassBinding{ Input.Name, Found->second, ERenderGraphResourceVisibility::Input });
				}
			}

			Result.ExecutionOrder.push_back(std::move(Compiled));
		}

		bool bPresentableOutputs = true;
		for (const FRenderGraphResourceRef& Output : Graph.GetGraphOutputs())
		{
			const auto Found = ResourceByField.find(MakeFieldKey(Output.PassName, Output.ResourceName));
			if (Found == ResourceByField.end())
			{
				continue;
			}

			// A graph output is copied to the viewport, and that copy needs a colour format. A depth
			// resource cannot satisfy it: the formats do not match, so the copy is skipped and the viewport
			// stays black with nothing to say why.
			//
			// Rejected here rather than left to fail at presentation, because the two are indistinguishable
			// on screen — a graph that compiled and shows black looks exactly like a rendering bug. Depth is
			// also not viewable as it stands: a perspective depth buffer is non-linear, so most of its range
			// sits within a few values of 1.
			if (Result.Resources[Found->second].bIsDepth)
			{
				Result.Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error,
				                                  "Graph output '" + Output.ToString() +
				                                      "' is a depth resource, which cannot be displayed directly. Feed it to a "
				                                      "DebugVisualizer pass and mark that pass's colour output instead."));
				bPresentableOutputs = false;
				continue;
			}

			Result.OutputResourceIndices.push_back(Found->second);
		}

		if (!bPresentableOutputs)
		{
			// Cleared because the order was built before the outputs were checked, and reporting an order for
			// a graph that failed to compile invites the reader to believe those passes will run. Every other
			// failure returns before the order exists, so this is the one place it has to be undone.
			Result.ExecutionOrder.clear();
			Result.OutputResourceIndices.clear();
			return Result;
		}

		// A graph whose outputs are all produced by culled passes would leave nothing to execute, which is
		// not a runnable graph however well formed the description was.
		if (Result.ExecutionOrder.empty())
		{
			Result.Issues.push_back(MakeIssue(FRenderGraphIssue::ESeverity::Error, "No pass contributes to a graph output."));
			return Result;
		}

		Result.bSucceeded = true;
		return Result;
	}
} // namespace Lime
