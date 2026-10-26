# SPDX-License-Identifier: MIT
#
# Compile and link options shared by the library build and by applications;
# the CMake counterpart of MPFS_CFLAGS / MPFS_LDFLAGS in mk/toolchain.mk.
#
# -mcmodel=medany and -fno-jump-tables are what let the virtual-linked image
# run from its physical copy in M-mode (HAL start-up, shim, smode_m_prehook):
# see README.md, "How one image runs both physical and virtual". Code that an
# application runs in M-mode must be built with them too.

# Default -march for the compiler in use: GCC >= 12 split Zicsr/Zifencei out
# of 'g' (SoftConsole's GCC 8.3 gets plain rv64gc).
function(mpfs_smode_default_march out)
    if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 12)
        set(${out} "rv64gc_zicsr_zifencei" PARENT_SCOPE)
    else()
        set(${out} "rv64gc" PARENT_SCOPE)
    endif()
endfunction()

# mpfs_smode_flags(<march> <libc-specs> <libc-link-specs> <out-compile> <out-link>)
function(mpfs_smode_flags march libc_specs libc_link_specs out_c out_ld)
    separate_arguments(_specs NATIVE_COMMAND "${libc_specs}")
    separate_arguments(_link_specs NATIVE_COMMAND "${libc_link_specs}")
    set(${out_c}
        -march=${march} -mabi=lp64d -mcmodel=medany -mstrict-align
        -fno-jump-tables -ffunction-sections -fdata-sections
        -fmessage-length=0 -fsigned-char ${_specs}
        PARENT_SCOPE)
    set(${out_ld}
        -march=${march} -mabi=lp64d -mcmodel=medany ${_specs} ${_link_specs}
        -nostartfiles -Wl,--gc-sections
        PARENT_SCOPE)
endfunction()
