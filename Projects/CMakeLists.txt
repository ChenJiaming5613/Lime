# Adds every project that provides its own CMakeLists.txt.
#
# That file is a single call to lime_add_project(), and it is the only thing a project must author
# besides ProjectSettings.json. C++ sources are optional: the engine owns main and provides the built-in
# render passes, so a project that only configures existing engine features needs no code at all.
#
# Each one is given a binary directory inside the project itself, so the intermediate files CMake
# generates for it (CMakeFiles, object files, response files, per config Ninja fragments) land in
# Projects/<Name>/Intermediate/Build rather than in the shared build tree. Together with the output
# directories lime_add_project sets, that keeps a project's sources and everything built from them in
# one place.

file(GLOB ProjectDirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(ProjectDir ${ProjectDirs})
	if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${ProjectDir}"
		AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${ProjectDir}/CMakeLists.txt")
		add_subdirectory(${ProjectDir} "${CMAKE_CURRENT_SOURCE_DIR}/${ProjectDir}/Intermediate/Build")
	endif()
endforeach()
