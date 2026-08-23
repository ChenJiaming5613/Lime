// Editor for EditorSettings.json.
//
// Mirrors FProjectSettingsPanel: it edits the appearance that will be used on the next launch and
// never touches the live style. Applying a change immediately is technically possible for colours,
// but not for the font size, which is baked into the atlas at startup. Treating all of them the same
// way keeps the rule simple and the running appearance consistent with the file it was loaded from.

#pragma once

#include "Editor/EditorSettings.h"
#include "Editor/Panels/EditorPanel.h"

namespace Lime
{
	class FEditorSettingsPanel final : public IEditorPanel
	{
	public:
		const char* GetName() const override { return "Editor Settings"; }
		EEditorDockSlot GetDefaultDockSlot() const override { return EEditorDockSlot::Right; }
		// Opened from the Window menu when needed, rather than taking up dock space every session.
		bool IsVisibleByDefault() const override { return false; }

		void Initialize(const FEditorSettings& CurrentSettings);

		void OnDrawUI(const FEditorContext& Context) override;

		// True when the edited values differ from what was last saved.
		bool HasUnsavedChanges() const { return Edited != Saved; }

	private:
		void DrawAppearanceSection();
		void DrawToolbar();

		// Values being edited, and the last known on-disk state used to detect changes. The running
		// appearance is not tracked separately: it cannot change, so Saved is also what the session
		// started with until the user edits something.
		FEditorSettings Edited;
		FEditorSettings Saved;
		bool bInitialized = false;
	};
} // namespace Lime
