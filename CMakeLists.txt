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
#   cmake -B build-cmake -DRENODE=<Renode executable> [-DPYTHON=<python3>]
#         [-DRENODE_TEST=<renode-test>]
#   cmake --build build-cmake
#   ctest --test-dir build-cmake --output-on-failure    # -R sim, -L host, ...
#
# A test is only registered when its tool is set. Each run's outputs go to
# <build>/sim, <build>/sim-host and <build>/robot. The same runs are also
# build targets: cmake --build build-cmake --target sim (sim_host, robot).
#-------------------------------------------------------------------------------
enable_testing()

set(RENODE "" CACHE FILEPATH "Renode executable, for the sim / sim_host tests")
set(RENODE_FLAGS "--disable-xwt;--console" CACHE STRING
    "Renode options for headless runs (on Windows try --disable-xwt alone if --console is rejected)")
set(RENODE_TEST "" CACHE FILEPATH "Renode's renode-test script, for the robot test")
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

# One Renode run + checks (renode/cmake/RunSim.cmake), as a test and a target.
function(mpfs_sim_run name script)
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
    set_tests_properties(${name} PROPERTIES
        TIMEOUT 900
        RESOURCE_LOCK renode            # fixed UDP ports, CPU-heavy
        LABELS "renode;network")
    add_custom_target(${name} COMMAND ${_cmd}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL
        VERBATIM)
    add_dependencies(${name} freertos-demo)
endfunction()

if(RENODE)
    mpfs_sim_run(sim run-demo)
    if(PYTHON)
        mpfs_sim_run(sim_host run-host -DPYTHON=${PYTHON})
        set_property(TEST sim_host APPEND PROPERTY LABELS host)
    else()
        message(STATUS "Renode tests: no Python 3 found, so no sim_host test (set -DPYTHON=...)")
    endif()
else()
    message(STATUS "Renode tests: set -DRENODE=<Renode executable> to add the sim / sim_host tests")
endif()

if(RENODE_TEST)
    set(_robot_cmd ${RENODE_TEST} ${_renode_dir}/tests/mpfs-demo.robot
        -r ${CMAKE_BINARY_DIR}/robot
        --variable ELF:${_elf}
        --variable OUT:${CMAKE_BINARY_DIR}/robot)
    add_test(NAME robot COMMAND ${_robot_cmd} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(robot PROPERTIES TIMEOUT 900 RESOURCE_LOCK renode LABELS "renode;robot")
    add_custom_target(robot COMMAND ${_robot_cmd}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL
        VERBATIM)
    add_dependencies(robot freertos-demo)
else()
    message(STATUS "Renode tests: set -DRENODE_TEST=<Renode's renode-test> to add the robot test")
endif()

