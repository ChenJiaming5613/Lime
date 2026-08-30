// Shows the scene texture and reports the size the renderer should use.
//
// The panel only learns its size while the UI is being built, which is after the scene was rendered,
// so the request is applied on the next frame. Resizing therefore stretches the image for one frame.

#pragma once

#include "Editor/Panels/EditorPanel.h"

#include <imgui.h>

namespace Lime
{
	class FViewportPanel final : public IEditorPanel
	{
	public:
		FViewportPanel();

		const char* GetName() const override { return Name.c_str(); }
		// The scene fills the central node, matching the layout of a typical editor.
		EEditorDockSlot GetDefaultDockSlot() const override { return EEditorDockSlot::Center; }

		uint32 GetViewportIndex() const { return ViewportIndex; }

		void OnDrawUI(const FEditorContext& Context) override;

		void SetTextureId(ImTextureID InTextureId) { TextureId = InTextureId; }
		// Dimensions of the texture this panel samples, used to preserve its aspect ratio when the panel
		// and the texture disagree. Zero means the size is not known yet.
		void SetTextureSize(uint32 Width, uint32 Height) { TextureWidth = Width; TextureHeight = Height; }

		// Content region measured during the last draw; zero until the panel has been drawn once.
		uint32 GetDesiredWidth() const { return DesiredWidth; }
		uint32 GetDesiredHeight() const { return DesiredHeight; }
		bool IsHovered() const { return bHovered; }

	private:
		static uint32 ViewportCount;

		std::string Name;
		uint32 ViewportIndex;
		ImTextureID TextureId = ImTextureID_Invalid;
		uint32 TextureWidth = 0;
		uint32 TextureHeight = 0;
		uint32 DesiredWidth = 0;
		uint32 DesiredHeight = 0;
		bool bHovered = false;
	};
} // namespace Lime
