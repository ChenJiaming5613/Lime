#include "Automation/AutomationCommandRegistry.h"

#include "Core/Logging/LogManager.h"

namespace Lime
{
	FAutomationCommandRegistry& FAutomationCommandRegistry::Get()
	{
		static FAutomationCommandRegistry Instance;
		return Instance;
	}

	void FAutomationCommandRegistry::Register(std::string Name, std::string Description, FAutomationHandler Handler)
	{
		if (Name.empty() || Handler == nullptr)
		{
			LIME_LOG_ERROR(LIME_LOG_CATEGORY_AUTOMATION, "Ignoring an automation command with no name or handler");
			return;
		}

		const bool bReplacing = Commands.find(Name) != Commands.end();
		if (bReplacing)
		{
			LIME_LOG_INFO(LIME_LOG_CATEGORY_AUTOMATION, "Automation command '{}' was replaced", Name);
		}

		Commands[Name] = FAutomationCommand{ Name, std::move(Description), std::move(Handler) };
	}

	const FAutomationCommand* FAutomationCommandRegistry::Find(const std::string& Name) const
	{
		const auto Iterator = Commands.find(Name);
		return Iterator != Commands.end() ? &Iterator->second : nullptr;
	}

	std::vector<const FAutomationCommand*> FAutomationCommandRegistry::GetAll() const
	{
		// std::map already orders by name, so no explicit sort is needed.
		std::vector<const FAutomationCommand*> Result;
		Result.reserve(Commands.size());
		for (const auto& [Name, Command] : Commands)
		{
			Result.push_back(&Command);
		}
		return Result;
	}
} // namespace Lime
