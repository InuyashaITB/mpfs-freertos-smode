# SPDX-License-Identifier: MIT
#
# Compile and link flags shared by the library build (../CMakeLists.txt) and
# the installed package (MpfsSmodeConfig.cmake), so an application is built
# with the flags the libraries were built with. mk/toolchain.mk has the same
# flags for make.

function(mpfs_smode_default_march out)
    if(CMAKE_C_COMPILER_VERSION VERSION_LESS 12)
        set(${out} "rv64gc" PARENT_SCOPE)
    else()
        set(${out} "rv64gc_zicsr_zifencei" PARENT_SCOPE)
    endif()
endfunction()

function(mpfs_smode_flags march libc_specs libc_link_specs out_c out_ld)
    separate_arguments(_specs NATIVE_COMMAND "${libc_specs}")
    separate_arguments(_link_specs NATIVE_COMMAND "${libc_link_specs}")
    set(${out_c}
        -march=${march} -mabi=lp64d -mcmodel=medany -mstrict-align -fno-jump-tables
        -ffunction-sections -fdata-sections -fmessage-length=0 ${_specs}
        PARENT_SCOPE)
    set(${out_ld}
        -march=${march} -mabi=lp64d -mcmodel=medany ${_specs} ${_link_specs}
        -nostartfiles -Wl,--gc-sections
        PARENT_SCOPE)
endfunction()

# $<LINK_LIBRARY:WHOLE_ARCHIVE,...> for GNU ld: CMake only defines it per
# platform, and a bare-metal build is the Generic platform.
macro(mpfs_smode_whole_archive_support)
    if(NOT CMAKE_LINK_LIBRARY_USING_WHOLE_ARCHIVE_SUPPORTED)
        set(CMAKE_LINK_LIBRARY_USING_WHOLE_ARCHIVE
            "LINKER:--whole-archive" "<LINK_ITEM>" "LINKER:--no-whole-archive")
        set(CMAKE_LINK_LIBRARY_USING_WHOLE_ARCHIVE_SUPPORTED TRUE)
    endif()
endmacro()
