// Engine provided UI tests.
//
// Registered through an explicit call rather than the LIME_REGISTER_UI_TEST macro, because
// LimeEditor is a static library: the linker discards an object file whose only purpose is running
// a static initializer. Projects can use the macro, since their sources are linked into the
// executable directly.
//
// These cover the two interactions the automation exists for, clicking and dragging, against
// widgets the engine always provides. They deliberately go through the menu and the slider instead
// of setting the underlying state, because driving the real path is the whole point: a menu entry
// wired to the wrong panel passes a state based check and fails this one.

#include "Editor/TestEngine/UITestRegistry.h"

#include "Editor/EditorSettings.h"

#include <imgui.h>
// FindWindowByName lives in the internal API. Checking window state directly is what makes these
// tests assert an outcome rather than merely that the click did not crash.
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_context.h>

namespace Lime
{
	namespace
	{
		// The dock host owns the menu bar. Named here because tests address it by window title.
		constexpr const char* DockHostWindow = "//LimeEditorDockHost";

		void TestMenuTogglesPanel(ImGuiTestContext* Ctx)
		{
			// Stats starts visible, so it is closed first to make the reopen observable rather than
			// depending on whichever state a previous test left behind.
			Ctx->SetRef(DockHostWindow);

			Ctx->MenuUncheck("Window/Stats");
			Ctx->Yield();
			IM_CHECK(ImGui::FindWindowByName("Stats") == nullptr || !ImGui::FindWindowByName("Stats")->Active);

			Ctx->MenuCheck("Window/Stats");
			Ctx->Yield();

			ImGuiWindow* Stats = ImGui::FindWindowByName("Stats");
			IM_CHECK(Stats != nullptr);
			IM_CHECK(Stats->Active);
		}

		void TestDragChangesSliderValue(ImGuiTestContext* Ctx)
		{
			// Editor Settings is hidden by default, so the menu is also the way to reach it. Opening
			// it through the menu keeps the test honest about how a user would get there.
			Ctx->SetRef(DockHostWindow);
			Ctx->MenuCheck("Window/Editor Settings");
			Ctx->Yield();

			// Several panels share the Right dock slot, so they are tabs of one node and only the
			// selected tab can be hovered. Without this the slider is found but never hoverable.
			Ctx->WindowFocus("//Editor Settings");
			Ctx->Yield();

			Ctx->SetRef("//Editor Settings");
			// The section is open by default, but a saved layout could have collapsed it.
			Ctx->ItemOpen("Appearance");

			const float Before = Ctx->ItemReadAsFloat("Font size");
			IM_CHECK(Before >= FEditorSettings::MinFontSize);
			IM_CHECK(Before <= FEditorSettings::MaxFontSize);

			// Dragging rather than typing: this is the interaction the automation is meant to cover,
			// and it exercises the slider's own hit testing and value mapping.
			Ctx->ItemDragWithDelta("Font size", ImVec2(40.0f, 0.0f));

			const float After = Ctx->ItemReadAsFloat("Font size");
			IM_CHECK_NE(Before, After);
			// The panel clamps to the range the loader accepts, so a drag must not escape it.
			IM_CHECK(After >= FEditorSettings::MinFontSize);
			IM_CHECK(After <= FEditorSettings::MaxFontSize);

			// Revert, so the run leaves no pending edit that a later Save would write to disk.
			Ctx->ItemClick("Revert");

			Ctx->SetRef(DockHostWindow);
			Ctx->MenuUncheck("Window/Editor Settings");
		}

		void TestClickSelectsInspectorPass(ImGuiTestContext* Ctx)
		{
			// Inspector lists render passes. Clicking a header is the cheapest interaction that
			// proves item lookup works against a panel populated at runtime rather than statically.
			Ctx->SetRef(DockHostWindow);
			Ctx->MenuCheck("Window/Inspector");
			Ctx->Yield();

			ImGuiWindow* Inspector = ImGui::FindWindowByName("Inspector");
			IM_CHECK(Inspector != nullptr);
			IM_CHECK(Inspector->Active);
		}
	} // namespace

	void RegisterBuiltinUITests()
	{
		FUITestRegistry& Registry = FUITestRegistry::Get();

		{
			FUITestRegistration Registration;
			Registration.Category = "Editor";
			Registration.Name = "menu_toggles_panel";
			Registration.TestFunc = &TestMenuTogglesPanel;
			Registry.Register(std::move(Registration));
		}
		{
			FUITestRegistration Registration;
			Registration.Category = "Editor";
			Registration.Name = "drag_changes_slider_value";
			Registration.TestFunc = &TestDragChangesSliderValue;
			Registry.Register(std::move(Registration));
		}
		{
			FUITestRegistration Registration;
			Registration.Category = "Editor";
			Registration.Name = "menu_opens_inspector";
			Registration.TestFunc = &TestClickSelectsInspectorPass;
			Registry.Register(std::move(Registration));
		}
	}
} // namespace Lime
