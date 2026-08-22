// Automation command registry.
//
// Commands self register with LIME_REGISTER_AUTOMATION_COMMAND, mirroring the render pass and editor
// panel registries. A project can therefore add its own commands without touching engine code.
//
// Unlike those registries this one lives in a static library, so the built-in commands are pulled in
// explicitly by FAutomationServer rather than relying on static initializers surviving the link.

#pragma once

#include "Automation/AutomationTypes.h"

#include <map>
#include <string>
#include <vector>

namespace Lime
{
	struct FAutomationCommand
	{
		std::string Name;
		// One line summary returned by the "help" command.
		std::string Description;
		FAutomationHandler Handler;
		// Hidden commands back an HTTP route rather than being called by name, so they are left out of
		// the catalogue to keep it an accurate list of what a client should call.
		bool bHidden = false;
	};

	class FAutomationCommandRegistry
	{
	public:
		static FAutomationCommandRegistry& Get();

		// A duplicate name replaces the previous entry, so a project can override a built-in command.
		void Register(std::string Name, std::string Description, FAutomationHandler Handler);
		void RegisterHidden(std::string Name, std::string Description, FAutomationHandler Handler);

		const FAutomationCommand* Find(const std::string& Name) const;
		// Sorted by name, which keeps the help output stable. Hidden commands are excluded.
		std::vector<const FAutomationCommand*> GetAll() const;

	private:
		FAutomationCommandRegistry() = default;

		void RegisterInternal(std::string Name, std::string Description, FAutomationHandler Handler, bool bHidden);

		std::map<std::string, FAutomationCommand> Commands;
	};

	// Registers everything under Commands/. Called by the server; safe to call more than once.
	void RegisterBuiltinAutomationCommands();
} // namespace Lime

// Registers one command. Place at file scope in a .cpp.
#define LIME_REGISTER_AUTOMATION_COMMAND(CommandName, Description, HandlerLambda)                                                          \
	namespace                                                                                                                              \
	{                                                                                                                                      \
		const bool LIME_CONCAT(bLimeRegisteredCommand_, __LINE__) = []                                                                     \
		{                                                                                                                                  \
			::Lime::FAutomationCommandRegistry::Get().Register(CommandName, Description, HandlerLambda);                                   \
			return true;                                                                                                                   \
		}();                                                                                                                               \
	}
