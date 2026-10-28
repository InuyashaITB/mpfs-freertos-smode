# SPDX-License-Identifier: MIT
#
# Checks the MMUART1 output of a headless Renode run: the CMake twin of
# renode/check-uart.sh, for CTest (no shell needed, so it also runs on
# Windows without busybox).
#
#   cmake -DUART=<uart log> -DRENODE_LOG=<renode log>
#         "-DEXPECTED=<expected lines>|<more expected lines>" -P CheckUart.cmake
#
# EXPECTED: files separated by '|' or ';' ('|' survives being passed through
# execute_process() / add_test(), where ';' splits the argument).
#
# Fails if the log is missing or empty, if any line reports a failure
# ("FAIL", "no response", or a line starting "*** ": a fault report from the
# libraries), or if a line from the expected files (fixed strings matched
# anywhere in a line; '#' comment lines and blank lines skipped) does not
# appear. The renode log is only shown when the UART log is empty.
#
# Also usable from another script: include() it after setting the same
# variables.

string(REPLACE "|" ";" EXPECTED "${EXPECTED}")
if(NOT UART OR NOT EXPECTED)
    message(FATAL_ERROR "usage: cmake -DUART=<uart log> -DRENODE_LOG=<renode log> \"-DEXPECTED=<file>;...\" -P CheckUart.cmake")
endif()

set(_size 0)
if(EXISTS "${UART}")
    file(SIZE "${UART}" _size)
endif()
if(_size EQUAL 0)
    set(_msg "check-uart: FAIL: no UART output in ${UART}")
    if(RENODE_LOG AND EXISTS "${RENODE_LOG}")
        file(STRINGS "${RENODE_LOG}" _log)     # only printed: ';' splits are harmless
        list(LENGTH _log _n)
        if(_n GREATER 15)
            math(EXPR _first "${_n} - 15")
            list(SUBLIST _log ${_first} 15 _log)
        endif()
        list(JOIN _log "\n" _tail)
        string(APPEND _msg "\ncheck-uart: last lines of ${RENODE_LOG}:\n${_tail}")
    endif()
    message(FATAL_ERROR "${_msg}")
endif()

# Lines of a text file as a list. Semicolons would split CMake list items,
# so they are swapped for a placeholder while the text is a list (restore
# with _restore before printing or comparing).
set(_SEMI "<semicolon>")
function(_read_lines file out)
    file(READ "${file}" _text)
    string(REPLACE "\r" "" _text "${_text}")
    string(REPLACE ";" "${_SEMI}" _text "${_text}")
    string(REPLACE "\n" ";" _text "${_text}")
    set(${out} "${_text}" PARENT_SCOPE)
endfunction()
macro(_restore var)
    string(REPLACE "${_SEMI}" ";" ${var} "${${var}}")
endmacro()

# The whole log as one string, without CR, for fixed-string search.
file(READ "${UART}" _uart)
string(REPLACE "\r" "" _uart "${_uart}")

set(_errors "")

# Failure reports, line by line.
_read_lines("${UART}" _lines)
set(_lineno 0)
foreach(_line IN LISTS _lines)
    math(EXPR _lineno "${_lineno} + 1")
    _restore(_line)
    string(FIND "${_line}" "FAIL" _f)
    string(FIND "${_line}" "no response" _r)
    string(FIND "${_line}" "*** " _s)
    if(NOT _f EQUAL -1 OR NOT _r EQUAL -1 OR _s EQUAL 0)
        string(APPEND _errors "check-uart: FAIL: failure reported in ${UART}:${_lineno}: ${_line}\n")
    endif()
endforeach()

set(_found 0)
set(_missing 0)
foreach(_file IN LISTS EXPECTED)
    if(NOT EXISTS "${_file}")
        message(FATAL_ERROR "check-uart: expected-lines file ${_file} does not exist")
    endif()
    _read_lines("${_file}" _wanted)
    foreach(_want IN LISTS _wanted)
        _restore(_want)
        if(_want STREQUAL "" OR _want MATCHES "^#")
            continue()
        endif()
        string(FIND "${_uart}" "${_want}" _pos)
        if(_pos EQUAL -1)
            string(APPEND _errors "check-uart: FAIL: missing in ${UART}: ${_want}\n")
            math(EXPR _missing "${_missing} + 1")
        else()
            math(EXPR _found "${_found} + 1")
        endif()
    endforeach()
endforeach()

if(NOT _errors STREQUAL "")
    message(FATAL_ERROR "${_errors}check-uart: ${_found} expected lines found, ${_missing} missing")
endif()
message(STATUS "check-uart: PASS (${_found} expected lines found in ${UART})")
