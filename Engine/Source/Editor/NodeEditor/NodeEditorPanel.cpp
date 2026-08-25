#include "Editor/NodeEditor/NodeEditorPanel.h"

#include "Core/Logging/LogManager.h"

#include <imgui.h>

// The upstream sample, verbatim.
//
// Its Example type is declared inside the .cpp, so there is no header to include and no library to
// link against: including the source is the only way to reach it without editing the vendored file.
// The sample's own <application.h> resolves to the shim in NodeEditor/Shim, which is on this target's
// include path ahead of anything else providing that name.
//
// basic-interaction rather than blueprints. The blueprint sample calls ImGui::BeginHorizontal and
// ImGui::Spring, which belong to a stack layout API that exists only in the fork of Dear ImGui this
// library vendors for its own examples (1.84 WIP). This engine uses mainline 1.92, which has never had
// that API, so building the blueprint sample would mean carrying roughly 700 lines of patched ImGui
// core and re-merging it on every upgrade. basic-interaction uses BeginGroup and SameLine only, so it
// compiles against mainline unchanged while still exercising node creation, linking and deletion.
//
// Everything the sample declares at file scope is static, so nothing here escapes this translation
// unit. Its Main function is compiled and never called.
#include "basic-interaction-example.cpp"

namespace Lime
{
	// Holds the sample instance, so its type stays out of the panel's header.
	struct FNodeEditorPanel::FImpl
	{
		Example Sample{ "BasicInteraction" };
	};

	FNodeEditorPanel::FNodeEditorPanel()
	    : Impl(std::make_unique<FImpl>())
	{
	}

	FNodeEditorPanel::~FNodeEditorPanel()
	{
		if (bStarted)
		{
			// Releases the editor context the sample created in OnStart.
			Impl->Sample.OnStop();
			bStarted = false;
		}
	}

	void FNodeEditorPanel::EnsureStarted()
	{
		if (bStarted)
		{
			return;
		}

		Impl->Sample.OnStart();
		bStarted = true;

		LIME_LOG_INFO(LIME_LOG_CATEGORY_EDITOR, "Node editor sample started");
	}

	void FNodeEditorPanel::OnDrawUI(const FEditorContext& Context)
	{
		LIME_UNUSED(Context);

		// A first-run size, because the sample calls ed::Begin with a zero extent, meaning "use whatever
		// space is available". Upstream that space is a maximised window; a freshly opened panel is far
		// smaller, and the graph would be clipped to a strip showing nothing but the frame rate. Applied
		// as a condition rather than every frame so a size the user chose is kept.
		ImGui::SetNextWindowSize(ImVec2(720.0f, 480.0f), ImGuiCond_FirstUseEver);

		if (!ImGui::Begin(GetName(), GetVisiblePtr()))
		{
			ImGui::End();
			return;
		}

		// Started on first display rather than on construction, so a panel that is never opened costs
		// nothing: the editor context and its settings file are only created when the panel is shown.
		EnsureStarted();

		// The sample draws straight into the current window without opening one of its own, which is
		// what makes it usable as a panel body.
		Impl->Sample.OnFrame(ImGui::GetIO().DeltaTime);

		// Reframes the graph once the panel has a real size.
		//
		// The sample zooms to fit on its own first frame, but that frame happens while the window is
		// still being laid out, so the fit is computed against a nearly empty rectangle and locks in a
		// zoom around 0.09: the nodes are drawn correctly and are a few pixels wide. Repeating it here,
		// after a few frames have established the true extent, is what makes them legible.
		//
		// The editor has to be made current again first. OnFrame ends with SetCurrentEditor(nullptr), so
		// calling a navigation function straight after it dereferences a null context and takes the
		// process down on the first frame the panel is drawn.
		//
		// Bounded to the first few frames so it cannot fight a zoom the user set afterwards.
		if (FramesSinceStart <= ReframeAfterFrames)
		{
			++FramesSinceStart;
			if (FramesSinceStart == ReframeAfterFrames)
			{
				// m_Context is the sample's own editor context, left public by its struct. Borrowing it
				// beats duplicating the sample's setup, and it is only read here.
				ed::SetCurrentEditor(Impl->Sample.m_Context);
				ed::NavigateToContent(0.0f);
				ed::SetCurrentEditor(nullptr);
			}
		}

		ImGui::End();
	}
} // namespace Lime
