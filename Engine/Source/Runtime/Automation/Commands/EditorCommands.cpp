// Automation commands for editor panels.
//
// Compiled only when the editor is present. The panel list is read from the editor layer, so project
// panels are scriptable without registering anything extra.

#include "Automation/AutomationCommandRegistry.h"

#if LIME_WITH_EDITOR

#include "Editor/EditorLayer.h"
#include "Editor/Panels/EditorPanel.h"

#include <spdlog/fmt/fmt.h>

namespace Lime
{
	namespace
	{
		FEditorLayer* ResolveEditor(FAutomationInvocation& Invocation)
		{
			FEditorLayer* Editor = Invocation.GetContext().Editor;
			if (Editor == nullptr)
			{
				Invocation.Fail("The editor is not active in this session");
			}
			return Editor;
		}

		IEditorPanel* ResolvePanel(FAutomationInvocation& Invocation, FEditorLayer& Editor)
		{
			std::string PanelName;
			if (!Invocation.RequireString("panel", PanelName))
			{
				return nullptr;
			}

			IEditorPanel* Panel = Editor.FindPanel(PanelName.c_str());
			if (Panel == nullptr)
			{
				Invocation.Fail(fmt::format("Unknown panel '{}'; call 'panel.list' for the available ones", PanelName));
			}
			return Panel;
		}
	} // namespace

	void RegisterEditorAutomationCommands()
	{
		FAutomationCommandRegistry& Registry = FAutomationCommandRegistry::Get();

		Registry.Register("panel.list", "Lists editor panels with their visibility and dock slot",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FEditorLayer* Editor = ResolveEditor(Invocation);
			                  if (Editor == nullptr)
			                  {
				                  return;
			                  }

			                  FJson Panels = FJson::array();
			                  for (const std::shared_ptr<IEditorPanel>& Panel : Editor->GetPanels())
			                  {
				                  FJson Entry = FJson::object();
				                  Entry["name"] = Panel->GetName();
				                  Entry["visible"] = Panel->IsVisible();
				                  Entry["dockSlot"] = ToString(Panel->GetDefaultDockSlot());
				                  Entry["category"] = Panel->GetMenuCategory();
				                  Panels.push_back(std::move(Entry));
			                  }

			                  Invocation.GetResult()["panels"] = std::move(Panels);
		                  });

		Registry.Register("panel.show", "Shows or hides a panel. Params: panel, visible (default true)",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FEditorLayer* Editor = ResolveEditor(Invocation);
			                  if (Editor == nullptr)
			                  {
				                  return;
			                  }

			                  IEditorPanel* Panel = ResolvePanel(Invocation, *Editor);
			                  if (Panel == nullptr)
			                  {
				                  return;
			                  }

			                  bool bVisible = true;
			                  std::string Error;
			                  if (!Invocation.TryGetBool("visible", bVisible, Error))
			                  {
				                  Invocation.Fail(std::move(Error));
				                  return;
			                  }

			                  Panel->SetVisible(bVisible);
			                  Invocation.GetResult()["panel"] = Panel->GetName();
			                  Invocation.GetResult()["visible"] = Panel->IsVisible();
		                  });

		Registry.Register("panel.addViewport", "Adds a viewport panel, as the Window menu does",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FEditorLayer* Editor = ResolveEditor(Invocation);
			                  if (Editor == nullptr)
			                  {
				                  return;
			                  }

			                  // A render target per viewport, so the ceiling is the saved layout's rather
			                  // than unbounded: a script in a loop would otherwise exhaust video memory.
			                  if (Editor->GetViewportCount() >= FEditorLayoutState::MaxViewportCount)
			                  {
				                  Invocation.Fail(fmt::format("Already at the {} viewport limit",
				                       FEditorLayoutState::MaxViewportCount));
				                  return;
			                  }

			                  Invocation.GetResult()["panel"] = Editor->AddViewport();
			                  Invocation.GetResult()["viewportCount"] = Editor->GetViewportCount();
		                  });

		Registry.Register("panel.resetLayout", "Discards the saved dock layout and rebuilds the default one",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FEditorLayer* Editor = ResolveEditor(Invocation);
			                  if (Editor == nullptr)
			                  {
				                  return;
			                  }

			                  Editor->RequestLayoutReset();
			                  Invocation.GetResult()["reset"] = true;
		                  });
	}
} // namespace Lime

#else

namespace Lime
{
	// Keeps the registration site in FAutomationServer free of preprocessor branches.
	void RegisterEditorAutomationCommands() {}
} // namespace Lime

#endif
