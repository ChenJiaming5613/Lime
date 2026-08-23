// Registry for UI tests, which drive editor widgets by injecting real input events.
//
// A UI test is written as straight line code even though its actions span many frames: the test
// engine suspends it on a coroutine between them. See FEditorTestEngine for the lifecycle.
//
// Two registration paths exist, for the same reason FRenderPassRegistry and FEditorPanelRegistry
// have them:
//
//   Projects use LIME_REGISTER_UI_TEST. Project sources are compiled straight into the executable
//   (see CMake/LimeProject.cmake), so a static initializer cannot be dropped.
//
//   The engine calls RegisterBuiltinUITests() explicitly. LimeEditor is a static library, and the
//   linker discards an object file whose only purpose is registration.
//
// Registrations are collected here rather than handed to the test engine directly, because they are
// declared during static initialization, long before an ImGui context exists.

#pragma once

#include "Core/CoreTypes.h"

#include <functional>
#include <string>
#include <vector>

struct ImGuiTestContext;

namespace Lime
{
	struct FUITestRegistration
	{
		// Drives the test: clicks, drags and checks. Runs on the coroutine, interleaved with the main
		// thread rather than in parallel with it, so it may touch engine state without locking.
		using FTestFunc = std::function<void(ImGuiTestContext*)>;

		// Groups tests in the test engine UI and in filters. Conventionally the project name, or
		// "Editor" for the built-in ones.
		std::string Category;
		std::string Name;
		FTestFunc TestFunc;
		// Optional. Supplies a window of its own, for testing a widget in isolation. Tests that drive
		// existing editor panels leave this empty, since FEditorLayer::DrawUI already draws them.
		FTestFunc GuiFunc;

		// "Category/Name", which is how a test is addressed over the automation API.
		std::string GetQualifiedName() const { return Category + "/" + Name; }
	};

	class FUITestRegistry
	{
	public:
		static FUITestRegistry& Get();

		// A duplicate qualified name replaces the earlier entry, letting a project override a
		// built-in test the same way it can override a panel.
		void Register(FUITestRegistration Registration);

		const std::vector<FUITestRegistration>& GetRegistrations() const { return Registrations; }

	private:
		FUITestRegistry() = default;

		std::vector<FUITestRegistration> Registrations;
	};

	// Engine provided tests. Called explicitly because a static library would lose them.
	void RegisterBuiltinUITests();

	// Backs LIME_REGISTER_UI_TEST. Always returns true so it can initialize a namespace scope
	// constant, which is what gives the registration a place to run.
	//
	// Takes a plain function pointer rather than FTestFunc so that converting it to a std::function,
	// which allocates, happens inside this noexcept function instead of at the call site. The call
	// site is a static initializer, where an escaping exception could not be caught by anything.
	// A project needing to capture state can still call FUITestRegistry::Register directly.
	using FUITestFuncPtr = void (*)(ImGuiTestContext*);
	bool RegisterUITest(const char* Category, const char* Name, FUITestFuncPtr TestFunc) noexcept;
} // namespace Lime

// Registers a UI test. Place at file scope in a project .cpp:
//
//   LIME_REGISTER_UI_TEST("MyGame", "open_settings", &MyGame::UITests::TestOpenSettings);
//
// The work happens in a free function rather than inline in the macro body, because a multi
// statement lambda inside the expansion trips an internal compiler error in MSVC 19.4x.
#define LIME_REGISTER_UI_TEST(Category, Name, TestFunction)                                                                                \
	namespace                                                                                                                              \
	{                                                                                                                                      \
		const bool LIME_CONCAT(bLimeRegisteredUITest_, __COUNTER__) = ::Lime::RegisterUITest((Category), (Name), (TestFunction));          \
	}
