// Camera settings: field of view and the near/far clip planes.
//
// Edits the live perspective camera rather than a stored copy, so a change is visible the same frame. The
// values are not persisted: near and far are re-derived whenever the camera frames a model, and the field
// of view comes from ProjectSettings.json.

#pragma once

#include "Editor/Panels/EditorPanel.h"

namespace Lime
{
	class FCameraSettingsPanel final : public IEditorPanel
	{
	public:
		const char* GetName() const override { return "Camera Settings"; }
		EEditorDockSlot GetDefaultDockSlot() const override { return EEditorDockSlot::Right; }

		void OnDrawUI(const FEditorContext& Context) override;
	};
} // namespace Lime
