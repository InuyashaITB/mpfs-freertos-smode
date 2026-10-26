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

# HAL boot-mode definitions (mss_sw_config.h); the same as MPFS_BOOT_DEFS in
# mk/toolchain.mk. loaded_by_bootloader: 0 standalone, 1 started by a
# previous boot stage (e.g. HSS).
function(mpfs_smode_boot_defs loaded_by_bootloader first_hart last_hart out)
    set(${out}
        "-DIMAGE_LOADED_BY_BOOTLOADER=${loaded_by_bootloader} -DMPFS_HAL_FIRST_HART=${first_hart} -DMPFS_HAL_LAST_HART=${last_hart}"
        PARENT_SCOPE)
endfunction()

# mpfs_smode_flags(<march> <libc-specs> <libc-link-specs> <boot-defs>
#                  <out-compile> <out-link>)
function(mpfs_smode_flags march libc_specs libc_link_specs boot_defs out_c out_ld)
    separate_arguments(_specs NATIVE_COMMAND "${libc_specs}")
    separate_arguments(_boot_defs NATIVE_COMMAND "${boot_defs}")
    separate_arguments(_link_specs NATIVE_COMMAND "${libc_link_specs}")
    set(${out_c}
        -march=${march} -mabi=lp64d -mcmodel=medany -mstrict-align
        -fno-jump-tables -ffunction-sections -fdata-sections
        -fmessage-length=0 -fsigned-char ${_specs} ${_boot_defs}
        PARENT_SCOPE)
    set(${out_ld}
        -march=${march} -mabi=lp64d -mcmodel=medany ${_specs} ${_link_specs}
        -nostartfiles -Wl,--gc-sections
        PARENT_SCOPE)
endfunction()
