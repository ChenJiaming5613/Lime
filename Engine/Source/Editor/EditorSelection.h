// Which scene entity the editor has selected.
//
// Held by FEditorLayer and handed to panels by pointer rather than stored in FEditorContext by value:
// the context is rebuilt every frame, so a selection living in it would be lost as soon as it was made.
// Sharing one object also means the hierarchy panel and the inspector cannot disagree about it.

#pragma once

#include "Core/CoreTypes.h"

#include <entt/entity/entity.hpp>

namespace Lime
{
	class FScene;

	class FEditorSelection
	{
	public:
		entt::entity Get() const { return Selected; }
		void Set(entt::entity Entity) { Selected = Entity; }
		void Clear() { Selected = entt::null; }
		bool Has() const { return Selected != entt::null; }
		bool Is(entt::entity Entity) const { return Selected == Entity; }

		// Drops a selection that no longer exists, which happens when a scene is replaced. Called once per
		// frame so a panel never has to validate the handle itself.
		void Validate(const FScene* Scene);

	private:
		entt::entity Selected = entt::null;
	};
} // namespace Lime
