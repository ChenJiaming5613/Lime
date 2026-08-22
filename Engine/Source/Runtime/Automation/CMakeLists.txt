# LimeAutomation: local TCP command server used by external test scripts.
#
# Optional via LIME_BUILD_AUTOMATION. Links the editor when it is present, because the panel commands
# reach into the editor layer.

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
		AutomationServer.h
		AutomationServer.cpp
		AutomationTypes.h
		AutomationTypes.cpp
		ScreenshotService.h
		ScreenshotService.cpp
		Commands/BuiltinCommands.cpp
		Commands/CoreCommands.cpp
		Commands/EditorCommands.cpp
		Commands/RenderPassCommands.cpp
		Commands/SettingsCommands.cpp
	PUBLIC_DEPS ${AutomationDeps}
	# stb_image_write comes from the tinygltf target, which already compiles its implementation.
	PRIVATE_DEPS tinygltf
	FOLDER "Engine/Runtime"
)
