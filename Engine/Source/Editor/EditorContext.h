// Frame state handed to editor panels so they never reach into engine internals.

#pragma once

#include "Core/CoreTypes.h"
#include "Renderer/Renderer.h"

#include <string>

namespace Lime
{
	class FLogRingBuffer;
	class FEditorSelection;
	class FScene;

	struct FEditorContext
	{
		float DeltaSeconds = 0.0f;
		float FramesPerSecond = 0.0f;
		uint32 ViewportWidth = 0;
		uint32 ViewportHeight = 0;
		const char* BackendName = "Unknown";
		std::string AdapterName;
		FLogRingBuffer* LogBuffer = nullptr;
		// Lets a panel look up a pass by type; see FRenderer::FindPass.
		FRenderer* Renderer = nullptr;
		// Shared selection, owned by FEditorLayer so it survives the per frame rebuild of this struct.
		FEditorSelection* Selection = nullptr;

		// Convenience wrapper so panels do not need to null check the renderer.
		template<typename PassType>
		PassType* FindPass() const
		{
			return Renderer != nullptr ? Renderer->FindPass<PassType>() : nullptr;
		}

		// The scene being rendered, or null when none is loaded. Read through the renderer rather than
		// duplicated here, so there is one source of truth for what is on screen.
		FScene* GetScene() const { return Renderer != nullptr ? Renderer->GetScene() : nullptr; }
	};
} // namespace Lime
