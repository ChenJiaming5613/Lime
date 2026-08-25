# LimeAutomation: local HTTP command server used by external test scripts.
#
# Optional via LIME_BUILD_AUTOMATION. Links the editor when it is present, because the panel commands
# reach into the editor layer.
#
# Networking comes from cpp-httplib, so this module carries no platform specific socket code.

set(AutomationDeps LimeRenderer)
if(LIME_BUILD_EDITOR)
	list(APPEND AutomationDeps LimeEditor)
endif()

lime_add_module(
	NAME LimeAutomation
	SOURCES
		AutomationCommandRegistry.h
		AutomationCommandRegistry.cpp
		AutomationReflection.h
		AutomationReflection.cpp
		AutomationScript.h
		AutomationScript.cpp
		AutomationServer.h
		AutomationServer.cpp
		AutomationTypes.h
		AutomationTypes.cpp
		ScreenshotService.h
		ScreenshotService.cpp
		Commands/BuiltinCommands.cpp
		Commands/CoreCommands.cpp
		Commands/EditorCommands.cpp
		Commands/FrameGraphCommands.cpp
		Commands/RenderPassCommands.cpp
		Commands/SceneCommands.cpp
		Commands/ScriptCommands.cpp
		Commands/SettingsCommands.cpp
		Commands/UITestCommands.cpp
	PUBLIC_DEPS ${AutomationDeps}
	# httplib is private: it appears only in the server's translation unit, so its <windows.h> include
	# does not leak into anything that links this module.
	# stb_image_write comes from the tinygltf target, which already compiles its implementation.
	PRIVATE_DEPS tinygltf httplib
	FOLDER "Engine/Runtime"
)
