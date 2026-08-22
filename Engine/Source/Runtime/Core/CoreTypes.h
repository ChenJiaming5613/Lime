// Fundamental type aliases and compiler intrinsics used across the engine.

#pragma once

#include <cstddef>
#include <cstdint>

namespace Lime
{
	using int8 = std::int8_t;
	using int16 = std::int16_t;
	using int32 = std::int32_t;
	using int64 = std::int64_t;

	using uint8 = std::uint8_t;
	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;
	using uint64 = std::uint64_t;

	using SizeType = std::size_t;
} // namespace Lime

#if defined(_MSC_VER)
#define LIME_FORCEINLINE __forceinline
#define LIME_NOINLINE __declspec(noinline)
#define LIME_DEBUG_BREAK() __debugbreak()
#else
#define LIME_FORCEINLINE inline __attribute__((always_inline))
#define LIME_NOINLINE __attribute__((noinline))
#define LIME_DEBUG_BREAK() __builtin_trap()
#endif

#define LIME_UNUSED(Expression) (void)(Expression)

// Disables copy and move for types that own platform or GPU resources.
#define LIME_NON_COPYABLE(TypeName)                                                                                                        \
	TypeName(const TypeName&) = delete;                                                                                                    \
	TypeName& operator=(const TypeName&) = delete

#define LIME_NON_MOVABLE(TypeName)                                                                                                         \
	TypeName(TypeName&&) = delete;                                                                                                         \
	TypeName& operator=(TypeName&&) = delete
