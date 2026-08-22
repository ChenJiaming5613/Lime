// Engine startup configuration, including command line parsing.

#pragma once

#include "RHI/RHITypes.h"

#include <string>

namespace Lime
{
	struct FEngineConfig
	{
		std::string WindowTitle = "LimeEngine";
		uint32 WindowWidth = 1600;
		uint32 WindowHeight = 900;
		ERHIBackend Backend = GetDefaultBackend();
		uint32 BackBufferCount = 3;
		bool bVSync = true;
		bool bEnableEditor = LIME_WITH_EDITOR != 0;
		bool bEnableDebugRuntime = LIME_DEBUG != 0;
		bool bEnableNvrhiValidation = LIME_DEBUG != 0;

		// Supported switches: --rhi=<d3d12|vulkan>, --no-editor, --no-vsync, --width=N, --height=N,
		// --no-validation. Unknown arguments are logged and ignored.
		void ParseCommandLine(int ArgumentCount, const char* const* Arguments);
	};
} // namespace Lime
