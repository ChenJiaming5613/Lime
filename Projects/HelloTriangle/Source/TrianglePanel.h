// Project specific panel, showing how a project contributes custom UI.
//
// The generic Inspector already exposes FTriangleSettings through reflection, so this panel only
// adds what reflection cannot express: derived read-only values and an action button.

#pragma once

#include "Editor/Panels/EditorPanel.h"

namespace HelloTriangle
{
	class FTrianglePanel final : public Lime::IEditorPanel
	{
	public:
		const char* GetName() const override { return "Triangle"; }
		Lime::EEditorDockSlot GetDefaultDockSlot() const override { return Lime::EEditorDockSlot::Left; }

		void OnDrawUI(const Lime::FEditorContext& Context) override;
	};
} // namespace HelloTriangle
