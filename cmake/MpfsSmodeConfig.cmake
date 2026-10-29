# SPDX-License-Identifier: MIT
#
# find_package(MpfsSmode) for an application built against the installed
# libraries (cmake --install, make install, or make's staged build/dist):
#
#   MpfsSmode::mpfs_smode          what an application links: both libraries
#                                  whole-archive, the linker script, the flags
#   MpfsSmode::mpfs_smode_boot     libmpfs_smode_boot.a: M-mode entry, the SBI
#                                  shim, the M->S handoff (boot/)
#   MpfsSmode::freertos_smode      libfreertos_smode.a: FreeRTOS kernel + the
#                                  S-mode port, with its FreeRTOSConfig.h
#   MpfsSmode::mpfs_smode_headers  include paths and compile flags only
#
# MARCH and LIBC_SPECS are LIBRARY-FIXED: an application may leave them unset
# or repeat the libraries' values, anything else is an error. BUILD_TYPE
# defaults to the libraries'. MPFS_SMODE_LINKER_SCRIPT replaces the installed
# linker script.

get_filename_component(MpfsSmode_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${CMAKE_CURRENT_LIST_DIR}/build_config.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/MpfsSmodeFlags.cmake")

foreach(_v MARCH LIBC_SPECS)
    if(DEFINED ${_v} AND NOT "${${_v}}" STREQUAL "${MPFS_SMODE_BUILT_${_v}}")
        message(FATAL_ERROR
            "${_v} is LIBRARY-FIXED: the libraries in ${MpfsSmode_PREFIX} were built with "
            "${_v}=${MPFS_SMODE_BUILT_${_v}}, not ${${_v}}. Rebuild the libraries to change it.")
    endif()
endforeach()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "${MPFS_SMODE_BUILT_BUILD_TYPE}" CACHE STRING "Debug or Release" FORCE)
endif()
if(NOT MPFS_SMODE_LINKER_SCRIPT)
    set(MPFS_SMODE_LINKER_SCRIPT "${MpfsSmode_PREFIX}/lib/ldscripts/linker.ld")
endif()
mpfs_smode_whole_archive_support()

if(NOT TARGET MpfsSmode::mpfs_smode)
    mpfs_smode_flags("${MPFS_SMODE_BUILT_MARCH}" "${MPFS_SMODE_BUILT_LIBC_SPECS}"
                     "${MPFS_SMODE_BUILT_LIBC_LINK_SPECS}" _mpfs_cflags _mpfs_ldflags)

    foreach(_lib mpfs_smode_boot freertos_smode)
        add_library(MpfsSmode::${_lib} STATIC IMPORTED)
        set_target_properties(MpfsSmode::${_lib} PROPERTIES
            IMPORTED_LOCATION "${MpfsSmode_PREFIX}/lib/lib${_lib}.a")
    endforeach()

    add_library(MpfsSmode::mpfs_smode_headers INTERFACE IMPORTED)
    set_target_properties(MpfsSmode::mpfs_smode_headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
            "${MpfsSmode_PREFIX}/include;${MpfsSmode_PREFIX}/include/freertos"
        INTERFACE_COMPILE_OPTIONS "${_mpfs_cflags}")

    # Whole-archive: nothing in an application refers to the boot code (the
    # bootloader jumps to _start), so a plain archive link would drop it.
    add_library(MpfsSmode::mpfs_smode INTERFACE IMPORTED)
    set_target_properties(MpfsSmode::mpfs_smode PROPERTIES
        INTERFACE_LINK_OPTIONS "${_mpfs_ldflags};-T${MPFS_SMODE_LINKER_SCRIPT}"
        INTERFACE_LINK_LIBRARIES
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,MpfsSmode::mpfs_smode_boot,MpfsSmode::freertos_smode>;MpfsSmode::mpfs_smode_headers")
    set_property(TARGET MpfsSmode::mpfs_smode APPEND PROPERTY
        INTERFACE_LINK_DEPENDS "${MPFS_SMODE_LINKER_SCRIPT}")

    unset(_mpfs_cflags)
    unset(_mpfs_ldflags)
endif()

set(MpfsSmode_FOUND TRUE)
