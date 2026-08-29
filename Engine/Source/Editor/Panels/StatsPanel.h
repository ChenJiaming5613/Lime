// Read only runtime statistics. Built in, so every project gets it for free.

#pragma once

#include "Editor/Panels/EditorPanel.h"

namespace Lime
{
	class FStatsPanel final : public IEditorPanel
	{
	public:
		const char* GetName() const override { return "Stats"; }
		EEditorDockSlot GetDefaultDockSlot() const override { return EEditorDockSlot::Right; }
		bool IsVisibleByDefault() const override { return false; }

		void OnDrawUI(const FEditorContext& Context) override;

	private:
		static constexpr int32 HistorySize = 120;

		float FrameTimeHistory[HistorySize] = {};
		int32 HistoryOffset = 0;
	};
} // namespace Lime
