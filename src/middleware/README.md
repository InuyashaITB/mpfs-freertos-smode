# FreeRTOS in this project: deviations from the public RISC-V port

`FreeRTOS-Kernel/` is **FreeRTOS-Kernel V11.3.2** (upstream commit
`640d0c43705e64cd53f46b31d882de6cd8433933`) with one change: the RISC-V
port runs in **supervisor mode (S-mode)** instead of machine mode (M-mode).

This document lists every difference from upstream and explains where the
M-mode and S-mode designs really differ. The S-mode port is in
`FreeRTOS-Kernel/portable/GCC/RISC-V-SMode/`; the upstream port it is derived
from is `portable/GCC/RISC-V/`.

## 1. What is and isn't modified

| Part | Status |
|---|---|
| Kernel core: `tasks.c`, `queue.c`, `list.c`, `timers.c`, `event_groups.c`, `stream_buffer.c`, `croutine.c` | **Unmodified** (byte-identical to V11.3.2) |
| `include/` | **Unmodified**. `include/CMakeLists.txt` dropped (not used by SoftConsole) |
| `portable/MemMang/heap_4.c` | **Unmodified**. The other heap implementations are not included |
| `portable/GCC/RISC-V/` | **Not included** |
| `portable/GCC/RISC-V-SMode/` | **New port**, derived from `portable/GCC/RISC-V/`. Details below |

Port files, compared with upstream:

| File | Change |
|---|---|
| `portASM.S` | Rewritten trap path, timer re-arm and initial task status for S-mode. Removes the split handlers. Includes `FreeRTOSConfig.h` |
| `portContext.h` | Mechanical: `mstatus/mepc/mcause/mret` become `sstatus/sepc/scause/sret` |
| `port.c` | Timer set-up uses SBI and an MMIO `mtime` read. `sie` instead of `mie`. No `mhartid`, no `mtimecmp` pointer |
| `portmacro.h` | `portYIELD()` pends SSIP instead of `ecall`. Interrupt enable uses `sstatus.SIE`. Config checks reduced to `configMTIME_BASE_ADDRESS` |
| `freertos_risc_v_chip_specific_extensions.h` | Copied from `RISCV_MTIME_CLINT_no_extensions`, without `portasmHAS_MTIME` / `portasmHAS_SIFIVE_CLINT` (the S-mode `portASM.S` doesn't use them) |
| `README.md` | Short summary of the port. This document is the detailed version |

Unchanged in behaviour, so code that relies on them works the same:

* Task stack frame layout (`portCONTEXT_SIZE`, register slots, critical
  nesting stored in the frame, `portCRITICAL_NESTING_OFFSET`).
* Switching to the dedicated ISR stack (`xISRStackTop`,
  `configISR_STACK_SIZE_WORDS`) after the context is saved.
* Lazy FPU/VPU saving on `FS`/`VS` == dirty. These fields are at the same
  bit positions in `sstatus` as in `mstatus`, so the `MSTATUS_FS_*` /
  `MSTATUS_VS_*` constants are reused as-is.
* `pxPortInitialiseStack()` frame contents (apart from the status word),
  `xTaskReturnAddress`, `portasmSAVE/RESTORE_ADDITIONAL_REGISTERS` hooks.
* `portYIELD_FROM_ISR()` / `portEND_SWITCHING_ISR()`: still call
  `vTaskSwitchContext()` directly from the trap handler.
* Critical sections (`xCriticalNesting`, enter/exit macros), just with a
  different enable bit.

## 2. Privilege-level CSRs

The mechanical part: every machine CSR the port touches has a supervisor
counterpart.

| Purpose | M-mode port | S-mode port |
|---|---|---|
| Status, saved per task | `mstatus` | `sstatus` |
| Trap return address | `mepc` | `sepc` |
| Trap cause | `mcause` | `scause` |
| Trap return | `mret` | `sret` |
| Trap vector | `mtvec` (set by the application) | `stvec` (set by `smode_os_init()`, libfreertos_smode) |
| Interrupt enables | `mie` | `sie` |
| Interrupt pending (yield) | n/a | `sip` |
| Global interrupt enable | `mstatus.MIE` = bit 3 (`0x8`) | `sstatus.SIE` = bit 1 (`0x2`) |
| Previous enable | `MPIE` = bit 7 | `SPIE` = bit 5 |
| Previous privilege | `MPP` = bits 12:11 (two bits) | `SPP` = bit 8 (one bit) |

`sstatus` is a restricted view of `mstatus`, so writing it from S-mode can
never change M-mode state (`MPP`, `MPIE`, `MIE`, `MPRV`, ...).

## 3. Points where the two designs differ

### 3.1 The tick: S-mode cannot program the timer

This is the biggest difference.

**M-mode port:** the tick is the machine timer interrupt (`mcause` =
interrupt 7). The port computes the address of its own `mtimecmp` from
`mhartid` (`pullMachineTimerCompareRegister`) and writes the next compare
value directly from the trap handler (`portUPDATE_MTIMER_COMPARE_REGISTER`).
Writing `mtimecmp` is also what clears `MTIP`.

**S-mode port:** `mtimecmp` is only meant to be written by M-mode, writing
it never raises a *supervisor* interrupt, and S-mode cannot read `mhartid`.
So:

1. The tick is the **supervisor timer interrupt** (`scause` = interrupt 5).
2. `portUPDATE_STIMER_COMPARE` (in `portASM.S`) and `vPortSetupTimerInterrupt()`
   (in `port.c`) program the next tick with an **SBI ecall**:
   `TIME.set_timer` (`a7` = `0x54494D45`, `a6` = 0, `a0` = absolute time).
3. M-mode firmware (OpenSBI, or `src/smode/m_shim.c` here) writes
   `mtimecmp[hart]`, clears `mip.STIP` and enables `mie.MTIE`. When the
   machine timer fires it masks `MTIE` and sets `mip.STIP`, which is
   delegated to S-mode.

Consequences:

* Each tick costs two extra traps into M-mode: the machine timer interrupt,
  forwarded as STIP, and the `set_timer` ecall. The M-mode port has none.
* The tick handler executes an `ecall` from inside the S-mode trap handler.
  That is safe: the trap goes to M-mode, which only touches M-mode state
  and returns with `a0`/`a1` changed (the macro treats them as clobbered).
* Hart identity is M-mode's problem; the S-mode port never needs a hart ID.
* `configMTIMECMP_BASE_ADDRESS` is no longer used. `configMTIME_BASE_ADDRESS`
  must be non-zero: `mtime` is read directly (memory mapped) when the timer
  starts, so **PMP must let S-mode read it**. The `time` CSR is avoided
  because the PolarFire SoC U54 does not implement it (`rdtime` traps).
* `uxTimerIncrementsForOneTick` and `ullNextTime` work exactly as upstream
  ("next compare = previous compare + one period", so no drift).
  `configCPU_CLOCK_HZ` is still the **mtime** rate, as in upstream.

### 3.2 Yield: interrupt instead of `ecall`

**M-mode port:** `portYIELD()` is `ecall`. It traps synchronously with
`mcause` = 11 (ecall from M-mode); the handler adds 4 to `mepc` and calls
`vTaskSwitchContext()`.

**S-mode port:** an `ecall` from S-mode goes to M-mode (it is how SBI calls
are made) and cannot be the yield. Instead `portYIELD()` sets `sip.SSIP`,
which raises a **supervisor software interrupt** (`scause` = interrupt 1).
The trap handler clears `SSIP` and calls `vTaskSwitchContext()`.

```c
/* upstream */  #define portYIELD()  __asm volatile ( "ecall" );
/* S-mode   */  #define portYIELD()  __asm volatile ( "csrs sip, %0" :: "r" ( 1UL << 1 ) : "memory" )
```

Behavioural differences to know about:

* **Inside a critical section.** An `ecall` traps immediately even with
  interrupts disabled. A pended SSIP is only taken when `sstatus.SIE` is set
  again, i.e. when the critical section ends. This is the PendSV behaviour
  of the Cortex-M ports, which the kernel is designed for.
* **Return address.** The yield is asynchronous, so `sepc` already points to
  the next instruction and is saved unmodified. The M-mode port adjusts
  `mepc` by 4 because `ecall` is synchronous.
* `SSIE` must be enabled in `sie` (done in `xPortStartScheduler()`) and SSIP
  must be delegated in `mideleg`. Otherwise `portYIELD()` does nothing.

**Renode 1.13 (SoftConsole v2022.2): `configPORT_YIELD_VIA_SBI_IPI`.** That
Renode's RISC-V core only re-checks pending interrupts when an interrupt line
changes or M-mode writes `mip`; an S-mode write to `sip` sets the bit but is
not noticed until the next timer edge (see `CSR_SIP` vs `CSR_MIP` in tlib's
`arch/riscv/op_helper.c`). The yield is then taken up to one tick late and
the task keeps running after it has been put on a delayed or event list:
`vTaskDelay()` returns at once, and a blocking `xQueueReceive()` inserts the
same list item twice and hangs in `vListInsert()`. With
`configPORT_YIELD_VIA_SBI_IPI = 1` (set in this project's
`FreeRTOSConfig.h`), `portYIELD()` instead makes an SBI `IPI.send_ipi` call
to its own hart (`configSBI_HART_ID`) and M-mode sets `mip.SSIP`, which
Renode does act on. The trap handling is unchanged (still scause 1, handler
clears SSIP). Costs one ecall per yield; set it to 0 on hardware.

### 3.3 Exceptions

**M-mode port:** `mcause` 11 means yield; any other exception goes to
`freertos_risc_v_application_exception_handler`.

**S-mode port:** no exception is part of normal operation, so **every**
exception goes to `freertos_risc_v_application_exception_handler(scause, sepc + 4)`.
That includes an ecall from U-mode, if someone delegates it. Which
exceptions reach S-mode at all depends on `medeleg`, set by M-mode firmware.
This project delegates faults, illegal instruction and misaligned accesses,
but not breakpoints (left to the debugger) and not ecall-from-S (that's SBI).

### 3.4 Interrupt enables and the status word of a new task

| | M-mode port | S-mode port |
|---|---|---|
| Enabled by `xPortStartScheduler()` | `mie` bits `0x880` (MTIE, MEIE) | `sie` bits `0x222` (SSIE, STIE, SEIE) |
| Initial task status | `mstatus` with `MPIE=1, MPP=M` (`0x1880`), MIE cleared | `sstatus` with `SPIE=1, SPP=S` (`0x120`), SIE cleared |
| First task start (`xPortStartFirstTask`) | `addi t0, t0, 0x8` (set MIE) | `ori t0, t0, 0x2` (set SIE) |

`SPP` must be 1 so that `sret` returns to S-mode. With `SPP` = 0 the task
would start in U-mode. The upstream `addi` relies on MIE being clear; the
S-mode port uses `ori`, which is the same here but does not depend on that.

### 3.5 Trap vector entry points

**M-mode port** exports four entry points:
`freertos_risc_v_trap_handler` (direct mode) and the split handlers
`freertos_risc_v_exception_handler`, `freertos_risc_v_interrupt_handler` and
`freertos_risc_v_mtimer_interrupt_handler`, for vectored `mtvec` or CLIC
set-ups.

**S-mode port** exports only `freertos_risc_v_trap_handler`. `stvec` must
be in **direct** mode. The split handlers and their macros
(`portcontextSAVE_EXCEPTION_CONTEXT` / `portcontextSAVE_INTERRUPT_CONTEXT`,
still defined in `portContext.h`) are unused.

Dispatch in `freertos_risc_v_trap_handler`:

| `scause` | Action |
|---|---|
| interrupt 5 (STI) | re-arm timer via SBI, `xTaskIncrementTick()`, switch if needed |
| interrupt 1 (SSI) | clear `sip.SSIP`, `vTaskSwitchContext()` |
| any other interrupt (e.g. 9, SEI) | `freertos_risc_v_application_interrupt_handler(scause, sepc)` |
| any exception | `freertos_risc_v_application_exception_handler(scause, sepc + 4)` |

### 3.6 External interrupts (PLIC)

The port only forwards them: interrupt 9 (supervisor external) goes to
`freertos_risc_v_application_interrupt_handler`. On the M-mode port this is
interrupt 11 (machine external). The real difference is in the
application, which must use the PLIC's **supervisor context** for the hart
(on PolarFire SoC: context `2 x hart`, e.g. 2 for U54_1) for claim, complete,
threshold and enables. It must also not use helpers that pick the context
from `mhartid`. See `src/smode/smode_plic.h` and `src/freertos_glue/smode_irq.c`.

### 3.7 `mhartid` and other M-mode-only state

The M-mode port reads `mhartid` (to find `mtimecmp`). The S-mode port reads
no machine CSR at all. The same rule applies to everything that runs in
S-mode: reading `mhartid`, `mstatus`, `mie`, ... raises an illegal
instruction exception. In this project that rules out several Microchip HAL
helpers from S-mode (the `PLIC_*` inline functions, `mss_config_clk_rst()`,
`MSS_GPIO_enable_irq()`, `SysTick_Config()`). They are called from M-mode
before the handoff instead.

### 3.8 `FreeRTOSConfig.h` and the assembler

The S-mode `portASM.S` includes `FreeRTOSConfig.h`, so settings such as
`configENABLE_FPU` reach the assembler without extra `-D` flags. Upstream
does not include it, so there those settings must come from the command
line or default to 0. As a result, **`FreeRTOSConfig.h` must be
assembler-safe**: C-only content (prototypes, `configASSERT`) goes inside
`#ifndef __ASSEMBLER__`.

### 3.9 Removed compatibility and configuration paths

* `configCLINT_BASE_ADDRESS` (deprecated upstream) and the derived
  `configMTIME/MTIMECMP_BASE_ADDRESS` defaults: removed.
* "No MTIME" builds (`configMTIME_BASE_ADDRESS` = 0, application-provided
  timer): not supported. `vPortSetupTimerInterrupt()` is still weak and can
  be overridden, but the tick must still arrive as STI.
* `portasmHAS_MTIME`, `portasmHAS_SIFIVE_CLINT`, `portasmHAS_CLINT`: not
  used.
* `portGET_HIGHEST_PRIORITY()`: explicitly casts the 64-bit ready mask to
  `uint32_t` before `__builtin_clz`. Upstream truncates implicitly; the
  result is the same (at most 32 priorities), this just removes the
  warning.

## 4. What the S-mode port needs from M-mode

Upstream needs nothing beyond running in M-mode. The S-mode port depends on
the M-mode firmware (OpenSBI or `src/smode/m_shim.c`) for:

| Requirement | Why |
|---|---|
| `mideleg` includes SSIP (1), STIP (5), and SEIP (9) if the PLIC is used | yield, tick, external interrupts |
| SBI `TIME.set_timer` implemented | tick (§3.1) |
| Machine timer interrupt forwarded as `mip.STIP` | tick (§3.1) |
| PMP lets S-mode access its code, data, peripherals and `mtime` | everything; `mtime` read (§3.1) |
| Entry into S-mode with `sstatus.SIE` = 0 and `FS` != 0 when `configENABLE_FPU` = 1 | FP code would trap with FS off |
| `medeleg` set up for the exceptions S-mode should see | §3.3 |
| `stvec` = `freertos_risc_v_trap_handler` before `vTaskStartScheduler()` (here `smode_os_init()` in libfreertos_smode does it before `s_mode_main()`) | §3.5 |

With OpenSBI as the M-mode firmware, the port needs no changes: it only uses
the standard SBI TIME extension.

## 5. Updating to a newer FreeRTOS release

1. Replace the kernel core files and `include/` with the new release.
2. Diff the new `portable/GCC/RISC-V/` against V11.3.2's (commit above), and
   apply relevant changes to `RISC-V-SMode/`, keeping the substitutions in
   sections 2–3. Watch `portContext.h` (stack frame, FPU/VPU) in particular.
3. Re-check the configuration and `portasm*` macros listed in §3.9 in case
   upstream renamed or added any.
4. Build and confirm the counters described in the project README (tick,
   spin, blink, ext) still behave.

## 6. Older FreeRTOS kernels

The S-mode port and the library run on every FreeRTOS-Kernel release tested
back to **V10.4.3, the kernel of the 202012 LTS**. The bundled V11.3.2 stays
the default; another kernel tree is selected at library build time with
`FREERTOS_KERNEL_DIR`.

| Kernel | LTS line | Library | Demo (`make sim`, two boards) | Hello-world | Needs the config shims (below) |
|---|---|---|---|---|---|
| V11.3.2 (bundled) | | builds | passes | runs | no |
| V11.2.0 | | builds | passes | runs | no |
| V11.1.0 | 202406 LTS | builds | passes | runs | no |
| V11.0.1 | | builds | passes | runs | no |
| V10.6.2 | | builds | passes | runs | yes |
| V10.5.1 | 202210 LTS | builds | passes | runs | yes |
| V10.4.3 | 202012 LTS | builds | passes | runs | yes |

Each row: the library built with the demo's configuration and the
reference configuration, the demo run on both boards in Renode 1.13 and
checked like `make sim` (self-tests, ping and TCP echo between the boards,
FreeRTOS+TCP V4.4.2 included), and the hello-world from
`docs/library-build-and-hello-world.md` run in Renode. Releases older than
V10.4.3 were not tested.

**Why it reaches that far.** The port is self-contained: the stack frame,
critical nesting kept in the frame, the ISR stack, `xTaskReturnAddress`, the
lazy FPU/VPU save and the SBI timer are all in `RISC-V-SMode/`, so it does
not depend on when upstream's M-mode port gained them (`portContext.h` and
in-frame critical nesting in V10.5, FPU/VPU in V11.3). Its interface to the
kernel core is old and stable: the kernel calls `pxPortInitialiseStack()`,
`xPortStartScheduler()` and `vPortEndScheduler()`; the port uses
`pxCurrentTCB`, `vTaskSwitchContext()` and `xTaskIncrementTick()`. The
library glue only uses `xTaskCreate()`, `vTaskStartScheduler()`, the yield
macros and the stack-overflow and malloc-failed hooks.

**The config shims.** Both `FreeRTOSConfig.h` files (the library's
reference one and the demo's) bridge the three differences found:

| Kernels | Problem without the shim | Shim |
|---|---|---|
| before V10.6 | `FreeRTOS.h` requires `configUSE_16_BIT_TICKS` and has no `TICK_TYPE_WIDTH_*`; from V10.6 on, defining both is an error | `configTICK_TYPE_WIDTH_IN_BITS` if `TICK_TYPE_WIDTH_64_BITS` is defined (V10.6 on defines it before including the config), else `configUSE_16_BIT_TICKS 0` |
| before V11.0 | link error: `traceISR_EXIT` / `traceISR_EXIT_TO_SCHEDULER`, used by the port's `portYIELD_FROM_ISR()`, have no default | empty defaults, only if not defined |
| before V11.0 | link error: `pdTICKS_TO_MS`, used by FreeRTOS+TCP V4 | V11's definition, only if not defined |

An application's own `FreeRTOSConfig.h` needs the same lines to use a
kernel older than V11.0 (copy the two blocks marked "Older kernels" and the
tick-width block).

Things that stay different on older kernels, by upstream design rather than
the port:
- **Event groups before V10.6** place their control bits as for a 32-bit
  tick (`configUSE_16_BIT_TICKS 0`: bits 24 to 31), so use event bits 0 to
  23, as with upstream's own RV64 port on those releases. From V10.6 on,
  with 64-bit ticks, bits 0 to 55 are available.
- **V11-only APIs** (for example `configKERNEL_PROVIDED_STATIC_MEMORY`,
  `traceENTER_*` hooks, `configNUMBER_OF_CORES`) are not available to the
  application; the library itself does not use them.

**Building against another kernel.** Point `FREERTOS_KERNEL_DIR` at a
FreeRTOS-Kernel tree (a release archive or a `git clone --branch <tag>` of
FreeRTOS-Kernel): its core `.c` files, `include/` and
`portable/MemMang/heap_4.c` are used, and the S-mode port always comes from
this repository.

```sh
make libs FREERTOS_KERNEL_DIR=C:/FreeRTOS-Kernel-V11.1.0         # root: library + install tree
make -C src FREERTOS_KERNEL_DIR=C:/FreeRTOS-Kernel-V10.5.1 install PREFIX=C:/mpfs-smode-k1051
cmake --preset debug -DFREERTOS_KERNEL_DIR=C:/FreeRTOS-Kernel-V11.1.0
```

Relative paths are relative to `src/`. The setting is recorded in the
install tree (`MPFS_SMODE_BUILT_FREERTOS_KERNEL_DIR`), and changing it
rebuilds the libraries from scratch, like the other library settings; the
installed `include/freertos/` then holds that kernel's headers.
