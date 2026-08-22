# LimeLaunch: the engine owned entry point.
#
# An OBJECT library, not a static library: object files are then added to the executable
# unconditionally, so main and every static registrar survive linking. With a static library the
# linker would discard object files whose only content is a self registration.

add_library(LimeLaunch OBJECT
	LaunchMain.cpp
)

target_include_directories(LimeLaunch PUBLIC ${LIME_ENGINE_INCLUDE_DIRS})
target_link_libraries(LimeLaunch PUBLIC LimeCompilerOptions LimeRuntime)
set_target_properties(LimeLaunch PROPERTIES FOLDER "Engine/Runtime")
