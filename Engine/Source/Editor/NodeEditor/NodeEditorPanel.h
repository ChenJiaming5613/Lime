// Hosts one of the node editor's upstream samples as an editor panel.
//
// The sample is compiled unmodified. It is a single translation unit that defines a struct deriving
// from an Application framework and expects that framework to drive it, so this panel supplies what is
// around it: a shim satisfies the base class, and the panel calls OnStart, OnFrame and OnStop at the
// points the editor already has for them.
//
// The basic-interaction sample is the one built, not blueprints: the latter needs a stack layout API
// that only exists in the fork of Dear ImGui the library vendors for its own examples. See the note in
// the .cpp.

#pragma once

#include "Editor/Panels/EditorPanel.h"

#include <memory>

namespace Lime
{
	// Draws the node editor sample. Hidden by default: it demonstrates the widget rather than being
	// something a project needs in the way on every launch.
	class FNodeEditorPanel final : public IEditorPanel
	{
	public:
		FNodeEditorPanel();
		~FNodeEditorPanel() override;

		const char* GetName() const override { return "Node Editor"; }
		// Center, because a node graph is a workspace: docked to a side it would be too narrow to use.
		EEditorDockSlot GetDefaultDockSlot() const override { return EEditorDockSlot::Center; }
		const char* GetMenuCategory() const override { return "Demos"; }
		bool IsVisibleByDefault() const override { return false; }

		void OnDrawUI(const FEditorContext& Context) override;

	private:
		// Deferred until the panel is first shown, so a project that never opens it does not create the
		// editor context or its settings file.
		void EnsureStarted();

		// Opaque so the sample's type does not leak into this header, which would drag the whole
		// vendored translation unit into everything that includes it.
		struct FImpl;
		std::unique_ptr<FImpl> Impl;
		bool bStarted = false;

		// Counts the first few frames after the panel opens, so the graph can be reframed once the
		// window has a real size. See the note in OnDrawUI.
		static constexpr int32 ReframeAfterFrames = 3;
		int32 FramesSinceStart = 0;
	};
} // namespace Lime
