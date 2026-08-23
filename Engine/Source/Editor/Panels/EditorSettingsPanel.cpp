#include "Editor/Panels/EditorSettingsPanel.h"

#include <imgui.h>

#include <array>

namespace Lime
{
	namespace
	{
		// Index order must match EEditorTheme.
		constexpr std::array<const char*, 3> ThemeLabels = { "Dark", "Light", "Classic" };
	} // namespace

	void FEditorSettingsPanel::Initialize(const FEditorSettings& CurrentSettings)
	{
		Edited = CurrentSettings;
		Saved = CurrentSettings;
		bInitialized = true;
	}

	void FEditorSettingsPanel::DrawAppearanceSection()
	{
		if (!ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		// Clamped to the same range LoadFromFile enforces, so the panel cannot produce a file the
		// loader would reject and silently replace with the default.
		ImGui::SliderFloat("Font size", &Edited.FontSize, FEditorSettings::MinFontSize, FEditorSettings::MaxFontSize, "%.0f");

		int32 ThemeIndex = static_cast<int32>(Edited.Theme);
		if (ImGui::Combo("Theme", &ThemeIndex, ThemeLabels.data(), static_cast<int32>(ThemeLabels.size())))
		{
			Edited.Theme = static_cast<EEditorTheme>(ThemeIndex);
		}

		float Accent[3] = { Edited.AccentColor.X, Edited.AccentColor.Y, Edited.AccentColor.Z };
		if (ImGui::ColorEdit3("Accent color", Accent))
		{
			Edited.AccentColor = { Accent[0], Accent[1], Accent[2] };
		}

		if (ImGui::Button("Reset to defaults"))
		{
			Edited = FEditorSettings();
		}
	}

	void FEditorSettingsPanel::DrawToolbar()
	{
		const bool bDirty = HasUnsavedChanges();

		ImGui::BeginDisabled(!bDirty);
		if (ImGui::Button("Save"))
		{
			if (Edited.SaveToFile())
			{
				Saved = Edited;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Revert"))
		{
			Edited = Saved;
		}
		ImGui::EndDisabled();

		ImGui::SameLine();
		if (bDirty)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "Unsaved changes");
		}
		else
		{
			ImGui::TextDisabled("Saved");
		}
	}

	void FEditorSettingsPanel::OnDrawUI(const FEditorContext& Context)
	{
		LIME_UNUSED(Context);

		if (!ImGui::Begin(GetName(), GetVisiblePtr()))
		{
			ImGui::End();
			return;
		}

		if (!bInitialized)
		{
			ImGui::TextDisabled("Editor settings are not available");
			ImGui::End();
			return;
		}

		const std::filesystem::path AuthoringPath = FEditorSettings::ResolveAuthoringPath();
		if (AuthoringPath.empty())
		{
			ImGui::TextWrapped("No project source directory is known, so editor settings cannot be saved from here.");
			ImGui::Separator();
		}

		// Stated once at the top rather than repeated per field: it applies to all of them.
		ImGui::TextDisabled("Applies on next launch");
		ImGui::Separator();

		DrawToolbar();
		ImGui::Separator();

		DrawAppearanceSection();

		ImGui::Separator();
		// Shown last: useful when it is unclear which file a change would land in.
		if (!AuthoringPath.empty())
		{
			ImGui::TextDisabled("%s", AuthoringPath.string().c_str());
		}

		ImGui::End();
	}
} // namespace Lime
