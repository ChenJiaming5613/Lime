// Which panels are open, and how many viewports exist. The window geometry itself is ImGui's.
//
// Named after EditorLayout.ini and sitting beside it on purpose: the two are one layout described from
// two sides. ImGui records where a window is docked and how big it is; this records that the window
// should exist at all, and whether it was open.
//
// That split is why this file is needed rather than merely convenient. ImGui persists the geometry of
// every window it has ever seen, including ones the application no longer creates, so the saved entry
// for "Viewport3" is silently ignored unless something asks for a fourth viewport to be built first.
//
// Deliberately not part of FEditorSettings. That file is project configuration meant to be committed and
// every value in it is consumed once at startup; this changes on every exit, so folding it in would make
// a shared file churn on each run. Under Saved it is local state, like the ini.
//
// A missing or unreadable file is not an error: it means a first run, and the defaults are what the
// editor has always started with.

#pragma once

#include "Core/Json/JsonUtils.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace Lime
{
	class IEditorPanel;

	struct FEditorLayoutState
	{
		// How many viewport panels to recreate. At least one: the editor is unusable without a view of
		// the scene, and a file claiming zero is more likely corrupt than intentional.
		static constexpr uint32 MinViewportCount = 1;
		// A guard against a corrupt file asking for thousands of panels, each of which owns a render
		// target. High enough that no real layout reaches it.
		static constexpr uint32 MaxViewportCount = 16;

		uint32 ViewportCount = MinViewportCount;

		// Panel name to whether it was open. Only panels actually present are recorded, so a project
		// panel that disappears from a build does not leave a permanent entry behind.
		//
		// Keyed by name rather than by index because the panel list is assembled from registrations and
		// its order is not guaranteed between runs. The name is also what the ImGui layout keys on, so
		// the two files agree on identity.
		std::unordered_map<std::string, bool> PanelVisibility;

		// <exe>/Saved/EditorLayout.json, beside the ImGui layout it completes.
		static std::filesystem::path ResolvePath();

		// Applies the file on top of the defaults. Returns false when there was nothing usable to read,
		// which callers treat as a first run rather than as a failure.
		bool LoadFromFile(const std::filesystem::path& Path);
		bool SaveToFile(const std::filesystem::path& Path) const;

		// Whether a panel should start open. Falls back to the panel's own default when the file says
		// nothing about it, which is what makes a newly added panel appear without the file being edited.
		bool IsPanelVisible(const IEditorPanel& Panel) const;

		// Clamps into the supported range. Applied on load so the rest of the editor can trust the value.
		static uint32 ClampViewportCount(uint32 Value);

		// Used to decide whether anything is worth writing this frame, so the file is only touched when the
		// arrangement actually changed rather than at a fixed interval.
		bool operator==(const FEditorLayoutState& Other) const;
		bool operator!=(const FEditorLayoutState& Other) const { return !(*this == Other); }
	};
} // namespace Lime
