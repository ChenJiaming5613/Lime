# Project integration.
#
# lime_add_project() derives everything from ProjectSettings.json, so a project's CMakeLists is a
# single call with no arguments. Build time and run time therefore read the same file and the project
# name cannot drift between them.
#
# A project is self contained: everything it produces stays inside its own directory rather than in
# the shared build tree.
#
#   Projects/<Name>/Binaries/<Config>/     exe, Shaders, Content, ProjectSettings.json, Saved
#   Projects/<Name>/Intermediate/          object files, compiler pdb, import libraries
#
# Keeping the runtime directory per project matters because the engine resolves Shaders, Content,
# Saved and ProjectSettings.json relative to the executable: a shared directory would let one
# project's settings file overwrite another's and send it looking for the wrong shaders.
#
# The object files land under Intermediate because Projects/CMakeLists.txt gives each project a build
# directory there; only the output directories are set here.
#
# Project sources are compiled straight into the executable rather than into an intermediate static
# library. This is required, not stylistic: render passes and editor panels register themselves
# through static initializers, and a static library would let the linker drop the object files whose
# only purpose is that registration.
#
# A project may also have no sources at all. The engine owns main and provides the built-in passes, so
# a project that only configures existing engine features needs nothing but ProjectSettings.json and the
# one line CMakeLists.txt that calls this function. LIME_LAUNCH_SOURCE is always compiled in, which
# gives the executable its one required translation unit.

# lime_add_project([NAME <override>] [SOURCE_DIR <dir>] [SHADER_DIR <dir>])
function(lime_add_project)
	set(OneValueArgs NAME SOURCE_DIR SHADER_DIR)
	cmake_parse_arguments(LIME_PROJ "" "${OneValueArgs}" "" ${ARGN})

	set(ProjectDir "${CMAKE_CURRENT_SOURCE_DIR}")
	set(SettingsFile "${ProjectDir}/ProjectSettings.json")
	set(EditorSettingsFile "${ProjectDir}/EditorSettings.json")

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

	# Everything this project produces at build time lands here, and this is also its working
	# directory at run time. It sits inside the project rather than in the shared build tree, so a
	# project directory holds its own sources and its own binaries.
	set(ProjectBinariesDir "${ProjectDir}/Binaries")
	set(ProjectIntermediateDir "${ProjectDir}/Intermediate")
	set(ProjectOutputDir "${ProjectBinariesDir}/$<CONFIG>")

	# Globbing keeps a project's CMakeLists free of file lists. CONFIGURE_DEPENDS makes CMake rerun
	# when files are added or removed. An empty result is valid: see the note at the top of this file.
	set(ProjectSources "")
	if(EXISTS "${LIME_PROJ_SOURCE_DIR}")
		file(GLOB_RECURSE ProjectSources CONFIGURE_DEPENDS
			"${LIME_PROJ_SOURCE_DIR}/*.cpp"
			"${LIME_PROJ_SOURCE_DIR}/*.h"
		)
	endif()

	# The launch source is compiled per project rather than shared through a library target, so that
	# LIME_PROJECT_SOURCE_DIR is visible while compiling main. A shared target could not carry a value
	# that differs per project.
	add_executable(${LIME_PROJ_NAME} ${ProjectSources} "${LIME_LAUNCH_SOURCE}")
	if(ProjectSources)
		target_include_directories(${LIME_PROJ_NAME} PRIVATE "${LIME_PROJ_SOURCE_DIR}")
	endif()
	target_link_libraries(${LIME_PROJ_NAME} PRIVATE LimeCompilerOptions LimeRuntime)

	# Lets the runtime resolve and write the authored settings file in the source tree.
	target_compile_definitions(${LIME_PROJ_NAME} PRIVATE
		LIME_PROJECT_NAME="${LIME_PROJ_NAME}"
		LIME_PROJECT_SOURCE_DIR="${ProjectDir}"
	)

	# Overrides the global defaults from the root CMakeLists so nothing this target produces ends up
	# in the shared build tree: the executable and its side files under Binaries, the import library
	# and the compiler pdb under Intermediate.
	set_target_properties(${LIME_PROJ_NAME} PROPERTIES
		FOLDER "Projects"
		RUNTIME_OUTPUT_DIRECTORY "${ProjectOutputDir}"
		LIBRARY_OUTPUT_DIRECTORY "${ProjectOutputDir}"
		ARCHIVE_OUTPUT_DIRECTORY "${ProjectIntermediateDir}/Lib/$<CONFIG>"
		PDB_OUTPUT_DIRECTORY "${ProjectOutputDir}"
		COMPILE_PDB_OUTPUT_DIRECTORY "${ProjectIntermediateDir}/Lib/$<CONFIG>"
		VS_DEBUGGER_WORKING_DIRECTORY "${ProjectOutputDir}"
	)
	if(ProjectSources)
		source_group(TREE "${ProjectDir}" FILES ${ProjectSources})
	endif()

	# Project shaders build straight into Shaders/<ProjectName> inside this project's directory. The
	# engine registers that path as a search root ahead of its own, so a project can override a
	# built-in shader by using the same relative path.
	file(GLOB ProjectShaderConfigs "${LIME_PROJ_SHADER_DIR}/*.cfg")
	if(ProjectShaderConfigs)
		list(GET ProjectShaderConfigs 0 ProjectShaderConfig)
		lime_compile_shaders(
			TARGET ${LIME_PROJ_NAME}Shaders
			CONFIG "${ProjectShaderConfig}"
			OUTPUT_DIR "${ProjectOutputDir}/Shaders/${LIME_PROJ_NAME}"
			INCLUDE_DIRS "${LIME_ENGINE_DIR}/Shaders/Include" "${LIME_PROJ_SHADER_DIR}"
			FOLDER "Projects"
		)
		add_dependencies(${LIME_PROJ_NAME} ${LIME_PROJ_NAME}Shaders)
	endif()

	add_dependencies(${LIME_PROJ_NAME} LimeShaders)

	# Engine shaders and content are built once into a shared staging area and copied in here, so the
	# cost of compiling them is paid once no matter how many projects exist. The destination repeats
	# the Engine leaf because the staging path already ends in it, and copy_directory copies the
	# contents rather than the directory itself.
	add_custom_command(TARGET ${LIME_PROJ_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${LIME_SHADER_OUTPUT_DIR}" "${ProjectOutputDir}/Shaders/Engine"
		COMMENT "Copying engine shaders"
		VERBATIM
	)

	add_custom_command(TARGET ${LIME_PROJ_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${LIME_ENGINE_DIR}/Content" "${ProjectOutputDir}/Content"
		COMMENT "Copying engine content"
		VERBATIM
	)

	# Settings and content are copied next to the executable so it runs without the source tree.
	if(EXISTS "${SettingsFile}")
		add_custom_command(TARGET ${LIME_PROJ_NAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${SettingsFile}" "${ProjectOutputDir}/ProjectSettings.json"
			COMMENT "Copying ProjectSettings.json"
			VERBATIM
		)
	endif()

	# Editor appearance. Optional: the engine falls back to built-in defaults, so a project only
	# ships this file when it wants something else.
	if(EXISTS "${EditorSettingsFile}")
		add_custom_command(TARGET ${LIME_PROJ_NAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${EditorSettingsFile}" "${ProjectOutputDir}/EditorSettings.json"
			COMMENT "Copying EditorSettings.json"
			VERBATIM
		)
	endif()

	if(EXISTS "${ProjectDir}/Content")
		add_custom_command(TARGET ${LIME_PROJ_NAME} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${ProjectDir}/Content" "${ProjectOutputDir}/Content/${LIME_PROJ_NAME}"
			COMMENT "Copying project content"
			VERBATIM
		)
	endif()

	# Says whether the project brought code of its own, which is the quickest way to see that a
	# configuration only project was picked up as intended.
	if(ProjectSources)
		list(LENGTH ProjectSources ProjectSourceCount)
		set(SourceSummary "${ProjectSourceCount} source(s)")
	else()
		set(SourceSummary "no sources, engine features only")
	endif()
	message(STATUS "Project ${LIME_PROJ_NAME}: ${SourceSummary} -> ${ProjectBinariesDir}/<Config>")
endfunction()
