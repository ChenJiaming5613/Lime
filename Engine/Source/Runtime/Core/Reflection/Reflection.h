// Thin wrapper over the EnTT meta system.
//
// Registration is lazy: Reflect<T>() runs it once on first use, so declarations can live in
// headers without static initialization order concerns and without duplicate registration.
//
// EnTT API notes that this layer depends on (verified against v3.16.0):
//   - entt::meta_factory<T>{} is the entry point; .data(const char*) also stores the field name.
//   - Fields must be registered with entt::as_ref_t. The default as-value policy returns a copy,
//     so try_cast would hand out a pointer to a temporary instead of the real field.
//   - meta_data::get() takes an lvalue (it binds a meta_handle), so instances are passed directly.
//   - custom<T>() overwrites rather than accumulates, so the metadata is built in one shot.

#pragma once

#include "Core/Reflection/PropertyMeta.h"

#include <entt/meta/factory.hpp>
#include <entt/meta/resolve.hpp>

namespace Lime
{
	// Specialized by LIME_REFLECT. The default reports "no reflection" so generic code can branch.
	template<typename Type>
	struct TReflectionRegistrar
	{
		static constexpr bool bHasReflection = false;
		static void Apply() {}
	};

	template<typename Type>
	constexpr bool bHasReflection = TReflectionRegistrar<Type>::bHasReflection;

	// Registers Type on first call and returns its meta type.
	template<typename Type>
	entt::meta_type Reflect()
	{
		static const bool bRegistered = []
		{
			TReflectionRegistrar<Type>::Apply();
			return true;
		}();
		LIME_UNUSED(bRegistered);
		return entt::resolve<Type>();
	}

	// Alias used at registration sites to keep them readable.
	using FProp = FPropertyMetaBuilder;

	// Returns the metadata attached to a field, or nullptr when none was provided.
	inline const FPropertyMeta* GetPropertyMeta(const entt::meta_data& Data)
	{
		return static_cast<const FPropertyMeta*>(Data.custom());
	}
} // namespace Lime

// Declares reflection for a type. Must appear at global scope; the type may be qualified.
//
//   LIME_REFLECT(Lime::FTriangleSettings)
//   {
//       LIME_PROPERTY(bPaused, Lime::FProp("Pause rotation"));
//       LIME_PROPERTY(Speed,   Lime::FProp("Speed").Range(-6.0f, 6.0f));
//   }
#define LIME_REFLECT(TypeName)                                                                                                             \
	template<>                                                                                                                             \
	struct Lime::TReflectionRegistrar<TypeName>                                                                                            \
	{                                                                                                                                      \
		using FReflectedType = TypeName;                                                                                                   \
		static constexpr bool bHasReflection = true;                                                                                       \
		static void Apply();                                                                                                               \
	};                                                                                                                                     \
	inline void Lime::TReflectionRegistrar<TypeName>::Apply()

// Registers one field with its metadata. Only valid inside a LIME_REFLECT body.
#define LIME_PROPERTY(FieldName, MetaExpression)                                                                                           \
	entt::meta_factory<FReflectedType>{}                                                                                                   \
	    .data<&FReflectedType::FieldName, entt::as_ref_t>(#FieldName)                                                                      \
	    .custom<Lime::FPropertyMeta>(static_cast<Lime::FPropertyMeta>(MetaExpression))

// Optional: gives the type a readable name in diagnostics and in the inspector header.
#define LIME_REFLECT_TYPE_NAME(DisplayName) entt::meta_factory<FReflectedType>{}.type(DisplayName)
