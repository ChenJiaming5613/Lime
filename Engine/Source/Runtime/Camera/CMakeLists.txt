# LimeCamera: camera abstraction and the controllers that drive it.
#
# Depends on LimePlatform for input and cursor control, and on nothing graphics related: a camera only
# produces matrices. That keeps the control rules unit testable without a device.

lime_add_module(
	NAME LimeCamera
	SOURCES
		Camera.h
		FlyCameraController.h
		FlyCameraController.cpp
		PerspectiveCamera.h
		PerspectiveCamera.cpp
	PUBLIC_DEPS
		LimeCore
		LimePlatform
	FOLDER "Engine/Runtime"
)
