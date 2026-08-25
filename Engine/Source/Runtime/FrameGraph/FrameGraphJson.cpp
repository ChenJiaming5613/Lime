#include "FrameGraph/FrameGraphJson.h"

#include "Core/Json/JsonUtils.h"
#include "Core/Logging/LogManager.h"

#include <algorithm>
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

		// Splits "Pass.resource" into its parts. A name without a dot is a pass on its own, which is what
		// an execution edge endpoint looks like.
		//
		// Splits at the first dot rather than the last: a pass name cannot contain one, so anything after
		// the first belongs to the resource, and a resource name with a dot in it stays intact.
		FFrameGraphResourceRef ParseRef(const std::string& Text)
		{
			FFrameGraphResourceRef Ref;
			const SizeType Dot = Text.find('.');
			if (Dot == std::string::npos)
			{
				Ref.PassName = Text;
				return Ref;
			}

			Ref.PassName = Text.substr(0, Dot);
			Ref.ResourceName = Text.substr(Dot + 1);
			return Ref;
		}

		const char* ToString(EFrameEdgeKind Kind)
		{
			return Kind == EFrameEdgeKind::Execution ? "execution" : "data";
		}

		// Reads a string field, reporting rather than throwing on the wrong type. Returns false when the
		// field is absent or not a string, which lets the caller decide whether that element is droppable.
		bool ReadString(const FJson& Object, const char* Key, std::string& OutValue)
		{
			const auto Found = Object.find(Key);
			if (Found == Object.end() || !Found->is_string())
			{
				return false;
			}
			OutValue = Found->get<std::string>();
			return true;
		}

		// One place that decides what a saved graph looks like. Both the file and the string form go through
		// it, so the two cannot describe the same graph differently.
		//
		// Deliberately writes no layout: positions are recomputed on load, and a file that carried them
		// would disagree with the computed placement as soon as either changed.
		FJson BuildDocument(const FFrameGraphDesc& Graph)
		{
			FJson Document;
			Document["name"] = Graph.GetName();

			FJson Passes = FJson::array();
			for (const FFramePassInstance& Pass : Graph.GetPasses())
			{
				FJson Entry;
				Entry["name"] = Pass.Name;
				Entry["type"] = Pass.TypeName;
				Passes.push_back(std::move(Entry));
			}
			Document["passes"] = std::move(Passes);

			FJson Edges = FJson::array();
			for (const FFrameGraphEdge& Edge : Graph.GetEdges())
			{
				FJson Entry;
				Entry["from"] = Edge.From.ToString();
				Entry["to"] = Edge.To.ToString();
				// Written only for execution edges. A data edge is recognisable from its endpoints naming
				// resources, so spelling it out on every edge would be redundant.
				if (Edge.Kind == EFrameEdgeKind::Execution)
				{
					Entry["kind"] = ToString(Edge.Kind);
				}
				Edges.push_back(std::move(Entry));
			}
			Document["edges"] = std::move(Edges);

			FJson Outputs = FJson::array();
			for (const FFrameGraphResourceRef& Output : Graph.GetGraphOutputs())
			{
				Outputs.push_back(Output.ToString());
			}
			Document["graphOutputs"] = std::move(Outputs);

			return Document;
		}
	} // namespace

	SizeType FFrameGraphLoadResult::CountErrors() const
	{
		return static_cast<SizeType>(
		    std::count_if(Issues.begin(), Issues.end(), [](const FFrameGraphIssue& Issue) { return Issue.IsError(); }));
	}

	FFrameGraphLoadResult FFrameGraphJson::LoadFromFile(const std::filesystem::path& Path, const FFramePassTypeRegistry& Types,
	                                                    FFrameGraphDesc& OutGraph)
	{
		FJson Document;
		if (!FJsonUtils::LoadFromFile(Path, Document))
		{
			FFrameGraphLoadResult Result;
			Result.Issues.push_back(MakeIssue(FFrameGraphIssue::ESeverity::Error, "Could not read '" + Path.string() + "'."));
			return Result;
		}

		return LoadFromString(Document.dump(), Types, OutGraph);
	}

	FFrameGraphLoadResult FFrameGraphJson::LoadFromString(const std::string& Json, const FFramePassTypeRegistry& Types,
	                                                      FFrameGraphDesc& OutGraph)
	{
		FFrameGraphLoadResult Result;

		FJson Document = FJson::parse(Json, nullptr, false, true);
		if (Document.is_discarded() || !Document.is_object())
		{
			Result.Issues.push_back(MakeIssue(FFrameGraphIssue::ESeverity::Error, "The document is not a JSON object."));
			return Result;
		}

		// Cleared only once the document parses, so a failed load leaves whatever was already open rather
		// than replacing a working graph with an empty one.
		OutGraph.Clear();

		std::string GraphName;
		if (ReadString(Document, "name", GraphName))
		{
			OutGraph.SetName(std::move(GraphName));
		}

		const auto PassArray = Document.find("passes");
		if (PassArray == Document.end() || !PassArray->is_array())
		{
			Result.Issues.push_back(MakeIssue(FFrameGraphIssue::ESeverity::Error, "Missing a 'passes' array."));
			return Result;
		}

		for (const FJson& Entry : *PassArray)
		{
			if (!Entry.is_object())
			{
				Result.Issues.push_back(MakeIssue(FFrameGraphIssue::ESeverity::Warning, "Skipped a pass entry that is not an object."));
				continue;
			}

			std::string PassName;
			std::string TypeName;
			if (!ReadString(Entry, "name", PassName) || !ReadString(Entry, "type", TypeName))
			{
				Result.Issues.push_back(
				    MakeIssue(FFrameGraphIssue::ESeverity::Warning, "Skipped a pass without both a 'name' and a 'type'."));
				continue;
			}

			// Routed through AddPass so the file cannot introduce a state the editor would refuse to create:
			// duplicate names and unknown types are rejected identically either way.
			FFrameGraphIssue Issue;
			if (!OutGraph.AddPass(PassName, TypeName, Types, Issue))
			{
				Issue.Severity = FFrameGraphIssue::ESeverity::Warning;
				Result.Issues.push_back(std::move(Issue));
			}
		}

		const auto EdgeArray = Document.find("edges");
		if (EdgeArray != Document.end() && EdgeArray->is_array())
		{
			for (const FJson& Entry : *EdgeArray)
			{
				if (!Entry.is_object())
				{
					Result.Issues.push_back(
					    MakeIssue(FFrameGraphIssue::ESeverity::Warning, "Skipped an edge entry that is not an object."));
					continue;
				}

				std::string FromText;
				std::string ToText;
				if (!ReadString(Entry, "from", FromText) || !ReadString(Entry, "to", ToText))
				{
					Result.Issues.push_back(
					    MakeIssue(FFrameGraphIssue::ESeverity::Warning, "Skipped an edge without both a 'from' and a 'to'."));
					continue;
				}

				FFrameGraphEdge Edge;
				Edge.From = ParseRef(FromText);
				Edge.To = ParseRef(ToText);

				std::string KindText;
				if (ReadString(Entry, "kind", KindText) && KindText == "execution")
				{
					Edge.Kind = EFrameEdgeKind::Execution;
					// An execution edge orders passes, so any resource part is meaningless and dropped rather
					// than silently kept where it would confuse a later comparison.
					Edge.From.ResourceName.clear();
					Edge.To.ResourceName.clear();
				}
				else
				{
					// Inferred rather than required: an edge naming resources on both ends is a data edge, and
					// making every file spell that out would be noise.
					Edge.Kind = Edge.From.IsPassOnly() && Edge.To.IsPassOnly() ? EFrameEdgeKind::Execution : EFrameEdgeKind::Data;
				}

				FFrameGraphIssue Issue;
				if (!OutGraph.AddEdge(Edge, Types, Issue))
				{
					Issue.Severity = FFrameGraphIssue::ESeverity::Warning;
					Result.Issues.push_back(std::move(Issue));
				}
			}
		}

		const auto OutputArray = Document.find("graphOutputs");
		if (OutputArray != Document.end() && OutputArray->is_array())
		{
			for (const FJson& Entry : *OutputArray)
			{
				if (!Entry.is_string())
				{
					Result.Issues.push_back(
					    MakeIssue(FFrameGraphIssue::ESeverity::Warning, "Skipped a graph output that is not a string."));
					continue;
				}

				const FFrameGraphResourceRef Output = ParseRef(Entry.get<std::string>());
				FFrameGraphIssue Issue;
				if (!OutGraph.ToggleGraphOutput(Output, Types, Issue))
				{
					// ToggleGraphOutput returns false both for a rejection and for removing an existing mark.
					// Only the former carries a message, which is how the two are told apart here.
					if (!Issue.Message.empty())
					{
						Issue.Severity = FFrameGraphIssue::ESeverity::Warning;
						Result.Issues.push_back(std::move(Issue));
					}
				}
			}
		}

		FJsonUtils::WarnUnknownKeys(Document, { "name", "passes", "edges", "graphOutputs" }, {}, "frame graph");

		Result.bSucceeded = true;
		return Result;
	}

	bool FFrameGraphJson::SaveToFile(const std::filesystem::path& Path, const FFrameGraphDesc& Graph)
	{
		if (!FJsonUtils::SaveToFile(Path, BuildDocument(Graph)))
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_RENDERER, "Could not write the frame graph to '{}'", Path.string());
			return false;
		}

		return true;
	}

	std::string FFrameGraphJson::SaveToString(const FFrameGraphDesc& Graph)
	{
		return BuildDocument(Graph).dump(1, '\t');
	}
} // namespace Lime
