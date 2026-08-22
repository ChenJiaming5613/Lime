// The single interface a project has to implement.

#pragma once

#include "Core/CoreTypes.h"

namespace Lime
{
	class FEngine;
	class FRenderer;

	class ILimeApplication
	{
	public:
		virtual ~ILimeApplication() = default;

		// Register render passes and load resources here. Return false to abort startup.
		virtual bool OnInitialize(FEngine& Engine) = 0;
		virtual void OnUpdate(float DeltaSeconds) = 0;
		// Called between BeginFrame and EndFrame, after the pass list has run.
		virtual void OnRender(FRenderer& Renderer) { LIME_UNUSED(Renderer); }
		// Called while the editor frame is open; only invoked when the editor is enabled.
		virtual void OnDrawEditorUI() {}
		virtual void OnShutdown() {}
	};
} // namespace Lime
