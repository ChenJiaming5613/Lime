# Project integration.
#
# lime_add_project() derives everything from ProjectSettings.json, so a project's CMakeLists is a
# single call with no arguments. Build time and run time therefore read the same file and the project
# name cannot drift between them.
#
# Project sources are compiled straight into the executable rather than into an intermediate static
# library. This is required, not stylistic: render passes and editor panels register themselves
# through static initializers, and a static library would let the linker drop the object files whose
# only purpose is that registration.

# lime_add_project([NAME <override>] [SOURCE_DIR <dir>] [SHADER_DIR <dir>])
function(lime_add_project)
	set(OneValueArgs NAME SOURCE_DIR SHADER_DIR)
	cmake_parse_arguments(LIME_PROJ "" "${OneValueArgs}" "" ${ARGN})

	set(ProjectDir "${CMAKE_CURRENT_SOURCE_DIR}")
	set(SettingsFile "${ProjectDir}/ProjectSettings.json")

	if(NOT LIME_PROJ_NAME)
		if(NOT EXISTS "${SettingsFile}")
			message(FATAL_ERROR "lime_add_project needs ${SettingsFile} or an explicit NAME")
		endif()
		file(READ "${SettingsFile}" SettingsJson)
		string(JSON LIME_PROJ_NAME ERROR_VARIABLE JsonError GET "${SettingsJson}" name)
		if(JsonError OR NOT LIME_PROJ_NAME)
			message(FATAL_ERROR "Cannot read \"name\" from ${SettingsFile}: ${JsonError}")
		endif()
	endif()

	if(NOT LIME_PROJ_SOURCE_DIR)
		set(LIME_PROJ_SOURCE_DIR "${ProjectDir}/Source")
	endif()
	if(NOT LIME_PROJ_SHADER_DIR)
		set(LIME_PROJ_SHADER_DIR "${ProjectDir}/Shaders")
	endif()

	# Globbing keeps a project's CMakeLists free of file lists. CONFIGURE_DEPENDS makes CMake rerun
	# when files are added or removed.
	file(GLOB_RECURSE ProjectSources CONFIGURE_DEPENDS
		"${LIME_PROJ_SOURCE_DIR}/*.cpp"
		"${LIME_PROJ_SOURCE_DIR}/*.h"
	)
	if(NOT ProjectSources)
		message(FATAL_ERROR "No sources found under ${LIME_PROJ_SOURCE_DIR}")
	endif()

	# The launch source is compiled per project rather than shared through a library target, so that
	# LIME_PROJECT_SOURCE_DIR is visible while compiling main. A shared target could not carry a value
	# that differs per project.
	add_executable(${LIME_PROJ_NAME} ${ProjectSources} "${LIME_LAUNCH_SOURCE}")
	target_include_directories(${LIME_PROJ_NAME} PRIVATE "${LIME_PROJ_SOURCE_DIR}")
	target_link_libraries(${LIME_PROJ_NAME} PRIVATE LimeCompilerOptions LimeRuntime)

	# Lets the runtime resolve and write the authored settings file in the source tree.
	target_compile_definitions(${LIME_PROJ_NAME} PRIVATE
		LIME_PROJECT_NAME="${LIME_PROJ_NAME}"
		LIME_PROJECT_SOURCE_DIR="${ProjectDir}"
	)

	set_target_properties(${LIME_PROJ_NAME} PROPERTIES
		FOLDER "Projects"
		VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${LIME_PROJ_NAME}>"
	)
	source_group(TREE "${ProjectDir}" FILES ${ProjectSources})

	# Project shaders build into Shaders/<ProjectName>, which the engine adds as a search root ahead
	# of its own, so a project can override a built-in shader.
	file(GLOB ProjectShaderConfigs "${LIME_PROJ_SHADER_DIR}/*.cfg")
	if(ProjectShaderConfigs)
		list(GET ProjectShaderConfigs 0 ProjectShaderConfig)
		lime_compile_shaders(
			TARGET ${LIME_PROJ_NAME}Shaders
			CONFIG "${ProjectShaderConfig}"
			OUTPUT_DIR "${LIME_SHADER_OUTPUT_DIR}/${LIME_PROJ_NAME}"
			INCLUDE_DIRS "${LIME_ENGINE_DIR}/Shaders/Include" "${LIME_PROJ_SHADER_DIR}"
			FOLDER "Projects"
		)
		add_dependencies(${LIME_PROJ_NAME} ${LIME_PROJ_NAME}Shaders)
	endif()

	add_dependencies(${LIME_PROJ_NAME} LimeShaders)

	# Settings and content are copied next to the executable so it runs without the source tree.
	if(EXISTS "${SettingsFile}")
		add_custom_command(TARGET ${LIME_PROJ_NAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${SettingsFile}" "$<TARGET_FILE_DIR:${LIME_PROJ_NAME}>/ProjectSettings.json"
			COMMENT "Copying ProjectSettings.json"
			VERBATIM
		)
	endif()

	add_custom_command(TARGET ${LIME_PROJ_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${LIME_ENGINE_DIR}/Content" "$<TARGET_FILE_DIR:${LIME_PROJ_NAME}>/Content"
		COMMENT "Copying engine content"
		VERBATIM
	)

	if(EXISTS "${ProjectDir}/Content")
		add_custom_command(TARGET ${LIME_PROJ_NAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${ProjectDir}/Content" "$<TARGET_FILE_DIR:${LIME_PROJ_NAME}>/Content/${LIME_PROJ_NAME}"
			COMMENT "Copying project content"
			VERBATIM
		)
	endif()

	message(STATUS "Project ${LIME_PROJ_NAME}: ${LIME_PROJ_SOURCE_DIR}")
endfunction()
