// Editor for ProjectSettings.json.
//
// The panel is a JSON editor and nothing more: it edits the configuration that will be used on the
// next launch and never touches the live runtime. Every value here is fixed once the window and device
// exist, so applying any of them mid-session would either be ignored or require tearing down and
// recreating the swap chain.
//
// Consequently the panel needs no access to the window or the renderer, and the running configuration
// stays immutable for the lifetime of the process.

#pragma once

#include "Editor/Panels/EditorPanel.h"
#include "Engine/ProjectSettings.h"

#include <array>

namespace Lime
{
	class FProjectSettingsPanel final : public IEditorPanel
	{
	public:
		const char* GetName() const override { return "Project Settings"; }
		EEditorDockSlot GetDefaultDockSlot() const override { return EEditorDockSlot::Right; }
		// Opened from the Window menu when needed, rather than taking up dock space every session.
		bool IsVisibleByDefault() const override { return false; }

		void Initialize(const FProjectSettings& CurrentSettings);

		void OnDrawUI(const FEditorContext& Context) override;

		// True when the edited values differ from what was last saved.
		bool HasUnsavedChanges() const { return Edited != Saved; }

	private:
		void DrawWindowSection();
		void DrawRHISection();
		void DrawEditorSection();
		void DrawToolbar();

		// Values being edited, and the last known on-disk state used to detect changes. The running
		// configuration is not tracked: it cannot change, so Saved is also what the session started
		// with until the user edits something.
		FProjectSettings Edited;
		FProjectSettings Saved;
		// Fixed buffer: ImGui writes into it directly, which is not valid on a std::string past its size.
		std::array<char, 256> TitleBuffer{};
		bool bInitialized = false;
	};
} // namespace Lime
