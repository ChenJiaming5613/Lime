# Python automation test suite, registered with ctest.
#
# Registered as a small number of coarse tests rather than one per pytest case: each engine launch
# costs seconds, and ctest would otherwise pay that cost for every test it runs individually. pytest
# does its own reporting inside each.
#
# Skipped silently when Python or pytest is missing, so a checkout without them still builds and runs
# the C++ tests.

if(NOT LIME_BUILD_AUTOMATION)
	return()
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_FOUND)
	message(STATUS "Automation tests: skipped (no Python 3 interpreter)")
	return()
endif()

# pytest is checked at configure time so a missing dependency is a clear status line rather than a
# confusing test failure later.
execute_process(
	COMMAND "${Python3_EXECUTABLE}" -c "import pytest"
	RESULT_VARIABLE PytestMissing
	OUTPUT_QUIET
	ERROR_QUIET
)

if(NOT PytestMissing EQUAL 0)
	message(STATUS "Automation tests: skipped (pytest not installed; pip install -r Automation/requirements.txt)")
	return()
endif()

set(AutomationTestDir "${CMAKE_CURRENT_SOURCE_DIR}")

# One test per backend, so a backend specific regression is attributable and the two can run in
# parallel. The suite launches its own engines, hence the generous timeout.
foreach(Backend IN ITEMS d3d12 vulkan)
	if(Backend STREQUAL "d3d12" AND NOT LIME_ENABLE_RHI_D3D12)
		continue()
	endif()
	if(Backend STREQUAL "vulkan" AND NOT LIME_ENABLE_RHI_VULKAN)
		continue()
	endif()

	add_test(
		NAME Automation.${Backend}
		COMMAND "${Python3_EXECUTABLE}" -m pytest -q
			--backend ${Backend}
			--config $<CONFIG>
		WORKING_DIRECTORY "${AutomationTestDir}"
	)

	set_tests_properties(Automation.${Backend} PROPERTIES
		TIMEOUT 600
		LABELS "automation"
		# The engines bind loopback ports and share one build directory, so the backends are run one at
		# a time rather than concurrently.
		RESOURCE_LOCK "LimeAutomationEngine"
		# LIME_BUILD_DIR points the suite at the binaries this configure step produced, which is more
		# reliable than deriving a preset name from the path.
		ENVIRONMENT "LIME_ROOT=${CMAKE_SOURCE_DIR};LIME_BUILD_DIR=${CMAKE_BINARY_DIR}"
	)
endforeach()

message(STATUS "Automation tests: registered (pytest)")
