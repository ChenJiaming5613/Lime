#include "Editor/EditorSelection.h"

#include "Scene/Scene.h"

namespace Lime
{
	void FEditorSelection::Validate(const FScene* Scene)
	{
		if (Selected == entt::null)
		{
			return;
		}

		// A stale handle can be reused by a later entity, so it must be dropped rather than left to point at
		// whatever now occupies that slot.
		if (Scene == nullptr || !Scene->GetRegistry().valid(Selected))
		{
			Selected = entt::null;
		}
	}
} // namespace Lime
