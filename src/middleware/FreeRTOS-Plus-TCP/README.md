# FreeRTOS-Plus-TCP (vendored subset)

FreeRTOS-Plus-TCP **V4.4.2** (https://github.com/FreeRTOS/FreeRTOS-Plus-TCP,
tag `V4.4.2`), unmodified. Only what libfreertos_tcp builds is kept:

| Kept | From |
|---|---|
| `source/*.c` | the stack (IPv4 and IPv6 sources; ipconfig options decide what is compiled) |
| `source/include/*.h` | its headers |
| `source/portable/BufferManagement/BufferAllocation_2.c` | network buffers from the FreeRTOS heap |
| `source/portable/Compiler/GCC/pack_struct_*.h` | packed-struct helpers for GCC |
| `LICENSE.md`, `History.txt` | |

Left out: the other buffer allocator, the other compiler ports, every
vendor NetworkInterface (this project's LiteEth one is in
`src/freertos_tcp/`), tests, tools and docs.

The configuration (`FreeRTOSIPConfig.h`) is not here: see
`src/freertos_tcp/config/` (library default) and `FREERTOS_IP_CONFIG_DIR`.
