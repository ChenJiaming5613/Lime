#include "Editor/EditorPanelRegistry.h"

#include "Core/Logging/LogManager.h"

namespace Lime
{
	const char* ToString(EEditorDockSlot Slot)
	{
		switch (Slot)
		{
			case EEditorDockSlot::Left:
				return "Left";
			case EEditorDockSlot::Right:
				return "Right";
			case EEditorDockSlot::Bottom:
				return "Bottom";
			case EEditorDockSlot::Center:
				return "Center";
		}
		return "Right";
	}

	FEditorPanelRegistry& FEditorPanelRegistry::Get()
	{
		// Function local static: safe regardless of translation unit initialization order.
		static FEditorPanelRegistry Instance;
		return Instance;
	}

	void FEditorPanelRegistry::Register(const char* Name, FEditorPanelRegistration::FFactory Factory)
	{
		if (Factory == nullptr)
		{
			return;
		}
		Registrations.push_back(FEditorPanelRegistration{ Name, std::move(Factory) });
	}

	std::vector<std::shared_ptr<IEditorPanel>> FEditorPanelRegistry::InstantiateAll() const
	{
		std::vector<std::shared_ptr<IEditorPanel>> Panels;
		Panels.reserve(Registrations.size());

		std::string Summary;
		for (const FEditorPanelRegistration& Registration : Registrations)
		{
			std::shared_ptr<IEditorPanel> Panel = Registration.Factory();
			if (Panel == nullptr)
			{
				LIME_LOG_ERROR(LIME_LOG_CATEGORY_EDITOR, "Factory for panel '{}' returned null", Registration.Name);
				continue;
			}

			if (!Summary.empty())
			{
				Summary += ", ";
			}
			Summary += Panel->GetName();
			Summary += '(';
			Summary += ToString(Panel->GetDefaultDockSlot());
			Summary += ')';

			Panels.push_back(std::move(Panel));
		}

		// Logged so a missing panel points at registration rather than at the layout.
		if (Panels.empty())
		{
			LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "No project panels are registered");
		}
		else
		{
			LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "Registered project panel(s): {}", Summary);
		}

		return Panels;
	}
} // namespace Lime
