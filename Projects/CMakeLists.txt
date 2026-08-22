# Adds every project that provides its own CMakeLists.txt.

file(GLOB ProjectDirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(ProjectDir ${ProjectDirs})
	if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${ProjectDir}"
		AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${ProjectDir}/CMakeLists.txt")
		add_subdirectory(${ProjectDir})
	endif()
endforeach()
