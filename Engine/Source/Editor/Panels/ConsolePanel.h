// Log viewer. Reads a snapshot of the ring buffer, so the UI never holds the log lock.

#pragma once

#include "Core/Logging/LogRingBuffer.h"
#include "Editor/Panels/EditorPanel.h"

#include <array>
#include <string>
#include <vector>

namespace Lime
{
	class FConsolePanel final : public IEditorPanel
	{
	public:
		const char* GetName() const override { return "Console"; }
		EEditorDockSlot GetDefaultDockSlot() const override { return EEditorDockSlot::Bottom; }

		void OnDrawUI(const FEditorContext& Context) override;

	private:
		void DrawToolbar(const FEditorContext& Context);
		void DrawEntries();
		bool PassesFilter(const FLogEntry& Entry) const;

		std::vector<FLogEntry> Snapshot;
		std::vector<const FLogEntry*> FilteredEntries;
		std::array<bool, static_cast<SizeType>(ELogLevel::Count)> LevelFilter{ false, true, true, true, true, true };
		std::string SearchText;
		uint64 LastRevision = 0;
		bool bAutoScroll = true;
		bool bFilterDirty = true;
	};
} // namespace Lime
