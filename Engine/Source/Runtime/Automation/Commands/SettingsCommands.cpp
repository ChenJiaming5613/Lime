// Automation commands for project settings.
//
// The settings themselves live in LimeRuntime, above this module, so they are reached through the
// delegates on FAutomationContext rather than by including the header.
//
// No setting can be applied to a running session: every one of them is consumed once during startup,
// when the window and device are created. These commands therefore separate two things that used to be
// conflated -- what the session is running with, and what will be written to disk.

#include "Automation/AutomationCommandRegistry.h"

namespace Lime
{
	void RegisterSettingsAutomationCommands()
	{
		FAutomationCommandRegistry& Registry = FAutomationCommandRegistry::Get();

		Registry.Register("settings.get", "Returns the settings this session is running with. Immutable until restart",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  if (Invocation.GetContext().QuerySettings == nullptr)
			                  {
				                  Invocation.Fail("Settings are unavailable");
				                  return;
			                  }

			                  Invocation.GetResult()["settings"] = Invocation.GetContext().QuerySettings();

			                  // Also reported so a client can tell whether an unsaved edit is pending
			                  // without a second round trip.
			                  if (Invocation.GetContext().QueryPendingSettings != nullptr)
			                  {
				                  const FJson Pending = Invocation.GetContext().QueryPendingSettings();
				                  Invocation.GetResult()["pending"] = Pending;
				                  Invocation.GetResult()["dirty"] = Pending != Invocation.GetResult()["settings"];
			                  }
		                  });

		Registry.Register(
		    "settings.set",
		    "Edits the pending settings file. Params: settings. Takes effect on the next launch, never on the running session",
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

			    // Returns the draft rather than the live values, since the draft is what
			    // this command changed. settings.save then commits it.
			    if (Invocation.GetContext().QueryPendingSettings != nullptr)
			    {
				    Invocation.GetResult()["pending"] = Invocation.GetContext().QueryPendingSettings();
			    }
			    Invocation.GetResult()["restartRequired"] = true;
		    });

		Registry.Register("settings.save", "Writes the pending settings to ProjectSettings.json in the project source tree",
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
