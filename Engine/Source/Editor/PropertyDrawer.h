// Generates ImGui controls from reflected fields, so a settings struct only has to be declared once.

#pragma once

#include "Renderer/RenderTypes.h"

namespace Lime
{
	class FPropertyDrawer
	{
	public:
		// Draws a control per reflected field. Returns true when any value changed this frame.
		template<typename Type>
		static bool Draw(Type& Instance)
		{
			static_assert(bHasReflection<Type>, "Draw requires a LIME_REFLECT declaration");
			return DrawFields(Reflect<Type>(), &Instance);
		}

		// Type erased entry point, used for a pass exposing its settings.
		static bool Draw(const FReflectedRef& Settings);

		static bool DrawFields(const entt::meta_type& MetaType, void* Instance);

	private:
		static bool DrawField(const entt::meta_data& Data, entt::meta_any& Owner);
		static bool DrawValue(const char* Label, const FPropertyMeta* Meta, entt::meta_any& Value);
	};
} // namespace Lime
