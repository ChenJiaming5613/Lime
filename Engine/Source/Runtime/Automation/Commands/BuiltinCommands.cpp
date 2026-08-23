// Pulls in the built-in command groups.
//
// Called explicitly rather than relying on static initializers: LimeAutomation is a static library,
// so the linker may discard an object file whose only content is a registration.

#include "Automation/AutomationCommandRegistry.h"

namespace Lime
{
	void RegisterCoreAutomationCommands();
	void RegisterRenderPassAutomationCommands();
	void RegisterEditorAutomationCommands();
	void RegisterSettingsAutomationCommands();
	void RegisterScriptAutomationCommands();
	void RegisterUITestAutomationCommands();

	void RegisterBuiltinAutomationCommands()
	{
		// Registration replaces by name, so running twice is harmless.
		static const bool bRegistered = []
		{
			RegisterCoreAutomationCommands();
			RegisterRenderPassAutomationCommands();
			RegisterEditorAutomationCommands();
			RegisterSettingsAutomationCommands();
			RegisterScriptAutomationCommands();
			RegisterUITestAutomationCommands();
			return true;
		}();
		LIME_UNUSED(bRegistered);
	}
} // namespace Lime
