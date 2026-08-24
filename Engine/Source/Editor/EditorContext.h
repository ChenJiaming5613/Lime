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

		// Scene import running on a worker thread, so the viewport can say why it is empty instead of
		// looking broken.
		//
		// Copied out as plain fields rather than holding the loader's own progress type. A panel needs to
		// know that something is loading and for how long, and phrasing it this way keeps the editor from
		// depending on the asset module for one label.
		bool bSceneLoading = false;
		float SceneLoadSeconds = 0.0f;
		std::string SceneLoadFileName;

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
