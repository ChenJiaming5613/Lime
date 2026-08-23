// Keyboard and mouse state, double buffered so edge queries work.

#pragma once

#include "Core/CoreTypes.h"
#include "Core/Math/Vector.h"

#include <array>

struct GLFWwindow;

namespace Lime
{
	// Values match the GLFW key codes for the subset the engine currently needs.
	enum class EKey : int32
	{
		Unknown = -1,
		Space = 32,
		Escape = 256,
		Enter = 257,
		Tab = 258,
		Right = 262,
		Left = 263,
		Down = 264,
		Up = 265,
		F1 = 290,
		A = 65,
		D = 68,
		E = 69,
		Q = 81,
		S = 83,
		W = 87,
		LeftShift = 340,
		LeftControl = 341,
		LeftAlt = 342
	};

	enum class EMouseButton : int32
	{
		Left = 0,
		Right = 1,
		Middle = 2
	};

	// ImGui installs its own GLFW callbacks and chains to ours, so both receive input.
	class FInput
	{
	public:
		static constexpr int32 MaxKeys = 512;
		static constexpr int32 MaxMouseButtons = 8;

		static FInput& Get();

		void AttachToWindow(GLFWwindow* Window);
		void DetachFromWindow(GLFWwindow* Window);

		// Promotes the current state to the previous state; call before glfwPollEvents.
		void BeginFrame();

		bool IsKeyDown(EKey Key) const;
		bool WasKeyPressed(EKey Key) const;
		bool WasKeyReleased(EKey Key) const;

		bool IsMouseButtonDown(EMouseButton Button) const;
		bool WasMouseButtonPressed(EMouseButton Button) const;

		FVector2 GetMousePosition() const { return MousePosition; }
		FVector2 GetMouseDelta() const { return MousePosition - PreviousMousePosition; }
		float GetScrollDelta() const { return ScrollDelta; }

		// Sets both the current and previous position, making this frame's delta zero.
		//
		// Needed when the cursor is warped, which happens on entering or leaving capture: GLFW reports the
		// jump as ordinary movement, and a fly camera would spin wildly for one frame without this.
		void ResetMousePosition(const FVector2& Position);

	private:
		FInput() = default;

		LIME_NON_COPYABLE(FInput);
		LIME_NON_MOVABLE(FInput);

		static void KeyCallback(GLFWwindow* Window, int Key, int ScanCode, int Action, int Mods);
		static void MouseButtonCallback(GLFWwindow* Window, int Button, int Action, int Mods);
		static void CursorPositionCallback(GLFWwindow* Window, double PositionX, double PositionY);
		static void ScrollCallback(GLFWwindow* Window, double OffsetX, double OffsetY);

		std::array<bool, MaxKeys> KeyStates{};
		std::array<bool, MaxKeys> PreviousKeyStates{};
		std::array<bool, MaxMouseButtons> MouseStates{};
		std::array<bool, MaxMouseButtons> PreviousMouseStates{};

		FVector2 MousePosition;
		FVector2 PreviousMousePosition;
		float ScrollDelta = 0.0f;
	};
} // namespace Lime
