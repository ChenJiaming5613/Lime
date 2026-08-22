# Shader build integration based on NVIDIA ShaderMake.
#
# ShaderMake owns config parsing, permutation expansion, include dependency tracking and
# incremental builds. We only wire it up per enabled backend.
#
# Output naming, mirrored by Lime::MakeShaderMakeOutputName on the C++ side:
#   <outDir>/<platform>/<relative path without extension>[_<Entry> if Entry != "main"]<ext>
# where <ext> is .dxil or .spirv. Shaders declaring defines produce a blob file at that same
# path; shaders without defines produce raw bytecode.

# Forces ShaderMake options before its CMakeLists runs. Defaults would download DXC from
# GitHub at configure time and hard-require FXC from the Windows SDK, neither of which we want.
macro(lime_configure_shadermake_options)
	set(SHADERMAKE_FIND_COMPILERS ON CACHE BOOL "" FORCE)
	set(SHADERMAKE_FIND_DXC OFF CACHE BOOL "" FORCE)
	set(SHADERMAKE_FIND_DXC_VK ON CACHE BOOL "" FORCE)
	set(SHADERMAKE_FIND_FXC OFF CACHE BOOL "" FORCE)
	set(SHADERMAKE_FIND_SLANG OFF CACHE BOOL "" FORCE)
	set(SHADERMAKE_TOOL OFF CACHE BOOL "" FORCE)
endmacro()

# Validates that a DXC capable of both DXIL and SPIR-V code generation was located.
function(lime_validate_shader_compiler)
	if(NOT SHADERMAKE_DXC_PATH AND NOT SHADERMAKE_DXC_VK_PATH)
		message(FATAL_ERROR
			"No DXC found. Install the Vulkan SDK (it ships a DXC with SPIR-V code generation) "
			"and make sure the VULKAN_SDK environment variable is set. "
			"The DXC bundled with the Windows SDK cannot emit SPIR-V.")
	endif()
endfunction()

# lime_compile_shaders(TARGET <name> CONFIG <cfg file> OUTPUT_DIR <dir>
#                      [SOURCE_DIR <dir>] [INCLUDE_DIRS <dirs...>] [SHADER_MODEL <X_Y>])
function(lime_compile_shaders)
	set(OneValueArgs TARGET CONFIG OUTPUT_DIR SOURCE_DIR SHADER_MODEL)
	set(MultiValueArgs INCLUDE_DIRS)
	cmake_parse_arguments(LIME_SHADERS "" "${OneValueArgs}" "${MultiValueArgs}" ${ARGN})

	if(NOT LIME_SHADERS_TARGET OR NOT LIME_SHADERS_CONFIG OR NOT LIME_SHADERS_OUTPUT_DIR)
		message(FATAL_ERROR "lime_compile_shaders requires TARGET, CONFIG and OUTPUT_DIR")
	endif()
	if(NOT EXISTS "${LIME_SHADERS_CONFIG}")
		message(FATAL_ERROR "Shader config not found: ${LIME_SHADERS_CONFIG}")
	endif()
	if(NOT LIME_SHADERS_SHADER_MODEL)
		set(LIME_SHADERS_SHADER_MODEL "6_5")
	endif()
	if(NOT LIME_SHADERS_SOURCE_DIR)
		get_filename_component(LIME_SHADERS_SOURCE_DIR "${LIME_SHADERS_CONFIG}" DIRECTORY)
	endif()

	set(DxcPath "${SHADERMAKE_DXC_PATH}")
	if(NOT DxcPath)
		set(DxcPath "${SHADERMAKE_DXC_VK_PATH}")
	endif()

	set(IncludeArgs "")
	foreach(IncludeDir ${LIME_SHADERS_INCLUDE_DIRS})
		list(APPEND IncludeArgs -I "${IncludeDir}")
	endforeach()

	# Shared arguments. --matrixRowMajor (-Zpr) matches the row major FMatrix4x4 storage,
	# --binaryBlob packs permutations of one shader into a single file.
	set(CommonArgs
		--config "${LIME_SHADERS_CONFIG}"
		--sourceDir "${LIME_SHADERS_SOURCE_DIR}"
		--compiler "${DxcPath}"
		--shaderModel ${LIME_SHADERS_SHADER_MODEL}
		--binaryBlob
		--matrixRowMajor
		--WX
		--compactProgress
		$<IF:$<CONFIG:Debug>,--embedPDB,-O3>
		$<$<CONFIG:Debug>:-O0>
		${IncludeArgs}
	)

	set(ShaderCommands "")
	set(ShaderComments "")

	if(LIME_ENABLE_RHI_D3D12)
		list(APPEND ShaderCommands
			COMMAND ${SHADERMAKE_PATH} --platform DXIL --out "${LIME_SHADERS_OUTPUT_DIR}/DXIL" ${CommonArgs})
		list(APPEND ShaderComments "DXIL")
	endif()

	if(LIME_ENABLE_RHI_VULKAN)
		# Register shifts must match nvrhi::VulkanBindingOffsets passed at device creation.
		list(APPEND ShaderCommands
			COMMAND ${SHADERMAKE_PATH} --platform SPIRV --out "${LIME_SHADERS_OUTPUT_DIR}/SPIRV"
				--vulkanMemoryLayout dx
				--vulkanVersion 1.3
				--tRegShift ${LIME_VK_SHIFT_SRV}
				--sRegShift ${LIME_VK_SHIFT_SAMPLER}
				--bRegShift ${LIME_VK_SHIFT_CBV}
				--uRegShift ${LIME_VK_SHIFT_UAV}
				${CommonArgs})
		list(APPEND ShaderComments "SPIRV")
	endif()

	file(GLOB_RECURSE ShaderSources CONFIGURE_DEPENDS
		"${LIME_SHADERS_SOURCE_DIR}/*.hlsl"
		"${LIME_SHADERS_SOURCE_DIR}/*.hlsli"
	)

	list(JOIN ShaderComments ", " ShaderCommentText)
	add_custom_target(${LIME_SHADERS_TARGET} ALL
		${ShaderCommands}
		SOURCES ${LIME_SHADERS_CONFIG} ${ShaderSources}
		COMMENT "Compiling shaders (${ShaderCommentText})"
		VERBATIM
	)

	set_target_properties(${LIME_SHADERS_TARGET} PROPERTIES FOLDER "Engine")
	source_group(TREE "${LIME_SHADERS_SOURCE_DIR}" FILES ${LIME_SHADERS_CONFIG} ${ShaderSources})
endfunction()
