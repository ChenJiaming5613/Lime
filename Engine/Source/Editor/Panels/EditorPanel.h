// Editor panel interface.
//
// Panels register themselves with LIME_REGISTER_EDITOR_PANEL, so a project only writes the class.
// GetDefaultDockSlot lets the editor build a sensible default layout without knowing the panel.

#pragma once

#include "Editor/EditorContext.h"

namespace Lime
{
	enum class EEditorDockSlot : uint8
	{
		Left = 0,
		Right,
		Bottom,
		// Docked over the scene; usually not what a panel wants.
		Center
	};

	class IEditorPanel
	{
	public:
		virtual ~IEditorPanel() = default;

		// Doubles as the ImGui window title, so it must be stable across runs for layout persistence.
		virtual const char* GetName() const = 0;
		virtual EEditorDockSlot GetDefaultDockSlot() const { return EEditorDockSlot::Right; }
		// Menu category used to group entries under Window.
		virtual const char* GetMenuCategory() const { return "Panels"; }
		// Panels that are only occasionally useful can start hidden and be opened from the menu.
		virtual bool IsVisibleByDefault() const { return true; }

		virtual void OnDrawUI(const FEditorContext& Context) = 0;

		bool IsVisible() const { return bVisible; }
		void SetVisible(bool bInVisible) { bVisible = bInVisible; }
		bool* GetVisiblePtr() { return &bVisible; }

	protected:
		bool bVisible = true;
	};

	const char* ToString(EEditorDockSlot Slot);
} // namespace Lime
