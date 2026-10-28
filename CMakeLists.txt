# SPDX-License-Identifier: MIT
#
# Project orchestrator (CMake): the counterpart of the root Makefile. No build
# rules of its own: it adds the library (src/CMakeLists.txt) and the
# application (examples/freertos-demo/CMakeLists.txt), which links the
# library's MpfsSmode:: targets.
#
#   cmake -B build-cmake [-DCMAKE_BUILD_TYPE=Release]
#   cmake --build build-cmake
#   cmake --install build-cmake --prefix <dir>     # the library's install tree
#   ctest --test-dir build-cmake --output-on-failure  # Renode tests (below)
#
# SoftConsole builds with the root Makefile; CMake is an alternative and must
# be installed separately (on Windows also choose -G Ninja, or
# -G "Unix Makefiles" with SoftConsole's build_tools\bin first on PATH).

cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/src/cmake/riscv64-unknown-elf.cmake")
endif()

project(mpfs_freertos_smode LANGUAGES C ASM)

# The demo's FreeRTOSConfig.h is compiled into the library, as the root
# Makefile does (relative to src/).
set(FREERTOS_CONFIG_DIR "../examples/freertos-demo" CACHE PATH
    "Directory with the FreeRTOSConfig.h compiled into libfreertos_smode")
set(FREERTOS_IP_CONFIG_DIR "../examples/freertos-demo" CACHE PATH
    "Directory with the FreeRTOSIPConfig.h compiled into libfreertos_tcp")

add_subdirectory(src)
add_subdirectory(examples/freertos-demo)

#-------------------------------------------------------------------------------
# Renode tests (CTest), the counterparts of 'make sim', 'make sim-host' and
# 'make robot'. They use the same scripts and expected-line files in renode/,
# run Renode in the project root, and need no shell (renode/cmake/*.cmake),
# so they also work from a plain Windows prompt.
#
#   cmake --preset debug             (or -B <dir> -DRENODE=... -DPYTHON=...)
#   cmake --build --preset debug
#   ctest --preset debug             # -R sim, -L host, ...
#
# Every test is always registered, so IDEs (VS Code's Testing view) list
# them all. One whose tool is not configured reports "Skipped" and names the
# missing setting. Tools: RENODE (found automatically in SoftConsole's usual
# install folders), PYTHON (found on PATH), RENODE_TEST. Machine-specific
# paths belong in CMakeUserPresets.json (see CMakeUserPresets.json.example).
#
# The Renode tests need the ELF: the build_demo test builds it first (a CTest
# fixture), so running a single test from an IDE never uses a stale image.
# Each run's outputs go to <build>/sim, <build>/sim_host and <build>/robot.
# The runs are also build targets: cmake --build <dir> --target sim
# (sim_host, robot).
#-------------------------------------------------------------------------------
enable_testing()

# Renode: given, or found next to a SoftConsole install (Windows:
# C:/Microchip/SoftConsole-*/renode[/bin]/Renode.exe), or on PATH.
if(NOT RENODE)
    file(GLOB _softconsole_renode_dirs LIST_DIRECTORIES true
        "C:/Microchip/SoftConsole-*/renode" "C:/Microchip/SoftConsole-*/renode/bin"
        "/opt/microchip/SoftConsole-*/renode" "/opt/microchip/SoftConsole-*/renode/bin"
        "$ENV{HOME}/Microchip/SoftConsole-*/renode" "$ENV{HOME}/Microchip/SoftConsole-*/renode/bin")
    find_program(_renode_found NAMES Renode renode HINTS ${_softconsole_renode_dirs} NO_CACHE)
    if(_renode_found)
        set(_renode_default "${_renode_found}")
    else()
        set(_renode_default "")
    endif()
endif()
set(RENODE "${_renode_default}" CACHE FILEPATH "Renode executable, for the sim / sim_host tests")
set(RENODE_FLAGS "--disable-xwt;--console" CACHE STRING
    "Renode options for headless runs (on Windows try --disable-xwt alone if --console is rejected)")
set(RENODE_TEST "" CACHE FILEPATH
    "Renode's renode-test script, for the robot test (Python with robotframework==4.0.1 must be on PATH)")
set(SIM_TIME "3" CACHE STRING "Emulated seconds after the first one")
if(NOT DEFINED PYTHON)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        set(_python_default "${Python3_EXECUTABLE}")
    else()
        set(_python_default "")
    endif()
    set(PYTHON "${_python_default}" CACHE FILEPATH "Python 3, for the sim_host test (renode/host_bridge.py)")
endif()

set(_renode_dir "${CMAKE_CURRENT_SOURCE_DIR}/renode")
set(_elf "$<TARGET_FILE:freertos-demo>")

# The checker itself, on fixed logs: runs everywhere, even without Renode.
add_test(NAME check_uart_selftest
    COMMAND ${CMAKE_COMMAND} -DUART=${_renode_dir}/tests/data/uart-good.txt
            -DEXPECTED=${_renode_dir}/tests/data/expected.txt
            -P ${_renode_dir}/cmake/CheckUart.cmake)
add_test(NAME check_uart_selftest_fail
    COMMAND ${CMAKE_COMMAND} -DUART=${_renode_dir}/tests/data/uart-fail.txt
            -DEXPECTED=${_renode_dir}/tests/data/expected.txt
            -P ${_renode_dir}/cmake/CheckUart.cmake)
set_tests_properties(check_uart_selftest_fail PROPERTIES WILL_FAIL TRUE)
set_tests_properties(check_uart_selftest check_uart_selftest_fail PROPERTIES LABELS "check")

# Builds the demo before any Renode test that needs it (fixture "demo_elf").
get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_multi_config)
    set(_config_arg --config $<CONFIG>)
else()
    set(_config_arg "")
endif()
add_test(NAME build_demo
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target freertos-demo ${_config_arg})
set_tests_properties(build_demo PROPERTIES FIXTURES_SETUP demo_elf LABELS "build" TIMEOUT 900)

# A test whose tool is missing: registered anyway, reports "Skipped".
function(mpfs_skipped_test name reason labels)
    add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -E echo "SKIPPED: ${reason}")
    set_tests_properties(${name} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED:" LABELS "${labels}")
endfunction()

set(_renode_test_props
    TIMEOUT 900
    RESOURCE_LOCK renode            # fixed UDP ports, CPU-heavy
    FIXTURES_REQUIRED demo_elf)

# One Renode run + checks (renode/cmake/RunSim.cmake), as a test and a target.
function(mpfs_sim_run name script labels)
    string(REPLACE ";" "|" _flags "${RENODE_FLAGS}")
    set(_cmd ${CMAKE_COMMAND}
        -DRENODE=${RENODE}
        -DRENODE_FLAGS_PIPE=${_flags}
        -DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}
        -DOUT=${CMAKE_BINARY_DIR}/${name}
        -DRUNTIME=${SIM_TIME}
        -DSCRIPT=${script}
        -DELF=${_elf}
        ${ARGN}
        -P ${_renode_dir}/cmake/RunSim.cmake)
    add_test(NAME ${name} COMMAND ${_cmd} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(${name} PROPERTIES ${_renode_test_props} LABELS "${labels}")
    add_custom_target(${name} COMMAND ${_cmd}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL
        VERBATIM)
    add_dependencies(${name} freertos-demo)
endfunction()

set(_need_renode "set RENODE to the Renode executable (CMakeUserPresets.json, or -DRENODE=...)")
if(RENODE)
    mpfs_sim_run(sim run-demo "renode;network")
else()
    mpfs_skipped_test(sim "${_need_renode}" "renode;network")
endif()
if(RENODE AND PYTHON)
    mpfs_sim_run(sim_host run-host "renode;network;host" -DPYTHON=${PYTHON})
elseif(NOT RENODE)
    mpfs_skipped_test(sim_host "${_need_renode}" "renode;network;host")
else()
    mpfs_skipped_test(sim_host "set PYTHON to a Python 3 interpreter" "renode;network;host")
endif()

if(RENODE_TEST)
    set(_robot_cmd ${RENODE_TEST} ${_renode_dir}/tests/mpfs-demo.robot
        -r ${CMAKE_BINARY_DIR}/robot
        --variable ELF:${_elf}
        --variable OUT:${CMAKE_BINARY_DIR}/robot)
    add_test(NAME robot COMMAND ${_robot_cmd} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(robot PROPERTIES ${_renode_test_props} LABELS "renode;robot")
    add_custom_target(robot COMMAND ${_robot_cmd}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL
        VERBATIM)
    add_dependencies(robot freertos-demo)
else()
    mpfs_skipped_test(robot
        "set RENODE_TEST to Renode's renode-test script (CMakeUserPresets.json, or -DRENODE_TEST=...)"
        "renode;robot")
endif()

message(STATUS "Renode tests: RENODE=${RENODE}")
message(STATUS "Renode tests: PYTHON=${PYTHON}")
message(STATUS "Renode tests: RENODE_TEST=${RENODE_TEST}")
