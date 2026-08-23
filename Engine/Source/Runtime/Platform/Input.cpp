#include "Platform/Input.h"

#include <GLFW/glfw3.h>

namespace Lime
{
	namespace
	{
		// Previously installed callbacks, so chaining with ImGui's GLFW backend keeps working.
		GLFWkeyfun PreviousKeyCallback = nullptr;
		GLFWmousebuttonfun PreviousMouseButtonCallback = nullptr;
		GLFWcursorposfun PreviousCursorPositionCallback = nullptr;
		GLFWscrollfun PreviousScrollCallback = nullptr;
	} // namespace

	FInput& FInput::Get()
	{
		static FInput Instance;
		return Instance;
	}

	void FInput::AttachToWindow(GLFWwindow* Window)
	{
		if (Window == nullptr)
		{
			return;
		}

		PreviousKeyCallback = glfwSetKeyCallback(Window, &FInput::KeyCallback);
		PreviousMouseButtonCallback = glfwSetMouseButtonCallback(Window, &FInput::MouseButtonCallback);
		PreviousCursorPositionCallback = glfwSetCursorPosCallback(Window, &FInput::CursorPositionCallback);
		PreviousScrollCallback = glfwSetScrollCallback(Window, &FInput::ScrollCallback);

		double CursorX = 0.0;
		double CursorY = 0.0;
		glfwGetCursorPos(Window, &CursorX, &CursorY);
		MousePosition = { static_cast<float>(CursorX), static_cast<float>(CursorY) };
		PreviousMousePosition = MousePosition;
	}

	void FInput::DetachFromWindow(GLFWwindow* Window)
	{
		if (Window == nullptr)
		{
			return;
		}

		glfwSetKeyCallback(Window, PreviousKeyCallback);
		glfwSetMouseButtonCallback(Window, PreviousMouseButtonCallback);
		glfwSetCursorPosCallback(Window, PreviousCursorPositionCallback);
		glfwSetScrollCallback(Window, PreviousScrollCallback);

		PreviousKeyCallback = nullptr;
		PreviousMouseButtonCallback = nullptr;
		PreviousCursorPositionCallback = nullptr;
		PreviousScrollCallback = nullptr;

		KeyStates.fill(false);
		PreviousKeyStates.fill(false);
		MouseStates.fill(false);
		PreviousMouseStates.fill(false);
	}

	void FInput::BeginFrame()
	{
		PreviousKeyStates = KeyStates;
		PreviousMouseStates = MouseStates;
		PreviousMousePosition = MousePosition;
		ScrollDelta = 0.0f;
	}

	void FInput::ResetMousePosition(const FVector2& Position)
	{
		MousePosition = Position;
		PreviousMousePosition = Position;
	}

	bool FInput::IsKeyDown(EKey Key) const
	{
		const int32 Index = static_cast<int32>(Key);
		return Index >= 0 && Index < MaxKeys && KeyStates[static_cast<SizeType>(Index)];
	}

	bool FInput::WasKeyPressed(EKey Key) const
	{
		const int32 Index = static_cast<int32>(Key);
		if (Index < 0 || Index >= MaxKeys)
		{
			return false;
		}
		const SizeType Slot = static_cast<SizeType>(Index);
		return KeyStates[Slot] && !PreviousKeyStates[Slot];
	}

	bool FInput::WasKeyReleased(EKey Key) const
	{
		const int32 Index = static_cast<int32>(Key);
		if (Index < 0 || Index >= MaxKeys)
		{
			return false;
		}
		const SizeType Slot = static_cast<SizeType>(Index);
		return !KeyStates[Slot] && PreviousKeyStates[Slot];
	}

	bool FInput::IsMouseButtonDown(EMouseButton Button) const
	{
		const int32 Index = static_cast<int32>(Button);
		return Index >= 0 && Index < MaxMouseButtons && MouseStates[static_cast<SizeType>(Index)];
	}

	bool FInput::WasMouseButtonPressed(EMouseButton Button) const
	{
		const int32 Index = static_cast<int32>(Button);
		if (Index < 0 || Index >= MaxMouseButtons)
		{
			return false;
		}
		const SizeType Slot = static_cast<SizeType>(Index);
		return MouseStates[Slot] && !PreviousMouseStates[Slot];
	}

	void FInput::KeyCallback(GLFWwindow* Window, int Key, int ScanCode, int Action, int Mods)
	{
		if (Key >= 0 && Key < MaxKeys && Action != GLFW_REPEAT)
		{
			Get().KeyStates[static_cast<SizeType>(Key)] = Action == GLFW_PRESS;
		}

		if (PreviousKeyCallback != nullptr)
		{
			PreviousKeyCallback(Window, Key, ScanCode, Action, Mods);
		}
	}

	void FInput::MouseButtonCallback(GLFWwindow* Window, int Button, int Action, int Mods)
	{
		if (Button >= 0 && Button < MaxMouseButtons)
		{
			Get().MouseStates[static_cast<SizeType>(Button)] = Action == GLFW_PRESS;
		}

		if (PreviousMouseButtonCallback != nullptr)
		{
			PreviousMouseButtonCallback(Window, Button, Action, Mods);
		}
	}

	void FInput::CursorPositionCallback(GLFWwindow* Window, double PositionX, double PositionY)
	{
		Get().MousePosition = { static_cast<float>(PositionX), static_cast<float>(PositionY) };

		if (PreviousCursorPositionCallback != nullptr)
		{
			PreviousCursorPositionCallback(Window, PositionX, PositionY);
		}
	}

	void FInput::ScrollCallback(GLFWwindow* Window, double OffsetX, double OffsetY)
	{
		Get().ScrollDelta += static_cast<float>(OffsetY);

		if (PreviousScrollCallback != nullptr)
		{
			PreviousScrollCallback(Window, OffsetX, OffsetY);
		}
	}
} // namespace Lime
