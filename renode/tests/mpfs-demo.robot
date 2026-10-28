# SPDX-License-Identifier: MIT
#
# Renode Robot Framework tests for the demo (Phase 6): the same two boards as
# 'make sim' (renode/boards.resc), checked line by line with terminal testers
# instead of a UART log afterwards. Build first ('make'), then from the
# project root:
#
#   renode-test renode/tests/mpfs-demo.robot
#
# renode-test comes with Renode and needs Python 3 with robotframework==4.0.1,
# requests, psutil and pyyaml (Renode's tests/requirements.txt). The tests run
# in order on one emulation: each continues where the previous one stopped
# (so there is no per-test teardown, which would reset it). Expected lines
# are matched as substrings; Robot splits arguments at two or more spaces, so
# they must not contain any.

*** Settings ***
Suite Setup       Two Boards Setup
Suite Teardown    Teardown
Resource          ${RENODEKEYWORDS}

*** Variables ***
${PROJECT}        ${CURDIR}/../..
${BUILD}          ${PROJECT}/build
# Override with renode-test --variable ELF:<path> (CTest passes CMake's ELF)
# and --variable OUT:<dir>.
${ELF}            ${BUILD}/examples/freertos-demo/freertos-demo.elf
${OUT}            ${BUILD}/sim
${TIMEOUT}        60

*** Keywords ***
Two Boards Setup
    Setup
    # boards.resc names its files relative to the project root; add it to
    # the monitor's search path (Renode's working directory differs here).
    Execute Command    path add @${PROJECT}
    Execute Command    $elf=@${ELF}
    Execute Command    $uart_log=@${OUT}/robot-uart1.txt
    Execute Command    $uart_log_b=@${OUT}/robot-uart1-board1.txt
    Execute Command    include @${PROJECT}/renode/boards.resc
    ${t0}=    Create Terminal Tester    sysbus.mmuart1    timeout=${TIMEOUT}    machine=board0
    ${t1}=    Create Terminal Tester    sysbus.mmuart1    timeout=${TIMEOUT}    machine=board1
    Set Suite Variable    ${T0}    ${t0}
    Set Suite Variable    ${T1}    ${t1}
    Start Emulation

Both Boards Should Print
    [Arguments]    ${line}
    Wait For Line On Uart    ${line}    testerId=${T0}
    Wait For Line On Uart    ${line}    testerId=${T1}

*** Test Cases ***
Should Enter S-Mode With Sv39
    Both Boards Should Print    [S] Sv39 on:
    Both Boards Should Print    fabric VA 0x40000000 -> PA 0x40000000

Should Pass The SPI Protocol Device Self-Test
    Both Boards Should Print    [S] SPI1 proto: corrupted CRC -> NAK BAD_CRC
    Both Boards Should Print    [S] SPI1 proto (S-mode, ProtoSpiDevice): PASS

Should Pass The SPI NOR Flash Self-Test
    Both Boards Should Print    [S] SPI0 flash: program 0x5A over it -> 0x00
    Both Boards Should Print    [S] SPI0 flash (S-mode, NorFlash model): PASS

Should Start The Scheduler
    Both Boards Should Print    [S] scheduler running

Should Pass The I2C EEPROM Self-Test
    Both Boards Should Print    [S] I2C0 EEPROM: raw write at 0x3C wraps to 0x00
    Both Boards Should Print    [S] I2C0 EEPROM (S-mode, MSS_I2C driver, interrupts): PASS

Should Ping And Echo Over TCP Between The Boards
    Wait For Line On Uart    [S] NET: ping board 1: PASS    testerId=${T0}
    Wait For Line On Uart    [S] NET: ping board 0: PASS    testerId=${T1}
    Wait For Line On Uart    [S] NET: TCP echo with board 1: PASS    testerId=${T0}
    Wait For Line On Uart    [S] NET: TCP echo with board 0: PASS    testerId=${T1}

Should Deliver An Injected Device Event By Interrupt
    Execute Command    mach set "board0"
    Execute Command    sysbus.spi1.proto InjectEvent 0x5c
    Wait For Line On Uart    [S] SPI1 proto: data-ready interrupt, events: 5c    testerId=${T0}
