// Shows read only frame statistics plus whatever the application injects.

#pragma once

#include "Editor/Panels/EditorPanel.h"

#include <functional>

namespace Lime
{
	class FInspectorPanel final : public IEditorPanel
	{
	public:
		using FDrawDelegate = std::function<void()>;

		const char* GetName() const override { return "Inspector"; }
		void OnDrawUI(const FEditorContext& Context) override;

		// Lets the project draw its own controls without the editor knowing its types.
		void SetDrawDelegate(FDrawDelegate Delegate) { DrawDelegate = std::move(Delegate); }

	private:
		FDrawDelegate DrawDelegate;
	};
} // namespace Lime
