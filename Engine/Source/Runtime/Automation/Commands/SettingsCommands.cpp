// Automation commands for project settings.
//
// The settings themselves live in LimeRuntime, above this module, so they are reached through the
// delegates on FAutomationContext rather than by including the header.

#include "Automation/AutomationCommandRegistry.h"

namespace Lime
{
	void RegisterSettingsAutomationCommands()
	{
		FAutomationCommandRegistry& Registry = FAutomationCommandRegistry::Get();

		Registry.Register("settings.get", "Returns the current project settings as JSON",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  if (Invocation.GetContext().QuerySettings == nullptr)
			                  {
				                  Invocation.Fail("Settings are unavailable");
				                  return;
			                  }
			                  Invocation.GetResult()["settings"] = Invocation.GetContext().QuerySettings();
		                  });

		Registry.Register("settings.set", "Applies a partial settings object. Params: settings. Most keys only take effect after a restart",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  if (Invocation.GetContext().ApplySettings == nullptr)
			                  {
				                  Invocation.Fail("Settings are unavailable");
				                  return;
			                  }

			                  const auto SettingsIterator = Invocation.GetParams().find("settings");
			                  if (SettingsIterator == Invocation.GetParams().end() || !SettingsIterator->is_object())
			                  {
				                  Invocation.Fail("'settings' must be an object");
				                  return;
			                  }

			                  std::string Error;
			                  if (!Invocation.GetContext().ApplySettings(*SettingsIterator, Error))
			                  {
				                  Invocation.Fail(std::move(Error));
				                  return;
			                  }

			                  Invocation.GetResult()["settings"] = Invocation.GetContext().QuerySettings();
		                  });

		Registry.Register("settings.save", "Writes ProjectSettings.json in the project source tree",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  if (Invocation.GetContext().SaveSettings == nullptr)
			                  {
				                  Invocation.Fail("Saving is unavailable");
				                  return;
			                  }
			                  if (!Invocation.GetContext().SaveSettings())
			                  {
				                  Invocation.Fail("Could not write the settings file; see the log for details");
				                  return;
			                  }
			                  Invocation.GetResult()["saved"] = true;
		                  });
	}
} // namespace Lime
