// Automation commands for project script discovery.
//
// The engine only enumerates scripts; running one is the launcher's job, since the scripts are Python
// and the engine embeds no interpreter.

#include "Platform/PlatformPaths.h"

#include "Automation/AutomationCommandRegistry.h"
#include "Automation/AutomationScript.h"

namespace Lime
{
	void RegisterScriptAutomationCommands()
	{
		FAutomationCommandRegistry& Registry = FAutomationCommandRegistry::Get();

		Registry.Register("script.list", "Lists the automation scripts the running project ships",
		                  [](FAutomationInvocation& Invocation)
		                  {
			                  FJson Scripts = FJson::array();
			                  for (const FAutomationScript& Script : FAutomationScriptLibrary::Discover())
			                  {
				                  FJson Entry = FJson::object();
				                  Entry["name"] = Script.Name;
				                  Entry["path"] = FPlatformPaths::ToUtf8(Script.Path);
				                  Entry["description"] = Script.Description;
				                  Scripts.push_back(std::move(Entry));
			                  }

			                  const std::filesystem::path Directory = FAutomationScriptLibrary::GetScriptDirectory();
			                  Invocation.GetResult()["directory"] = Directory.empty() ? std::string() : FPlatformPaths::ToUtf8(Directory);
			                  Invocation.GetResult()["scripts"] = std::move(Scripts);
		                  });
	}
} // namespace Lime
