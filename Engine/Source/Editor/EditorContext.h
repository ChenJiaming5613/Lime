// Read only frame state handed to editor panels so they never reach into engine internals.

#pragma once

#include "Core/CoreTypes.h"

#include <string>

namespace Lime
{
	class FLogRingBuffer;

	struct FEditorContext
	{
		float DeltaSeconds = 0.0f;
		float FramesPerSecond = 0.0f;
		uint32 ViewportWidth = 0;
		uint32 ViewportHeight = 0;
		const char* BackendName = "Unknown";
		std::string AdapterName;
		FLogRingBuffer* LogBuffer = nullptr;
	};
} // namespace Lime
