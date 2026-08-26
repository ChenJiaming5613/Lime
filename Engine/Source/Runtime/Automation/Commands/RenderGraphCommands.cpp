// Automation commands for the render graph inspector.
//
// Exists so the panel can be exercised from a script rather than by synthesising clicks: loading a file,
// reading back what was loaded and saving it again are the operations worth asserting, and none of them
// need the UI to be driven.
//
// Compiled only when the editor and the node editor panel are both present, since the panel is where the
// graph lives.

#include "Automation/AutomationCommandRegistry.h"

#if LIME_WITH_EDITOR && LIME_WITH_NODE_EDITOR

#include "Editor/EditorLayer.h"
#include "Editor/RenderGraph/RenderGraphPanel.h"

#include <spdlog/fmt/fmt.h>

#include <filesystem>

namespace Lime
{
	namespace
	{
		FRenderGraphPanel* ResolveRenderGraphPanel(FAutomationInvocation& Invocation)
		{
			FEditorLayer* Editor = Invocation.GetContext().Editor;
			if (Editor == nullptr)
			{
				Invocation.Fail("The editor is not active in this session");
				return nullptr;
			}

			FRenderGraphPanel* Panel = Editor->GetRenderGraphPanel();
			if (Panel == nullptr)
			{
				Invocation.Fail("The render graph panel is not available in this build");
			}
			return Panel;
		}

		// A relative path is resolved against the default directory for the operation, so a script can say
		// "DefaultGraph.json" without knowing where the engine keeps its content.
		std::filesystem::path ResolvePath(const std::string& Text, const std::filesystem::path& DefaultDirectory)
		{
			const std::filesystem::path Path(Text);
			return Path.is_absolute() ? Path : DefaultDirectory / Path;
		}

		void WriteGraphSummary(FAutomationInvocation& Invocation, const FRenderGraphPanel& Panel)
		{
			const FRenderGraphDesc& Graph = Panel.GetGraph();
			FJson& Result = Invocation.GetResult();

			Result["name"] = Graph.GetName();
			Result["passCount"] = Graph.GetPasses().size();
			Result["edgeCount"] = Graph.GetEdges().size();

			FJson Passes = FJson::array();
			for (const FRenderGraphPassInstance& Pass : Graph.GetPasses())
			{
				FJson Entry;
				Entry["name"] = Pass.Name;
				Entry["type"] = Pass.TypeName;
				Passes.push_back(std::move(Entry));
			}
			Result["passes"] = std::move(Passes);

			FJson Edges = FJson::array();
			for (const FRenderGraphEdge& Edge : Graph.GetEdges())
			{
				FJson Entry;
				Entry["from"] = Edge.From.ToString();
				Entry["to"] = Edge.To.ToString();
				Edges.push_back(std::move(Entry));
			}
			Result["edges"] = std::move(Edges);

			FJson Outputs = FJson::array();
			for (const FRenderGraphResourceRef& Output : Graph.GetGraphOutputs())
			{
				Outputs.push_back(Output.ToString());
			}
			Result["graphOutputs"] = std::move(Outputs);

		// Reported so a script can check the arrangement, not just the contents. Nodes stacked on top of
		// each other and nodes never laid out produce the same summary otherwise.
		FJson Layout = FJson::array();
		for (const FRenderGraphNodePlacement& Placement : Panel.GetPlacements())
		{
			FJson Entry;
			Entry["pass"] = Placement.PassName;
			Entry["layer"] = Placement.Layer;
			Entry["x"] = Placement.X;
			Entry["y"] = Placement.Y;
			Layout.push_back(std::move(Entry));
		}
		Result["layout"] = std::move(Layout);

			// Reported separately by severity, because a warning means the graph is usable and an error means
			// it is not; a script waiting for a clean load needs to tell those apart.
			FJson Issues = FJson::array();
			SizeType ErrorCount = 0;
			for (const FRenderGraphIssue& Issue : Panel.GetIssues())
			{
				FJson Entry;
				Entry["severity"] = Issue.IsError() ? "error" : "warning";
				Entry["message"] = Issue.Message;
				Issues.push_back(std::move(Entry));
				if (Issue.IsError())
				{
					++ErrorCount;
				}
			}
			Result["issues"] = std::move(Issues);
			Result["errorCount"] = ErrorCount;
			// True when the graph could be built: no errors, and something is marked as an output.
			Result["valid"] = ErrorCount == 0 && !Graph.GetGraphOutputs().empty();
		}
	} // namespace

	void RegisterRenderGraphAutomationCommands()
	{
		FAutomationCommandRegistry& Registry = FAutomationCommandRegistry::Get();

		Registry.Register("rendergraph.info", "Reports the loaded render graph: passes, edges, graph outputs and validation issues",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  const FRenderGraphPanel* Panel = ResolveRenderGraphPanel(Invocation);
			                  if (Panel == nullptr)
			                  {
				                  return;
			                  }

			                  WriteGraphSummary(Invocation, *Panel);
		                  });

		Registry.Register("rendergraph.load", "Loads a render graph. Params: path (relative to the content render graph directory)",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FRenderGraphPanel* Panel = ResolveRenderGraphPanel(Invocation);
			                  if (Panel == nullptr)
			                  {
				                  return;
			                  }

			                  std::string PathText;
			                  if (!Invocation.RequireString("path", PathText))
			                  {
				                  return;
			                  }

			                  const std::filesystem::path Path = ResolvePath(PathText, FRenderGraphPanel::GetDefaultLoadDirectory());
			                  if (!Panel->LoadFromFile(Path))
			                  {
				                  Invocation.Fail(fmt::format("Could not load '{}'", Path.string()));
				                  return;
			                  }

			                  // The summary rather than a bare acknowledgement, so a script can assert on what
			                  // arrived in one round trip.
			                  Invocation.GetResult()["path"] = Path.string();
			                  WriteGraphSummary(Invocation, *Panel);
		                  });

		Registry.Register("rendergraph.save", "Saves the loaded render graph. Params: path (relative to the saved render graph directory)",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FRenderGraphPanel* Panel = ResolveRenderGraphPanel(Invocation);
			                  if (Panel == nullptr)
			                  {
				                  return;
			                  }

			                  std::string PathText;
			                  if (!Invocation.RequireString("path", PathText))
			                  {
				                  return;
			                  }

			                  const std::filesystem::path Path = ResolvePath(PathText, FRenderGraphPanel::GetDefaultSaveDirectory());
			                  if (!Panel->SaveToFile(Path))
			                  {
				                  Invocation.Fail(fmt::format("Could not save to '{}'", Path.string()));
				                  return;
			                  }

			                  Invocation.GetResult()["path"] = Path.string();
		                  });
	}
} // namespace Lime

#else

namespace Lime
{
	// The registration list calls this unconditionally, so it has to exist even when the panel does not.
	void RegisterRenderGraphAutomationCommands() {}
} // namespace Lime

#endif
