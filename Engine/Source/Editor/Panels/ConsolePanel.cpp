#include "Editor/Panels/ConsolePanel.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

namespace Lime
{
	namespace
	{
		ImVec4 GetLevelColor(ELogLevel Level)
		{
			switch (Level)
			{
				case ELogLevel::Trace:
					return { 0.55f, 0.55f, 0.58f, 1.0f };
				case ELogLevel::Debug:
					return { 0.55f, 0.75f, 0.95f, 1.0f };
				case ELogLevel::Info:
					return { 0.85f, 0.87f, 0.90f, 1.0f };
				case ELogLevel::Warning:
					return { 0.98f, 0.78f, 0.30f, 1.0f };
				case ELogLevel::Error:
					return { 0.96f, 0.42f, 0.38f, 1.0f };
				case ELogLevel::Critical:
					return { 1.0f, 0.25f, 0.55f, 1.0f };
				default:
					return { 1.0f, 1.0f, 1.0f, 1.0f };
			}
		}

		bool ContainsCaseInsensitive(std::string_view Haystack, std::string_view Needle)
		{
			if (Needle.empty())
			{
				return true;
			}

			const auto Found =
			    std::search(Haystack.begin(), Haystack.end(), Needle.begin(), Needle.end(), [](char Left, char Right)
				            { return std::tolower(static_cast<unsigned char>(Left)) == std::tolower(static_cast<unsigned char>(Right)); });
			return Found != Haystack.end();
		}
	} // namespace

	void FConsolePanel::OnDrawUI(const FEditorContext& Context)
	{
		if (!bVisible)
		{
			return;
		}

		if (!ImGui::Begin(GetName(), &bVisible))
		{
			ImGui::End();
			return;
		}

		if (Context.LogBuffer == nullptr)
		{
			ImGui::TextUnformatted("No log buffer attached.");
			ImGui::End();
			return;
		}

		DrawToolbar(Context);
		ImGui::Separator();

		// Only re-copy when the buffer actually changed.
		const uint64 Revision = Context.LogBuffer->GetRevision();
		if (Revision != LastRevision)
		{
			Context.LogBuffer->CopyTo(Snapshot);
			LastRevision = Revision;
			bFilterDirty = true;
		}

		if (bFilterDirty)
		{
			FilteredEntries.clear();
			FilteredEntries.reserve(Snapshot.size());
			for (const FLogEntry& Entry : Snapshot)
			{
				if (PassesFilter(Entry))
				{
					FilteredEntries.push_back(&Entry);
				}
			}
			bFilterDirty = false;
		}

		DrawEntries();
		ImGui::End();
	}

	void FConsolePanel::DrawToolbar(const FEditorContext& Context)
	{
		if (ImGui::Button("Clear"))
		{
			Context.LogBuffer->Clear();
			Snapshot.clear();
			FilteredEntries.clear();
			LastRevision = Context.LogBuffer->GetRevision();
		}

		ImGui::SameLine();
		ImGui::Checkbox("Auto scroll", &bAutoScroll);

		ImGui::SameLine();
		ImGui::SetNextItemWidth(180.0f);
		if (ImGui::InputTextWithHint("##ConsoleSearch", "Filter", &SearchText))
		{
			bFilterDirty = true;
		}

		ImGui::SameLine();
		if (ImGui::BeginCombo("##ConsoleLevels", "Levels"))
		{
			for (SizeType Index = 0; Index < LevelFilter.size(); ++Index)
			{
				const auto Level = static_cast<ELogLevel>(Index);
				if (ImGui::Checkbox(ToString(Level), &LevelFilter[Index]))
				{
					bFilterDirty = true;
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		ImGui::TextDisabled("%zu / %zu", FilteredEntries.size(), Snapshot.size());
	}

	bool FConsolePanel::PassesFilter(const FLogEntry& Entry) const
	{
		const SizeType LevelIndex = static_cast<SizeType>(Entry.Level);
		if (LevelIndex < LevelFilter.size() && !LevelFilter[LevelIndex])
		{
			return false;
		}

		return SearchText.empty() || ContainsCaseInsensitive(Entry.Message, SearchText) ||
		       ContainsCaseInsensitive(Entry.Category, SearchText);
	}

	void FConsolePanel::DrawEntries()
	{
		if (!ImGui::BeginChild("##ConsoleEntries", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			ImGui::EndChild();
			return;
		}

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));

		// The clipper keeps scrolling responsive with a full ring buffer.
		ImGuiListClipper Clipper;
		Clipper.Begin(static_cast<int>(FilteredEntries.size()));
		while (Clipper.Step())
		{
			for (int Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; ++Index)
			{
				const FLogEntry& Entry = *FilteredEntries[static_cast<SizeType>(Index)];
				ImGui::PushStyleColor(ImGuiCol_Text, GetLevelColor(Entry.Level));
				ImGui::Text("[%8.3f] [%s] [%s] %s", Entry.TimeSeconds, ToString(Entry.Level), Entry.Category.c_str(),
				            Entry.Message.c_str());
				ImGui::PopStyleColor();
			}
		}
		Clipper.End();

		ImGui::PopStyleVar();

		if (bAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		{
			ImGui::SetScrollHereY(1.0f);
		}

		ImGui::EndChild();
	}
} // namespace Lime
