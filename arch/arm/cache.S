/* SPDX-License-Identifier: MIT
 * ARMv7/v8-A AArch32 set/way maintenance. Boot-only, clobbers r0-r12.
 * No stack or memory access: valid both before runtime setup and at handoff.
 */
.syntax unified
.arch armv8-a
.arm
.text
.global cache_clean_invalidate
cache_clean_invalidate:
    dsb sy
    mrc p15, 1, r0, c0, c0, 1
    and r3, r0, #0x07000000
    lsr r3, r3, #23
    mov r10, #0
1:  cmp r10, r3
    bge 5f
    add r2, r10, r10, lsr #1
    mov r1, r0, lsr r2
    and r1, r1, #7
    cmp r1, #2
    blt 4f
    mcr p15, 2, r10, c0, c0, 0
    isb
    mrc p15, 1, r1, c0, c0, 0
    and r2, r1, #7
    add r2, r2, #4
    ubfx r4, r1, #3, #10
    clz r5, r4
    ubfx r7, r1, #13, #15
2:  mov r9, r4
3:  orr r11, r10, r9, lsl r5
    orr r11, r11, r7, lsl r2
    mcr p15, 0, r11, c7, c14, 2
    subs r9, r9, #1
    bge 3b
    subs r7, r7, #1
    bge 2b
4:  add r10, r10, #2
    b 1b
5:  mov r10, #0
    mcr p15, 2, r10, c0, c0, 0
    dsb sy
    isb
    bx lr
.section .note.GNU-stack,"",%progbits
