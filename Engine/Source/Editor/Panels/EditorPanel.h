// Editor panel interface.

#pragma once

#include "Editor/EditorContext.h"

namespace Lime
{
	class IEditorPanel
	{
	public:
		virtual ~IEditorPanel() = default;

		virtual const char* GetName() const = 0;
		virtual void OnDrawUI(const FEditorContext& Context) = 0;

		bool IsVisible() const { return bVisible; }
		void SetVisible(bool bInVisible) { bVisible = bInVisible; }
		bool* GetVisiblePtr() { return &bVisible; }

	protected:
		bool bVisible = true;
	};
} // namespace Lime
