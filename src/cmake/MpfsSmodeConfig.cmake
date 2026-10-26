# SPDX-License-Identifier: MIT
#
# find_package(MpfsSmode) for an installed copy of the libraries, whether it
# was built with make ('make install PREFIX=...') or CMake ('cmake --install').
#
#   cmake -S my_app -B build \
#         -DCMAKE_TOOLCHAIN_FILE=<prefix>/lib/cmake/MpfsSmode/riscv64-unknown-elf.cmake \
#         -DCMAKE_PREFIX_PATH=<prefix>
#
#   find_package(MpfsSmode REQUIRED)
#   target_link_libraries(my_app PRIVATE MpfsSmode::mpfs_smode MpfsSmode::mpfs_drivers)
#
# Targets:
#   MpfsSmode::mpfs_smode    both libraries (whole-archive), include paths,
#                            compile options, link options + linker script
#   MpfsSmode::mpfs_drivers  the MSS drivers the libraries were built with
#
# Variables:
#   MPFS_SMODE_LINKER_SCRIPT  set before find_package() to link with your own
#                             copy of the linker script
#   MpfsSmode_PREFIX          the install prefix found
#
# Compile/link options come from build_config.cmake: the toolchain settings
# the libraries were built with.

get_filename_component(MpfsSmode_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

include("${CMAKE_CURRENT_LIST_DIR}/build_config.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/MpfsSmodeFlags.cmake")

# Default the application's build type to the libraries' (as mpfs_smode.mk
# does for make applications).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "${MPFS_SMODE_BUILD_TYPE}" CACHE STRING "Debug or Release" FORCE)
endif()

if(NOT MPFS_SMODE_LINKER_SCRIPT)
    set(MPFS_SMODE_LINKER_SCRIPT "${MpfsSmode_PREFIX}/lib/ldscripts/mpfs-lim-sv39.ld")
endif()

# $<LINK_LIBRARY:WHOLE_ARCHIVE,...> on a Generic (bare-metal) system, in case
# the application uses another toolchain file.
if(NOT CMAKE_LINK_LIBRARY_USING_WHOLE_ARCHIVE_SUPPORTED)
    set(CMAKE_LINK_LIBRARY_USING_WHOLE_ARCHIVE
        "LINKER:--whole-archive" "<LINK_ITEM>" "LINKER:--no-whole-archive")
    set(CMAKE_LINK_LIBRARY_USING_WHOLE_ARCHIVE_SUPPORTED TRUE)
endif()

if(NOT TARGET MpfsSmode::mpfs_smode)
    mpfs_smode_flags("${MPFS_SMODE_MARCH}" "${MPFS_SMODE_LIBC_SPECS}"
                     "${MPFS_SMODE_LIBC_LINK_SPECS}" "${MPFS_SMODE_BOOT_DEFS}"
                     _mpfs_cflags _mpfs_ldflags)

    foreach(_lib mpfs_smode_boot freertos_smode mpfs_drivers)
        add_library(MpfsSmode::${_lib} STATIC IMPORTED)
        set_target_properties(MpfsSmode::${_lib} PROPERTIES
            IMPORTED_LOCATION "${MpfsSmode_PREFIX}/lib/lib${_lib}.a")
    endforeach()

    add_library(MpfsSmode::mpfs_smode_headers INTERFACE IMPORTED)
    set_target_properties(MpfsSmode::mpfs_smode_headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
            "${MpfsSmode_PREFIX}/include;${MpfsSmode_PREFIX}/include/freertos"
        INTERFACE_COMPILE_OPTIONS "${_mpfs_cflags}")
    set_target_properties(MpfsSmode::mpfs_drivers PROPERTIES
        INTERFACE_LINK_LIBRARIES MpfsSmode::mpfs_smode_headers)

    # Whole-archive: weak defaults (the HAL's u54_1(), smode_diag.c's
    # smode_os_init()) live in archive members that are always loaded, so
    # without it the strong definitions in other members would never be
    # pulled in. --gc-sections removes what is unused.
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
