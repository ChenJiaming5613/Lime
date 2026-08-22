// Lists the reflected settings of every registered render pass and generates controls for them.
// A project gets an inspector for free by declaring LIME_REFLECT on its settings struct; no panel
// code is required.

#pragma once

#include "Editor/Panels/EditorPanel.h"

namespace Lime
{
	class FInspectorPanel final : public IEditorPanel
	{
	public:
		const char* GetName() const override { return "Inspector"; }
		EEditorDockSlot GetDefaultDockSlot() const override { return EEditorDockSlot::Right; }

		void OnDrawUI(const FEditorContext& Context) override;
	};
} // namespace Lime
