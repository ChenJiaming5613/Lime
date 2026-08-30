// Automation commands for render passes.
//
// Passes are addressed by the name they report, and their settings are reached through the same
// reflection the inspector uses. A project pass therefore becomes scriptable the moment it declares
// LIME_REFLECT, with no automation code of its own.

#include "Renderer/RenderPassRegistry.h"
#include "Renderer/Renderer.h"

#include "Automation/AutomationCommandRegistry.h"
#include "Automation/AutomationReflection.h"

#include <spdlog/fmt/fmt.h>

namespace Lime
{
	namespace
	{
		// Resolves the "pass" parameter. Fails the invocation and returns nullptr when unusable, so
		// callers can return straight away.
		IRenderPass* ResolvePass(FAutomationInvocation& Invocation)
		{
			FRenderer* Renderer = Invocation.GetContext().Renderer;
			if (Renderer == nullptr)
			{
				Invocation.Fail("No renderer");
				return nullptr;
			}

			std::string PassName;
			if (!Invocation.RequireString("pass", PassName))
			{
				return nullptr;
			}

			// Graph passes are addressed by their instance name in the graph, which is how the render
			// graph panel names them and how two passes of one type stay distinct.
			if (IRenderPass* GraphPass = Renderer->FindGraphPass(PassName))
			{
				return GraphPass;
			}

			for (const std::shared_ptr<IRenderPass>& Pass : Renderer->GetPasses())
			{
				if (PassName == Pass->GetName())
				{
					return Pass.get();
				}
			}

			Invocation.Fail(fmt::format("Unknown pass '{}'; call 'pass.list' for the available ones", PassName));
			return nullptr;
		}
	} // namespace

	void RegisterRenderPassAutomationCommands()
	{
		FAutomationCommandRegistry& Registry = FAutomationCommandRegistry::Get();

		Registry.Register("pass.list", "Lists render passes in execution order",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FRenderer* Renderer = Invocation.GetContext().Renderer;
			                  if (Renderer == nullptr)
			                  {
				                  Invocation.Fail("No renderer");
				                  return;
			                  }

			                  const std::vector<FRenderPassRegistration>& Registrations = FRenderPassRegistry::Get().GetRegistrations();

			                  // Emits one entry for a pass. The name differs between permanent passes (their
			                  // display name) and graph passes (their instance name in the graph), which is how
			                  // pass.get/pass.set address them.
			                  auto Emit = [&](FJson& Passes, const std::string& Name, IRenderPass& Pass)
			                  {
				                  FJson Entry = FJson::object();
				                  Entry["name"] = Name;
				                  Entry["priority"] = static_cast<int32>(Pass.GetPriority());
				                  // Tells a script whether pass.get and pass.set will work on it.
				                  Entry["hasSettings"] = Pass.GetReflectedSettings().IsValid();

				                  // Whether the engine provides it or the project does. Reported so a script can
				                  // address the project's own pass without keeping a list of engine pass names,
				                  // which would go stale as soon as one is added.
				                  //
				                  // Matched by type name, which is what the registry keys on; GetName is a display
				                  // name and the two need not agree.
				                  bool bIsBuiltin = false;
				                  for (const FRenderPassRegistration& Registration : Registrations)
				                  {
					                  if (Registration.Name != nullptr && Pass.GetTypeName() == Registration.Name)
					                  {
						                  bIsBuiltin = Registration.bIsBuiltin;
						                  break;
					                  }
				                  }
				                  Entry["isBuiltin"] = bIsBuiltin;

				                  Passes.push_back(std::move(Entry));
			                  };

			                  FJson Passes = FJson::array();
			                  for (const std::shared_ptr<IRenderPass>& Pass : Renderer->GetPasses())
			                  {
				                  Emit(Passes, Pass->GetName(), *Pass);
			                  }
			                  for (const auto& [Name, Pass] : Renderer->GetGraphPasses())
			                  {
				                  if (Pass != nullptr)
				                  {
					                  Emit(Passes, std::string(Name), *Pass);
				                  }
			                  }

			                  Invocation.GetResult()["passes"] = std::move(Passes);
		                  });

		Registry.Register("pass.describe", "Describes a pass's reflected fields with their metadata. Params: pass",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  IRenderPass* Pass = ResolvePass(Invocation);
			                  if (Pass == nullptr)
			                  {
				                  return;
			                  }

			                  const FReflectedRef Settings = Pass->GetReflectedSettings();
			                  if (!Settings.IsValid())
			                  {
				                  Invocation.Fail(fmt::format("Pass '{}' exposes no reflected settings", Pass->GetName()));
				                  return;
			                  }

			                  Invocation.GetResult()["pass"] = Pass->GetName();
			                  Invocation.GetResult()["fields"] = FAutomationReflection::DescribeFields(Settings);
		                  });

		Registry.Register("pass.get", "Reads a pass's current settings. Params: pass",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  IRenderPass* Pass = ResolvePass(Invocation);
			                  if (Pass == nullptr)
			                  {
				                  return;
			                  }

			                  const FReflectedRef Settings = Pass->GetReflectedSettings();
			                  if (!Settings.IsValid())
			                  {
				                  Invocation.Fail(fmt::format("Pass '{}' exposes no reflected settings", Pass->GetName()));
				                  return;
			                  }

			                  Invocation.GetResult()["pass"] = Pass->GetName();
			                  Invocation.GetResult()["values"] = FAutomationReflection::ReadValues(Settings);
		                  });

		Registry.Register("pass.set", "Writes pass settings. Params: pass, values (object of field name to value)",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  IRenderPass* Pass = ResolvePass(Invocation);
			                  if (Pass == nullptr)
			                  {
				                  return;
			                  }

			                  const FReflectedRef Settings = Pass->GetReflectedSettings();
			                  if (!Settings.IsValid())
			                  {
				                  Invocation.Fail(fmt::format("Pass '{}' exposes no reflected settings", Pass->GetName()));
				                  return;
			                  }

			                  const auto ValuesIterator = Invocation.GetParams().find("values");
			                  if (ValuesIterator == Invocation.GetParams().end())
			                  {
				                  Invocation.Fail("'values' is required");
				                  return;
			                  }

			                  std::vector<std::string> Applied;
			                  std::string Error;
			                  if (!FAutomationReflection::WriteFields(Settings, *ValuesIterator, Applied, Error))
			                  {
				                  // The applied names are reported even on failure, since earlier fields did change.
				                  Invocation.Fail(Applied.empty()
				                                      ? Error
													  : fmt::format("{} (applied {} field(s) before failing)", Error, Applied.size()));
				                  return;
			                  }

			                  Invocation.GetResult()["pass"] = Pass->GetName();
			                  Invocation.GetResult()["applied"] = Applied;
			                  // Echoed back so a caller can verify what the engine actually stored, which matters for
			                  // clamped or quantized fields.
			                  Invocation.GetResult()["values"] = FAutomationReflection::ReadValues(Settings);
		                  });
	}
} // namespace Lime
