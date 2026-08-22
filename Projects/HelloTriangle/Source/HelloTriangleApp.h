// Rotating triangle sample with the editor UI on top.

#pragma once

#include "Engine/ApplicationInterface.h"
#include "Renderer/Passes/TrianglePass.h"

#include <memory>

namespace Lime
{
	class FHelloTriangleApp final : public ILimeApplication
	{
	public:
		bool OnInitialize(FEngine& Engine) override;
		void OnUpdate(float DeltaSeconds) override;
		void OnDrawEditorUI() override;
		void OnShutdown() override;

	private:
		void DrawInspectorControls();

		FEngine* Engine = nullptr;
		std::shared_ptr<FTrianglePass> TrianglePass;
		FVector4 BackgroundColor{ 0.06f, 0.07f, 0.09f, 1.0f };
	};
} // namespace Lime
