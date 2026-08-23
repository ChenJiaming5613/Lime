// UI tests owned by this project.
//
// Registered with LIME_REGISTER_UI_TEST, which works here because project sources are linked into
// the executable directly. The engine cannot use the macro for its own tests: LimeEditor is a static
// library, and the linker drops an object file that only runs a static initializer.
//
// These drive the Inspector controls that reflection generates from FTriangleSettings, so they cover
// the path from a real mouse drag through to the value the pass actually renders with.
//
// The whole file compiles away when the test engine is disabled. Project sources are globbed, so
// this file is always part of the build and has to guard itself.

#include "Core/CoreTypes.h"

#if LIME_WITH_IMGUI_TEST_ENGINE

#include "TrianglePass.h"

#include "Core/Reflection/Reflection.h"
#include "Editor/TestEngine/UITestRegistry.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_test_engine/imgui_te_context.h>

namespace HelloTriangle::UITests
{
	constexpr const char* DockHostWindow = "//LimeEditorDockHost";

	// The Inspector wraps each pass in PushID(Pass.get()), so the pass pointer sits between the
	// window and the widget in the item path. The ** wildcard steps over it, which also means the
	// header itself cannot be addressed by label: it is inside the same PushID scope.
	constexpr const char* SpeedSlider = "//Inspector/**/Speed";
	constexpr const char* PauseCheckbox = "//Inspector/**/Pause rotation";

	// Opens the Inspector and brings it to the front of its dock node.
	//
	// Making the panel visible is not enough: Stats and Inspector share the Right slot, so they are
	// tabs of one node and only the selected tab receives hovering. Without the focus step the item
	// is found but cannot be hovered, and the action then waits for a hover that never arrives.
	//
	// The pass section itself needs no ItemOpen: its header is created with DefaultOpen, and it sits
	// inside the Inspector's PushID scope so it could not be addressed by label anyway.
	void EnsureInspectorReady(ImGuiTestContext* Ctx)
	{
		Ctx->SetRef(DockHostWindow);
		Ctx->MenuCheck("Window/Inspector");
		Ctx->Yield();
		Ctx->WindowFocus("//Inspector");
		Ctx->Yield();
	}

	void TestDragSpeedSlider(ImGuiTestContext* Ctx)
	{
		EnsureInspectorReady(Ctx);

		const float Before = Ctx->ItemReadAsFloat(SpeedSlider);

		Ctx->ItemDragWithDelta(SpeedSlider, ImVec2(-30.0f, 0.0f));
		const float After = Ctx->ItemReadAsFloat(SpeedSlider);

		IM_CHECK_NE(Before, After);
		// Dragging must respect the range declared in the LIME_PROPERTY block.
		IM_CHECK(After >= -6.0f);
		IM_CHECK(After <= 6.0f);

		Ctx->ItemInputValue(SpeedSlider, Before);
	}

	void TestClickPauseCheckbox(ImGuiTestContext* Ctx)
	{
		EnsureInspectorReady(Ctx);

		Ctx->ItemCheck(PauseCheckbox);
		Ctx->Yield();
		IM_CHECK(Ctx->ItemIsChecked(PauseCheckbox));

		Ctx->ItemUncheck(PauseCheckbox);
		Ctx->Yield();
		IM_CHECK(!Ctx->ItemIsChecked(PauseCheckbox));
	}

	void TestPanelButtonResetsSettings(ImGuiTestContext* Ctx)
	{
		EnsureInspectorReady(Ctx);

		// Move the value away from its default, then use the project panel's own button to put it
		// back. This covers a click on a project supplied widget rather than an engine one.
		Ctx->ItemInputValue(SpeedSlider, 3.5f);
		IM_CHECK_FLOAT_NEAR_EQ(Ctx->ItemReadAsFloat(SpeedSlider), 3.5f, 0.01f);

		Ctx->SetRef(DockHostWindow);
		Ctx->MenuCheck("Window/Triangle");
		Ctx->Yield();
		Ctx->WindowFocus("//Triangle");

		Ctx->ItemClick("//Triangle/Reset settings");
		Ctx->Yield();

		// FTriangleSettings defaults RotationSpeed to 1.0f.
		Ctx->WindowFocus("//Inspector");
		IM_CHECK_FLOAT_NEAR_EQ(Ctx->ItemReadAsFloat(SpeedSlider), 1.0f, 0.01f);
	}
} // namespace HelloTriangle::UITests

LIME_REGISTER_UI_TEST("HelloTriangle", "drag_speed_slider", &HelloTriangle::UITests::TestDragSpeedSlider);
LIME_REGISTER_UI_TEST("HelloTriangle", "click_pause_checkbox", &HelloTriangle::UITests::TestClickPauseCheckbox);
LIME_REGISTER_UI_TEST("HelloTriangle", "panel_button_resets_settings", &HelloTriangle::UITests::TestPanelButtonResetsSettings);

#endif // LIME_WITH_IMGUI_TEST_ENGINE
