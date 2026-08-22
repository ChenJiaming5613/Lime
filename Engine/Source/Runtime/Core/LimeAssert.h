// Assertion macros. LIME_CHECK compiles out in non-debug builds, LIME_VERIFY never does.

#pragma once

#include "Core/CoreTypes.h"

namespace Lime
{
	// Reports a failed assertion through the log and returns true when the debugger should break.
	bool HandleAssertionFailure(const char* Expression, const char* File, int32 Line, const char* Message);
} // namespace Lime

#define LIME_ASSERT_IMPL(Expression, Message)                                                                                              \
	do                                                                                                                                     \
	{                                                                                                                                      \
		if (!(Expression)) [[unlikely]]                                                                                                    \
		{                                                                                                                                  \
			if (::Lime::HandleAssertionFailure(#Expression, __FILE__, __LINE__, (Message)))                                                \
			{                                                                                                                              \
				LIME_DEBUG_BREAK();                                                                                                        \
			}                                                                                                                              \
		}                                                                                                                                  \
	} while (false)

// Always evaluated. Use when the expression has side effects that must run in every configuration.
#define LIME_VERIFY(Expression) LIME_ASSERT_IMPL(Expression, nullptr)
#define LIME_VERIFY_MSG(Expression, Message) LIME_ASSERT_IMPL(Expression, Message)

#if LIME_DEBUG
#define LIME_CHECK(Expression) LIME_ASSERT_IMPL(Expression, nullptr)
#define LIME_CHECK_MSG(Expression, Message) LIME_ASSERT_IMPL(Expression, Message)
#else
#define LIME_CHECK(Expression) LIME_UNUSED(0)
#define LIME_CHECK_MSG(Expression, Message) LIME_UNUSED(0)
#endif

// Reports once and keeps running. Evaluates to the condition so it can drive control flow.
#define LIME_ENSURE(Expression) (::Lime::Private::EnsureImpl((Expression), #Expression, __FILE__, __LINE__))

namespace Lime::Private
{
	LIME_FORCEINLINE bool EnsureImpl(bool bCondition, const char* Expression, const char* File, int32 Line)
	{
		if (!bCondition) [[unlikely]]
		{
			HandleAssertionFailure(Expression, File, Line, nullptr);
		}
		return bCondition;
	}
} // namespace Lime::Private
