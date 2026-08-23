# Adds every project that provides its own CMakeLists.txt.
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
