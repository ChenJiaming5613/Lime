# Adds every project directory.
#
# A project qualifies in one of two ways:
#   - it has a CMakeLists.txt, which is used as is; or
#   - it has a ProjectSettings.json, in which case a stub CMakeLists.txt is generated for it.
#
# The second form is what lets a project consist of nothing but configuration. Everything such a
# project needs already exists in the engine: main, the built-in render passes and their shaders.
#
# Each project is given a binary directory inside itself, so the intermediate files CMake generates for
# it (CMakeFiles, object files, response files, per config Ninja fragments) land in
# Projects/<Name>/Intermediate/Build rather than in the shared build tree. Together with the output
# directories lime_add_project sets, that keeps a project's sources and everything built from them in
# one place.

file(GLOB ProjectEntries RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(ProjectEntry ${ProjectEntries})
	# Named ProjectDir because that is the placeholder the stub template substitutes, and it must hold
	# an absolute path: the stub is configured into a different directory than the project itself.
	set(ProjectDir "${CMAKE_CURRENT_SOURCE_DIR}/${ProjectEntry}")
	if(NOT IS_DIRECTORY "${ProjectDir}")
		continue()
	endif()

	if(EXISTS "${ProjectDir}/CMakeLists.txt")
		add_subdirectory(${ProjectEntry} "${ProjectDir}/Intermediate/Build")
	elseif(EXISTS "${ProjectDir}/ProjectSettings.json")
		# The stub goes under Intermediate, which is git ignored, so generating it does not add anything
		# to the source tree. configure_file rather than file(WRITE) because it leaves the file alone
		# when the contents have not changed, and rewriting it every configure would invalidate the
		# whole project's build.
		set(GeneratedDir "${ProjectDir}/Intermediate/Generated")
		configure_file("${LIME_ROOT_DIR}/CMake/ZeroCodeProject.cmake.in" "${GeneratedDir}/CMakeLists.txt" @ONLY)

		# add_subdirectory(<source> <binary>) is the only way to give a project its own binary
		# directory; CMake does not allow overriding where a target's object files go.
		add_subdirectory("${GeneratedDir}" "${ProjectDir}/Intermediate/Build")
	endif()
endforeach()
