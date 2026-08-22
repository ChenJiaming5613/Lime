// Editor panel registry. Mirrors FRenderPassRegistry; see its header for why static self
// registration is safe here (project sources are compiled straight into the executable).

#pragma once

#include "Editor/Panels/EditorPanel.h"

#include <functional>
#include <memory>
#include <vector>

namespace Lime
{
	struct FEditorPanelRegistration
	{
		using FFactory = std::function<std::shared_ptr<IEditorPanel>()>;

		const char* Name = nullptr;
		FFactory Factory;
	};

	class FEditorPanelRegistry
	{
	public:
		static FEditorPanelRegistry& Get();

		void Register(const char* Name, FEditorPanelRegistration::FFactory Factory);

		// Instantiates every registered panel. Engine panels are added by FEditorLayer directly, so
		// this only covers what projects contributed.
		std::vector<std::shared_ptr<IEditorPanel>> InstantiateAll() const;

		const std::vector<FEditorPanelRegistration>& GetRegistrations() const { return Registrations; }

	private:
		FEditorPanelRegistry() = default;

		std::vector<FEditorPanelRegistration> Registrations;
	};
} // namespace Lime

// Registers a panel type. Place at file scope in the panel's .cpp; the type must be default
// constructible and may be namespace qualified.
#define LIME_REGISTER_EDITOR_PANEL(PanelType)                                                                                              \
	namespace                                                                                                                              \
	{                                                                                                                                      \
		const bool LIME_CONCAT(bLimeRegisteredPanel_, __LINE__) = []                                                                       \
		{                                                                                                                                  \
			::Lime::FEditorPanelRegistry::Get().Register(                                                                                  \
			    #PanelType, [] { return std::static_pointer_cast<::Lime::IEditorPanel>(std::make_shared<PanelType>()); });                 \
			return true;                                                                                                                   \
		}();                                                                                                                               \
	}
