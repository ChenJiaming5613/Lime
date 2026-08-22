// Reflection driven JSON serialization. Any type with LIME_REFLECT can be persisted without
// writing per-type code.
//
// Supported field types: bool, int32, uint32, float, FVector2/3/4 (as arrays), std::string.
// Fields marked Transient are skipped.

#pragma once

#include "Core/Json/JsonUtils.h"
#include "Core/Reflection/Reflection.h"

namespace Lime
{
	class FJsonArchive
	{
	public:
		// Writes every non-transient reflected field of Instance into OutJson.
		template<typename Type>
		static void Save(Type& Instance, FJson& OutJson)
		{
			static_assert(bHasReflection<Type>, "Save requires a LIME_REFLECT declaration");
			SaveFields(Reflect<Type>(), &Instance, OutJson);
		}

		// Applies matching fields from InJson onto Instance, leaving absent fields untouched.
		template<typename Type>
		static void Load(const FJson& InJson, Type& Instance)
		{
			static_assert(bHasReflection<Type>, "Load requires a LIME_REFLECT declaration");
			LoadFields(Reflect<Type>(), &Instance, InJson);
		}

		// Single value conversion, exposed so callers that walk fields themselves reuse the same type
		// support instead of duplicating it. Return false when the runtime type is not supported.
		static bool SaveValue(entt::meta_any& Value, FJson& OutJson);
		static bool LoadValue(entt::meta_any& Value, const FJson& InJson);

		// Name of the supported type behind a value, for diagnostics. "unsupported" when unknown.
		static const char* GetValueTypeName(const entt::meta_any& Value);

	private:
		// Instance is passed as an opaque pointer and rebound through meta_type::from_void, which is
		// the only way to obtain a writable meta_handle without knowing the static type here.
		static void SaveFields(const entt::meta_type& MetaType, void* Instance, FJson& OutJson);
		static void LoadFields(const entt::meta_type& MetaType, void* Instance, const FJson& InJson);
	};
} // namespace Lime
