# The engine owned entry point.
#
# LaunchMain.cpp is not built into a library: it is added to each project's executable by
# lime_add_project, because it needs LIME_PROJECT_SOURCE_DIR, whose value differs per project. A
# shared target could not carry that. Compiling it into the executable also guarantees main and every
# static registrar survive linking.

set(LIME_LAUNCH_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/LaunchMain.cpp" CACHE INTERNAL "Engine entry point source")
