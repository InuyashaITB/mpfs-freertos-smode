# Boot flow: reset → MMU/PMP configuration → S-mode handover

This document follows U54_1 from reset to the first FreeRTOS task running in
supervisor mode, and shows what M-mode keeps doing afterwards. The diagrams
are [Mermaid](https://mermaid.js.org/), which GitHub renders inline.

Applies to the **LIM-Debug** configuration (`IMAGE_LOADED_BY_BOOTLOADER = 0`,
`MPFS_HAL_HW_CONFIG` defined, `DDR_SUPPORT` off, image linked into L2LIM by
`mpfs-lim.ld`).

| Stage | Privilege | Hart(s) | Code |
|---|---|---|---|
| 1. Reset entry | M | all | `src/platform/mpfs_hal/startup_gcc/mss_entry.S` |
| 2. Platform init, wake other harts | M | E51 | `main_first_hart()` in `src/platform/mpfs_hal/startup_gcc/system_startup.c` |
| 3. Per-hart dispatch | M | all | `main_other_hart()` in `system_startup.c` |
| 4. Release U54_1, park | M | E51, U54_2..4 | `src/application/hart0/e51.c`, `hart2..4/u54_N.c`, `inc/hart_park.h` |
| 5. Peripheral + PLIC S-context set-up | M | U54_1 | `src/application/hart1/u54_1.c` |
| 6. Delegation, PMP, `satp`, `mret` | M → S | U54_1 | `m_shim_enter_smode()` in `src/application/smode/m_shim.c` |
| 7. FreeRTOS start | S | U54_1 | `s_mode_main()` in `src/application/hart1/app_main.c`, `portable/GCC/RISC-V-SMode/` |
| 8. Runtime traps into M-mode | M | U54_1 | `src/application/smode/m_shim_trap.S`, `m_shim.c` |

## 1. Overview

```mermaid
flowchart TD
    RST(["Power-on / reset<br/>all 5 harts start at reset_vector"]) --> ENTRY["mss_entry.S<br/>M-mode, per-hart stack + HLS"]

    ENTRY -->|"hart 0 = MPFS_HAL_FIRST_HART"| MFH["main_first_hart()<br/>L2 cache, init_memory, BEU, MPU,<br/>PMP h0, NWC clocks, PLIC_init_on_reset"]
    ENTRY -->|"harts 1..4"| WFI1["WFI loop<br/>waiting for MSIP from E51"]

    MFH -->|"raise_soft_interrupt(1..4)<br/>one hart at a time"| WFI1
    WFI1 --> MOH["main_other_hart()<br/>init_pmp(hartid), app stack"]
    MFH --> MOH0["main_other_hart()<br/>hart 0"]

    MOH0 --> E51["e51()<br/>UART0 banner"]
    MOH -->|"hart 1"| U541["u54_1()<br/>M-mode"]
    MOH -->|"harts 2..4"| U54N["u54_N()"] --> PARK2["park_this_hart()<br/>WFI forever"]

    E51 -->|"wait g_u54_1_waiting_for_release<br/>raise_soft_interrupt(1)"| U541
    E51 --> PARK0["park_this_hart()<br/>WFI forever"]

    U541 --> PLAT["m_mode_platform_init()<br/>clocks, MMUART1, GPIO2"]
    PLAT --> PLIC["m_mode_plic_init_for_smode()<br/>sources 43/44/118 → S-context"]
    PLIC --> SHIM["m_shim_enter_smode()<br/>mtvec, mideleg/medeleg, PMP, satp=0"]
    SHIM -->|"mret, MPP=S"| SMAIN["s_mode_main()<br/>S-mode, stvec = FreeRTOS handler"]
    SMAIN --> SCHED["vTaskStartScheduler()"]
    SCHED --> TASKS(["FreeRTOS tasks in S-mode<br/>console, blink, spin, heartbeat"])

    classDef m fill:#fde2e2,stroke:#b33,color:#000
    classDef s fill:#e2f0fd,stroke:#336,color:#000
    classDef park fill:#eee,stroke:#888,color:#555
    class ENTRY,MFH,WFI1,MOH,MOH0,E51,U541,U54N,PLAT,PLIC,SHIM m
    class SMAIN,SCHED,TASKS s
    class PARK0,PARK2 park
```

Red = M-mode, blue = S-mode, grey = parked.

## 2. Reset entry (`mss_entry.S`)

Every hart runs the same reset code. Only the hart that equals
`MPFS_HAL_FIRST_HART` (0, the E51) continues to `main_first_hart`; the others
sit in WFI until the E51 sends them a machine software interrupt.

```mermaid
flowchart TD
    A(["reset_vector / _start"]) --> B["clear RAS<br/>mtvec = trap_vector (HAL)"]
    B --> C["mstatus.MIE = 0, mie = 0, mip = 0"]
    C --> D{"mhartid == 0?"}
    D -->|"no (U54)"| D1["mideleg = 0, medeleg = 0<br/>(random after reset)"]
    D -->|"yes (E51)"| E
    D1 --> E["mscratch = mcause = mepc = 0<br/>pmpcfg0 = pmpcfg2 = 0<br/>x1..x31 = 0"]
    E --> F["mstatus.FS | XS = on<br/>fscsr = 0 on U54s"]
    F --> G["sp = __stack_top_hN$<br/>zero stack, reserve HLS"]
    G --> H{"mhartid == MPFS_HAL_FIRST_HART?"}
    H -->|"yes"| I["clear DTIM / L2LIM<br/>j main_first_hart(hls)"]
    H -->|"no"| J["mie = MSIE only<br/>HLS = HLS_OTHER_HART_IN_WFI"]
    J --> K["wfi"]
    K --> L{"mip.MSIP set?"}
    L -->|"no"| K
    L -->|"yes"| M["mie = mip = 0<br/>HLS = HLS_OTHER_HART_PASSED_WFI<br/>j main_other_hart(hls)"]
```

Notes:

* PMP is cleared here (`pmpcfg0/2 = 0`). M-mode ignores PMP entries that are
  not locked, so this has no effect until the hart drops to S-mode. It is
  reprogrammed (and entry 0 locked) by `init_pmp()` in `main_other_hart()`,
  see [section 5](#memory-configuration-at-the-moment-of-mret).
* `mstatus.FS` is turned on here and is never turned off, which is what the
  S-mode port needs for `configENABLE_FPU = 1`.

## 3. E51 brings up the platform and releases U54_1

```mermaid
sequenceDiagram
    autonumber
    participant E51 as E51 (hart 0, M)
    participant U1 as U54_1 (hart 1, M)
    participant U2 as U54_2..4 (M)
    participant HW as CLINT / PLIC / SYSREG

    Note over E51,U2: all harts ran mss_entry.S
    U1->>U1: wfi (HLS_OTHER_HART_IN_WFI)
    U2->>U2: wfi (HLS_OTHER_HART_IN_WFI)

    E51->>HW: config_l2_cache, init_memory (copy .data, zero .bss)
    E51->>HW: init_bus_error_unit, init_mem_protection_unit, init_pmp(0)
    E51->>HW: mss_nwc_init (clocks, IO), PLIC_init_on_reset
    loop hart_id = 1..4
        E51->>HW: raise_soft_interrupt(hart_id)
        HW-->>U1: MSIP (hart 1)
        U1->>U1: HLS_OTHER_HART_PASSED_WFI → main_other_hart
        E51->>E51: wait for HLS_OTHER_HART_PASSED_WFI
    end
    E51->>HW: FIC0..3 clocks on
    E51->>E51: main_other_hart → e51()

    U1->>U1: init_pmp(1), sp = __app_stack_top_h1 → u54_1()
    U2->>U2: init_pmp(N) → u54_N() → park_this_hart()

    U1->>HW: clear_soft_interrupt, mie = MSIP
    U1->>U1: g_u54_1_waiting_for_release = 1
    U1->>U1: wfi
    E51->>HW: UART0 banner
    E51->>E51: spin until g_u54_1_waiting_for_release
    E51->>HW: raise_soft_interrupt(1)
    HW-->>U1: MSIP
    U1->>HW: clear_soft_interrupt, mie = 0
    E51->>E51: park_this_hart() (WFI forever)
```

U54_1 waits twice: the HAL wake-up (steps 4–6) gets it out of
`mss_entry.S`, and the application-level release (steps 15–19) keeps it
from touching peripherals before `e51()` has run. The
`g_u54_1_waiting_for_release` flag stops the release IPI from being wiped by
U54_1's own `clear_soft_interrupt()`.

## 4. U54_1 M-mode set-up (`u54_1.c`)

Everything that needs `mhartid` or the M-mode PLIC context is done here,
because S-mode cannot read `mhartid` and the HAL helpers use it.

```mermaid
flowchart TD
    A(["u54_1() — M-mode<br/>sp = __app_stack_top_h1"]) --> B["clear_soft_interrupt<br/>mie = MSIP<br/>g_u54_1_waiting_for_release = 1"]
    B --> C["wfi until mip.MSIP"]
    C --> D["clear_soft_interrupt<br/>mie = 0"]

    D --> P1
    subgraph PLAT ["m_mode_platform_init()"]
        P1["mss_config_clk_rst:<br/>MMUART1, GPIO2, CFM on"] --> P2["MSS_UART_init(UART1, 115200 8N1)<br/>print [M] banner"]
        P2 --> P3["SYSREG->GPIO_INTERRUPT_FAB_CR = all ones<br/>(GPIO2 → PLIC)"]
        P3 --> P4["MSS_GPIO_init(GPIO2)<br/>16..19, 26..28 out; 30, 31 in, level-high IRQ"]
        P4 --> P5["MSS_GPIO_enable_irq(30, 31)<br/>also enables M-context — undone below"]
    end

    P5 --> Q1
    subgraph PLICS ["m_mode_plic_init_for_smode()"]
        Q1["SPLIC_disable_all<br/>(M and S contexts of hart 1)"] --> Q2["for 43, 44, 118:<br/>priority = 2<br/>disable in M-context<br/>enable in S-context"]
        Q2 --> Q3["S-context threshold = 0"]
        Q3 --> Q4["drain: claim/complete until 0"]
    end

    Q4 --> R["print [M] delegating ... entering S-mode"]
    R --> S(["m_shim_enter_smode(s_mode_main,<br/>&g_s_boot_stack[4096])"])
```

## 5. The handover: `m_shim_enter_smode()`

This is where the MMU and memory protection for S-mode are configured, the
trap routing is set up, and the hart changes privilege.

```mermaid
flowchart TD
    A(["m_shim_enter_smode(entry, s_stack_top)"]) --> B

    subgraph Q ["1 · Quiesce"]
        B["mstatus.MIE = 0<br/>mie = 0<br/>mip &= ~(SSIP | STIP | SEIP)"]
    end

    B --> C
    subgraph T ["2 · M-mode trap path after handover"]
        C["mtvec = m_shim_trap_vector<br/>mscratch = top of g_m_shim_stack (4 KiB)"]
    end

    C --> D
    subgraph DL ["3 · Delegation"]
        D["mideleg = SSIP | STIP | SEIP (0x222)"] --> D2["medeleg = misaligned, access faults,<br/>illegal instr, U-ecall, page faults (0xB1F7)"]
        D2 --> D3["mcounteren = CY | TM | IR (0x7)"]
    end

    D3 --> E
    subgraph MEM ["4 · Memory protection and translation"]
        E["pmpaddr0 = ~0<br/>pmpcfg0 = 0x1F (NAPOT, R W X)<br/>entry 0 already locked by init_pmp(1): write ignored"] --> F["satp = 0 (MODE = Bare, no paging)"]
        F --> G["sfence.vma"]
    end

    G --> H
    subgraph SS ["5 · Clean S-mode state"]
        H["sie = 0, sscratch = 0<br/>(stvec left for S-mode to set)"]
    end

    H --> I
    subgraph RET ["6 · Build the mret"]
        I["mstatus: clear MPP, MPIE, SIE, SPIE, SPP,<br/>MPRV, SUM, TVM, TW, TSR<br/>set MPP = S (01); FS unchanged"] --> J["mepc = entry (s_mode_main)"]
        J --> K["sp = s_stack_top<br/>fence.i"]
    end

    K --> L(["mret → S-mode at s_mode_main<br/>SIE = 0, sp = g_s_boot_stack top"])
```

### Memory configuration at the moment of `mret`

**Address translation (MMU).** `satp = 0` selects `MODE = Bare`: S-mode
virtual addresses are physical addresses and no page tables exist. The
`sfence.vma` discards any stale TLB entries. Because there is no paging, the
page-fault causes delegated in `medeleg` (12, 13, 15) can only come from
software that later turns paging on.

**PMP.** S-mode and U-mode accesses must match a PMP entry, otherwise they
fault (M-mode is unaffected by unlocked entries). The shim asks for one NAPOT
entry covering the whole address space with RWX. This lets S-mode reach L2LIM
(code/data/heap), MMUART1, GPIO2, the PLIC S-context registers and CLINT
`mtime` at `0x0200BFF8`.

> **PMP entry 0 is already locked when the shim runs.** `main_other_hart()`
> calls `init_pmp(1)` → `pmp_configure(1)`, which (with
> `LIBERO_SETTING_MEM_CONFIGS_ENABLED = 0`) writes the Libero values from
> `hw_pmp_hart1.h`: `pmpaddr0 = ~0`, `pmpcfg0 = 0x9F` = **L** | NAPOT | RWX.
> Locked entries ignore writes until reset, so on hardware that honours the
> lock bit the shim's `pmpaddr0`/`pmpcfg0` write does not change entry 0
> (the write does clear the cfg bytes of entries 1–7, which are 0 anyway).
> The effective result is the same — whole space RWX for S/U — but entry 0
> is locked, so it also applies to M-mode and cannot be tightened later
> without a reset. To restrict PMP for S-mode, override the weak
> `init_pmp()` (or skip it for hart 1) so that the entries stay unlocked
> until `m_shim_enter_smode()` programs them.

```mermaid
flowchart LR
    A["mss_entry.S<br/>pmpcfg0 = 0"] --> B["init_pmp(1)<br/>pmpaddr0 = ~0<br/>pmpcfg0 = 0x9F (L=1)"]
    B --> C["m_shim_enter_smode()<br/>pmpaddr0 = ~0, pmpcfg0 = 0x1F"]
    C --> D["effective entry 0:<br/>NAPOT, all memory, RWX, L=1<br/>(shim write ignored for entry 0)"]
```

```mermaid
flowchart LR
    subgraph S ["S-mode access, VA"]
        VA["load / store / fetch"]
    end
    VA -->|"satp.MODE = Bare<br/>VA = PA"| PA["Physical address"]
    PA --> PMP{"PMP entry 0<br/>NAPOT, all of memory<br/>R W X"}
    PMP -->|"match, allowed"| BUS["AXI / L2LIM / peripherals"]
    PMP -.->|"no match (not possible with this config)"| AF["access fault → medeleg → S-mode handler"]
```

**CSR state after `mret`:**

| CSR | Value | Why |
|---|---|---|
| privilege | S | `mstatus.MPP = 01` |
| `pc` | `s_mode_main` | `mepc` |
| `sp` | `&g_s_boot_stack[4096]` | set right before `mret` |
| `sstatus.SIE` / `SPIE` / `SPP` | 0 / 0 / 0 | interrupts off until the scheduler starts |
| `sstatus.FS` | on (from `mss_entry.S`) | FPU context saved by the port |
| `satp` | 0 | Bare, no MMU |
| `pmpcfg0` / `pmpaddr0` | `0x9F` / `~0` (shim writes `0x1F`, see note above) | whole space RWX for S/U, entry locked by the HAL |
| `mideleg` | `0x222` | SSIP, STIP, SEIP go straight to S-mode |
| `medeleg` | `0xB1F7` | faults, illegal instr, U-ecall go to S-mode; breakpoint (3) and S-ecall (9) stay in M |
| `mie` | 0 | only `MTIE` is ever set later, by SBI `set_timer` |
| `mtvec` / `mscratch` | `m_shim_trap_vector` / M stack top | M-mode traps never use the S-mode stack |
| `sie`, `sscratch` | 0 | set by the port / unused |
| `stvec` | not yet set | written first thing in `s_mode_main()` |

## 6. Privilege levels on U54_1

```mermaid
stateDiagram-v2
    direction LR
    [*] --> M_boot: reset
    M_boot: M-mode boot<br/>mss_entry.S → u54_1()
    M_boot --> S_kernel: mret in m_shim_enter_smode()
    S_kernel: S-mode<br/>s_mode_main() / FreeRTOS
    S_kernel --> M_shim: ecall (SBI)<br/>machine timer IRQ<br/>undelegated exception
    M_shim: M-mode shim<br/>m_shim_trap_vector
    M_shim --> S_kernel: mret (SBI done / STIP raised)
    M_shim --> Parked: unhandled trap<br/>m_shim_fatal()
    S_kernel --> S_kernel: delegated trap via stvec<br/>SSIP yield, STIP tick, SEIP PLIC,<br/>faults → freertos_risc_v_trap_handler, sret
    Parked: parked<br/>g_m_shim_fault, WFI forever
```

## 7. S-mode start-up and the first tick

```mermaid
sequenceDiagram
    autonumber
    participant S as s_mode_main / FreeRTOS (S)
    participant M as M-mode shim (M)
    participant CLINT as CLINT mtime / mtimecmp[1]

    Note over S: entered by mret, SIE = 0
    S->>S: stvec = freertos_risc_v_trap_handler
    S->>S: create queue, mutex, 4 tasks
    S->>S: vTaskStartScheduler → xPortStartScheduler
    S->>CLINT: read mtime (0x0200BFF8, PMP allows it)
    S->>M: ecall a7 = TIME, a6 = set_timer, a0 = mtime + 1000
    M->>CLINT: mtimecmp[1] = a0
    M->>M: clear mip.STIP, set mie.MTIE
    M-->>S: mret (a0 = SBI_SUCCESS, mepc += 4)
    S->>S: sie |= SSIE, STIE, SEIE (0x222)
    S->>S: xPortStartFirstTask: sstatus.SIE = 1, ret into first task

    Note over CLINT: 1 ms later, mtime ≥ mtimecmp
    CLINT-->>M: machine timer interrupt (not delegated)
    M->>M: clear mie.MTIE, set mip.STIP
    M-->>S: mret
    S->>S: STIP delegated → stvec, scause = 5
    S->>M: portUPDATE_STIMER_COMPARE: ecall set_timer(next)
    M->>CLINT: mtimecmp[1] = next (clears MTIP)
    M->>M: clear STIP, set MTIE
    M-->>S: mret
    S->>S: xTaskIncrementTick, maybe vTaskSwitchContext, sret
```

Other trap paths after the handover:

* **Yield** – `portYIELD()` sets `sip.SSIP`; delegated, taken as scause 1 by
  the port, which clears SSIP and switches context. No M-mode involvement.
* **PLIC** – sources 43/44/118 assert SEIP on hart 1's S-context; delegated,
  `smode_irq.c` claims/completes on the S-context and calls the HAL handler
  table entries.
* **Exceptions** – delegated faults go to
  `freertos_risc_v_application_exception_handler` (`smode_irq.c`), which
  records `g_smode_fault`. Breakpoints stay in M-mode for the debugger.

## 8. Memory used during the handover

| Region | Address | Used for |
|---|---|---|
| L2LIM | `0x08000000`, 256 KiB | code, data, bss, FreeRTOS heap (`mpfs-lim.ld`) |
| `__app_stack_top_h1` | L2LIM | U54_1 M-mode stack until `mret` |
| `g_s_boot_stack` | `.bss`, 4 KiB | S-mode stack for `s_mode_main()` until the scheduler starts; unused afterwards |
| `xISRStack` | `.bss`, 512 words | S-mode trap stack (`configISR_STACK_SIZE_WORDS`) |
| `g_m_shim_stack` | `.bss`, 4 KiB | M-mode stack for traps after the handover (via `mscratch`) |
| CLINT `mtime` | `0x0200BFF8` | read directly by S-mode (no `time` CSR on the U54) |
| CLINT `mtimecmp[1]` | `0x02004008` | written only by the M-mode shim |

## Turning on the MMU later

The port does not depend on `satp`. To run with Sv39 paging:

1. Build page tables in S-mode (identity-map L2LIM, the peripherals and the
   CLINT `mtime` page at minimum).
2. Write `satp = (8 << 60) | (asid << 44) | (root_pa >> 12)` and `sfence.vma`
   from S-mode, after `stvec` is set so a bad mapping traps cleanly.
3. The M-mode shim needs no mapping: M-mode always uses physical
   addresses (`MPRV = 0`), so `m_shim_trap_vector` and its stack keep working.
4. Tighten PMP at the same time (after unlocking entry 0, see section 5):
   paging protects S-mode from U-mode, PMP is what protects M-mode from S-mode.
