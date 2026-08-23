// Shows the scene as a collapsible tree.
//
// Docked left rather than right on purpose: panels sharing a slot become tabs, and only the selected tab
// can be hovered or clicked. Putting this beside the inspector would make one of them unreachable to UI
// automation without an extra tab activation step.

#pragma once

#include "Editor/Panels/EditorPanel.h"

#include <entt/entity/entity.hpp>
#include <string>

namespace Lime
{
	class FScene;

	class FSceneHierarchyPanel final : public IEditorPanel
	{
	public:
		const char* GetName() const override { return "Scene Hierarchy"; }
		EEditorDockSlot GetDefaultDockSlot() const override { return EEditorDockSlot::Left; }

		void OnDrawUI(const FEditorContext& Context) override;

	private:
		// Draws one entity and, when expanded, its children. Iterative rather than recursive would need an
		// explicit stack to mirror ImGui's push/pop pairing, so this recurses but is bounded by the tree
		// depth the user has actually expanded rather than by the whole scene.
		void DrawEntity(const FEditorContext& Context, FScene& Scene, entt::entity Entity);

		// True when the entity or any of its descendants matches the filter, so filtering keeps the parents
		// needed to reach a match instead of hiding it inside a collapsed branch.
		bool MatchesFilter(const FScene& Scene, entt::entity Entity) const;

		std::string Filter;
	};
} // namespace Lime
