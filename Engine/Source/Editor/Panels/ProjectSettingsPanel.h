// Editor for ProjectSettings.json.
//
// The panel edits the configuration that will be used on the next launch, not the live runtime state.
// Most of these values are fixed once the device and window exist: the backend, the buffer count and
// the validation mode are chosen at device creation, and turning the editor off would remove the very
// panel needed to turn it back on. Fields in that situation are marked "restart".
//
// The only exception is the window title, which is free to apply immediately.

#pragma once

#include "Editor/Panels/EditorPanel.h"
#include "Engine/ProjectSettings.h"

#include <array>
#include <string>

namespace Lime
{
	class FWindow;

	class FProjectSettingsPanel final : public IEditorPanel
	{
	public:
		const char* GetName() const override { return "Project Settings"; }
		EEditorDockSlot GetDefaultDockSlot() const override { return EEditorDockSlot::Right; }
		// Opened from the Window menu when needed, rather than taking up dock space every session.
		bool IsVisibleByDefault() const override { return false; }

		// Window is optional; when present the title is applied as soon as it is edited.
		void Initialize(const FProjectSettings& CurrentSettings, FWindow* InWindow);

		void OnDrawUI(const FEditorContext& Context) override;

		// True when the edited values differ from what was last saved.
		bool HasUnsavedChanges() const { return Edited != Saved; }

	private:
		void DrawWindowSection();
		void DrawRHISection();
		void DrawEditorSection();
		void DrawToolbar();
		// Appends the marker that tells the user a field only takes effect after a restart.
		static void DrawRestartMarker();

		// Values being edited, and the last known on-disk state used to detect changes.
		FProjectSettings Edited;
		FProjectSettings Saved;
		// Snapshot taken at startup, so the panel can report which values the session is really using.
		FProjectSettings Launched;
		FWindow* Window = nullptr;
		// Fixed buffer: ImGui writes into it directly, which is not valid on a std::string past its size.
		std::array<char, 256> TitleBuffer{};
		bool bInitialized = false;
	};
} // namespace Lime
