#include "Editor/Panels/ProjectSettingsPanel.h"

#include "Core/Logging/LogManager.h"

#include <imgui.h>

#include <array>

namespace Lime
{
	namespace
	{
		constexpr std::array<const char*, 2> BackendLabels = { "D3D12", "Vulkan" };
		constexpr std::array<const char*, 3> ValidationLabels = { "Off", "Debug only", "On" };

		void CopyToBuffer(std::array<char, 256>& Buffer, const std::string& Text)
		{
			const SizeType Length = std::min(Text.size(), Buffer.size() - 1);
			std::memcpy(Buffer.data(), Text.data(), Length);
			Buffer[Length] = '\0';
		}
	} // namespace

	void FProjectSettingsPanel::Initialize(const FProjectSettings& CurrentSettings)
	{
		Edited = CurrentSettings;
		Saved = CurrentSettings;
		CopyToBuffer(TitleBuffer, CurrentSettings.WindowTitle);
		bInitialized = true;
	}

	void FProjectSettingsPanel::DrawWindowSection()
	{
		if (!ImGui::CollapsingHeader("Window", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		if (ImGui::InputText("Title", TitleBuffer.data(), TitleBuffer.size()))
		{
			Edited.WindowTitle = TitleBuffer.data();
		}

		int32 Size[2] = { static_cast<int32>(Edited.WindowWidth), static_cast<int32>(Edited.WindowHeight) };
		if (ImGui::DragInt2("Size", Size, 4.0f, 320, 7680))
		{
			Edited.WindowWidth = static_cast<uint32>(std::max(Size[0], 320));
			Edited.WindowHeight = static_cast<uint32>(std::max(Size[1], 240));
		}
	}

	void FProjectSettingsPanel::DrawRHISection()
	{
		if (!ImGui::CollapsingHeader("RHI", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		int32 BackendIndex = static_cast<int32>(Edited.Backend);
		if (ImGui::Combo("Backend", &BackendIndex, BackendLabels.data(), static_cast<int32>(BackendLabels.size())))
		{
			const auto Chosen = static_cast<ERHIBackend>(BackendIndex);
			// Selecting a backend that was not compiled in would produce a file that cannot start.
			if (IsBackendEnabled(Chosen))
			{
				Edited.Backend = Chosen;
			}
			else
			{
				LIME_LOG_WARNING(LIME_LOG_CATEGORY_EDITOR, "Backend {} is not compiled in", ToString(Chosen));
			}
		}

		ImGui::Checkbox("VSync", &Edited.bVSync);

		int32 BufferCount = static_cast<int32>(Edited.BackBufferCount);
		if (ImGui::SliderInt("Back buffers", &BufferCount, 2, 4))
		{
			Edited.BackBufferCount = static_cast<uint32>(BufferCount);
		}

		int32 ValidationIndex = static_cast<int32>(Edited.Validation);
		if (ImGui::Combo("Validation", &ValidationIndex, ValidationLabels.data(), static_cast<int32>(ValidationLabels.size())))
		{
			Edited.Validation = static_cast<EValidationMode>(ValidationIndex);
		}
	}

	void FProjectSettingsPanel::DrawEditorSection()
	{
		if (!ImGui::CollapsingHeader("Editor", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		ImGui::Checkbox("Enabled", &Edited.bEnableEditor);
		if (!Edited.bEnableEditor)
		{
			// Saving this would leave no UI to switch it back on, so it is worth calling out.
			ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "The editor will not load next launch.");
			ImGui::TextDisabled("Re-enable it in the file, or run without --no-editor after editing.");
		}
	}

	void FProjectSettingsPanel::DrawToolbar()
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
			CopyToBuffer(TitleBuffer, Edited.WindowTitle);
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

	void FProjectSettingsPanel::OnDrawUI(const FEditorContext& Context)
	{
		LIME_UNUSED(Context);

		if (!ImGui::Begin(GetName(), GetVisiblePtr()))
		{
			ImGui::End();
			return;
		}

		if (!bInitialized)
		{
			ImGui::TextDisabled("Settings are not available");
			ImGui::End();
			return;
		}

		const std::filesystem::path AuthoringPath = FProjectSettings::ResolveAuthoringPath();
		if (AuthoringPath.empty())
		{
			ImGui::TextWrapped("No project source directory is known, so settings cannot be saved from here.");
			ImGui::Separator();
		}

		// Stated once at the top rather than repeated per field: it applies to all of them.
		ImGui::TextDisabled("Applies on next launch");
		ImGui::Separator();

		DrawToolbar();
		ImGui::Separator();

		DrawWindowSection();
		DrawRHISection();
		DrawEditorSection();

		ImGui::Separator();
		// Shown last: useful when it is unclear which file a change would land in.
		if (!AuthoringPath.empty())
		{
			ImGui::TextDisabled("%s", AuthoringPath.string().c_str());
		}
		ImGui::TextDisabled("Project '%s'", Edited.ProjectName.c_str());

		ImGui::End();
	}
} // namespace Lime
