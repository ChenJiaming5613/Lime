#include "Core/LimeAssert.h"

#include "Core/Logging/LogManager.h"

#include <cstdio>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace Lime
{
	bool HandleAssertionFailure(const char* Expression, const char* File, int32 Line, const char* Message)
	{
		const char* SafeMessage = Message != nullptr ? Message : "";

		if (FLogManager::Get().IsInitialized())
		{
			LIME_LOG_CRITICAL(LIME_LOG_CATEGORY_CORE, "Assertion failed: {} at {}:{} {}", Expression, File, Line, SafeMessage);
			FLogManager::Get().Flush();
		}
		else
		{
			std::fprintf(stderr, "Assertion failed: %s at %s:%d %s\n", Expression, File, Line, SafeMessage);
			std::fflush(stderr);
		}

#if defined(_WIN32)
		return IsDebuggerPresent() != FALSE;
#else
		return true;
#endif
	}
} // namespace Lime
