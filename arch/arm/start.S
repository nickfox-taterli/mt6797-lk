/* SPDX-License-Identifier: MIT
 * AArch32 SVC entry from the MT6797 BL31; r4/r5 carry vendor boot tags.
 * No thread or interrupt scheduler is needed by the synchronous boot path.
 */
.syntax unified
.arch armv8-a
.arm
.section .text.boot,"ax"
.global _start
_start:
    b reset
    b undef
    b svc
    b prefetch
    b data_abort
    b reserved
    b irq
    b fiq
reset:
    cpsid aif
    /* Preserve the proven BSP's AArch32 coherency enable on the boot CPU. */
    mrc p15, 0, r0, c1, c0, 1
    orr r0, r0, #0x40
    mcr p15, 0, r0, c1, c0, 1
    bl cache_clean_invalidate
    mrc p15, 0, r0, c1, c0, 0
    bic r0, r0, #0x1000
    bic r0, r0, #5
    bic r0, r0, #0x2000
    mcr p15, 0, r0, c1, c0, 0
    isb
    ldr r0, =_start
    mcr p15, 0, r0, c12, c0, 0
    isb
    cps #0x1b
    ldr sp, =__exception_stack
    cps #0x17
    ldr sp, =__exception_stack
    cps #0x12
    ldr sp, =__exception_stack
    cps #0x11
    ldr sp, =__exception_stack
    cps #0x13
    ldr sp, =__stack_top
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
1:  cmp r0, r1
    strlo r2, [r0], #4
    blo 1b
    bl kmain
    b .
.macro exception name, number, adjust
\name:
    mov r0, #\number
    sub r1, lr, #\adjust
    b exception_stop
.endm
exception undef, 1, 4
exception svc, 2, 4
exception prefetch, 3, 4
exception data_abort, 4, 8
exception reserved, 5, 4
exception irq, 6, 4
exception fiq, 7, 4

.text
.global linux_enter
linux_enter:
    cpsid aif
    bl cache_clean_invalidate
    /* No stack accesses after cleaning: disable the MMU/caches in place. */
    mrc p15, 0, r0, c1, c0, 0
    bic r0, r0, #0x1000
    bic r0, r0, #5
    mcr p15, 0, r0, c1, c0, 0
    isb
    mov r0, #0
    mcr p15, 0, r0, c7, c5, 0
    mcr p15, 0, r0, c7, c5, 6
    mcr p15, 0, r0, c8, c7, 0
    dsb sy
    isb
    ldr r1, =0x40080000
    ldr r2, =0x44000000
    mov r3, #0
    mov r4, #1
    ldr r0, =0x82000200
    dsb sy
    isb
    smc #0
    mov r0, #8
    mov r1, #0
    b exception_stop
.section .note.GNU-stack,"",%progbits
