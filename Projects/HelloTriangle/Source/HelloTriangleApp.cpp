#include "HelloTriangleApp.h"

#include "Core/Logging/LogManager.h"
#include "Engine/Engine.h"

#include <imgui.h>

namespace Lime
{
	bool FHelloTriangleApp::OnInitialize(FEngine& InEngine)
	{
		Engine = &InEngine;

		TrianglePass = std::make_shared<FTrianglePass>();
		InEngine.GetRenderer().AddPass(TrianglePass);
		InEngine.GetRenderer().SetClearColor(BackgroundColor);

		// Panels stay generic: the project injects its own controls rather than the editor knowing
		// about FTriangleSettings.
#if LIME_WITH_EDITOR
		if (FEditorLayer* Editor = InEngine.GetEditor())
		{
			Editor->GetInspectorPanel().SetDrawDelegate([this] { DrawInspectorControls(); });
		}
#endif

		LIME_LOG_INFO(LIME_LOG_CATEGORY_APP, "HelloTriangle running on {} ({})", ToString(InEngine.GetDeviceManager().GetBackend()),
		              InEngine.GetDeviceManager().GetAdapterName());
		LIME_LOG_INFO(LIME_LOG_CATEGORY_APP, "Use the Inspector panel to change rotation speed and colours");
		return TrianglePass != nullptr;
	}

	void FHelloTriangleApp::OnUpdate(float DeltaSeconds)
	{
		if (TrianglePass != nullptr)
		{
			TrianglePass->Update(DeltaSeconds);
		}
	}

	void FHelloTriangleApp::OnDrawEditorUI()
	{
		// Controls live in the Inspector through the delegate; nothing extra to draw here.
	}

	void FHelloTriangleApp::DrawInspectorControls()
	{
		if (TrianglePass == nullptr)
		{
			return;
		}

		if (!ImGui::CollapsingHeader("Triangle", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		FTriangleSettings& Settings = TrianglePass->GetSettings();

		ImGui::Checkbox("Pause rotation", &Settings.bPaused);
		ImGui::SliderFloat("Speed", &Settings.RotationSpeed, -6.0f, 6.0f, "%.2f rad/s");
		ImGui::SliderFloat("Field of view", &Settings.FovDegrees, 20.0f, 110.0f, "%.0f deg");
		ImGui::ColorEdit4("Tint", &Settings.Tint.X, ImGuiColorEditFlags_NoInputs);

		if (ImGui::ColorEdit3("Background", &BackgroundColor.X, ImGuiColorEditFlags_NoInputs) && Engine != nullptr)
		{
			Engine->GetRenderer().SetClearColor(BackgroundColor);
		}

		ImGui::Spacing();
		ImGui::Text("Angle %.2f rad", TrianglePass->GetRotationRadians());

		if (ImGui::Button("Reset"))
		{
			Settings = FTriangleSettings{};
			BackgroundColor = { 0.06f, 0.07f, 0.09f, 1.0f };
			if (Engine != nullptr)
			{
				Engine->GetRenderer().SetClearColor(BackgroundColor);
			}
			LIME_LOG_INFO(LIME_LOG_CATEGORY_APP, "Triangle settings reset");
		}
	}

	void FHelloTriangleApp::OnShutdown()
	{
		TrianglePass.reset();
		Engine = nullptr;
	}
} // namespace Lime
