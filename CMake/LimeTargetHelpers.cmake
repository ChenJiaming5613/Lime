# Helpers that keep module and executable declarations uniform across the engine.

# lime_add_module(NAME <target> SOURCES <files...> [PUBLIC_DEPS <targets...>]
#                 [PRIVATE_DEPS <targets...>] [DEFINES <defs...>] [FOLDER <ide folder>])
function(lime_add_module)
	set(OneValueArgs NAME FOLDER)
	set(MultiValueArgs SOURCES PUBLIC_DEPS PRIVATE_DEPS DEFINES)
	cmake_parse_arguments(LIME_MODULE "" "${OneValueArgs}" "${MultiValueArgs}" ${ARGN})

	if(NOT LIME_MODULE_NAME)
		message(FATAL_ERROR "lime_add_module requires NAME")
	endif()
	if(NOT LIME_MODULE_SOURCES)
		message(FATAL_ERROR "lime_add_module(${LIME_MODULE_NAME}) requires SOURCES")
	endif()

	add_library(${LIME_MODULE_NAME} STATIC ${LIME_MODULE_SOURCES})

	target_include_directories(${LIME_MODULE_NAME} PUBLIC ${LIME_ENGINE_INCLUDE_DIRS})
	target_link_libraries(${LIME_MODULE_NAME}
		PUBLIC LimeCompilerOptions ${LIME_MODULE_PUBLIC_DEPS}
		PRIVATE ${LIME_MODULE_PRIVATE_DEPS}
	)

	if(LIME_MODULE_DEFINES)
		target_compile_definitions(${LIME_MODULE_NAME} PUBLIC ${LIME_MODULE_DEFINES})
	endif()

	if(NOT LIME_MODULE_FOLDER)
		set(LIME_MODULE_FOLDER "Engine")
	endif()
	set_target_properties(${LIME_MODULE_NAME} PROPERTIES FOLDER "${LIME_MODULE_FOLDER}")

	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${LIME_MODULE_SOURCES})
endfunction()

# lime_add_executable(NAME <target> SOURCES <files...> [DEPS <targets...>] [FOLDER <ide folder>])
function(lime_add_executable)
	set(OneValueArgs NAME FOLDER)
	set(MultiValueArgs SOURCES DEPS)
	cmake_parse_arguments(LIME_EXE "" "${OneValueArgs}" "${MultiValueArgs}" ${ARGN})

	if(NOT LIME_EXE_NAME)
		message(FATAL_ERROR "lime_add_executable requires NAME")
	endif()

	add_executable(${LIME_EXE_NAME} ${LIME_EXE_SOURCES})
	target_include_directories(${LIME_EXE_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
	target_link_libraries(${LIME_EXE_NAME} PRIVATE LimeCompilerOptions ${LIME_EXE_DEPS})

	if(NOT LIME_EXE_FOLDER)
		set(LIME_EXE_FOLDER "Projects")
	endif()
	set_target_properties(${LIME_EXE_NAME} PROPERTIES
		FOLDER "${LIME_EXE_FOLDER}"
		VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${LIME_EXE_NAME}>"
	)

	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${LIME_EXE_SOURCES})
endfunction()

# Marks third party include directories as SYSTEM so their warnings stay out of our build log.
function(lime_mark_system_includes)
	foreach(TargetName ${ARGN})
		if(TARGET ${TargetName})
			get_target_property(TargetType ${TargetName} TYPE)
			if(TargetType STREQUAL "INTERFACE_LIBRARY")
				get_target_property(IncludeDirs ${TargetName} INTERFACE_INCLUDE_DIRECTORIES)
			else()
				get_target_property(IncludeDirs ${TargetName} INTERFACE_INCLUDE_DIRECTORIES)
			endif()
			if(IncludeDirs)
				set_target_properties(${TargetName} PROPERTIES
					INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${IncludeDirs}")
			endif()
		endif()
	endforeach()
endfunction()

# Groups third party targets under a single IDE folder.
function(lime_set_third_party_folder)
	foreach(TargetName ${ARGN})
		if(TARGET ${TargetName})
			get_target_property(TargetType ${TargetName} TYPE)
			if(NOT TargetType STREQUAL "INTERFACE_LIBRARY")
				set_target_properties(${TargetName} PROPERTIES FOLDER "ThirdParty")
			endif()
		endif()
	endforeach()
endfunction()
