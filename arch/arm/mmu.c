/* SPDX-License-Identifier: MIT */
#include <x20/boot.h>
static uint32_t translation[4096] __attribute__((aligned(16384)));

void mmu_enable(void)
{
    /* Identity sections. Both caches cover normal DRAM; all MMIO remains
     * device memory. PIO and UART copies use the CPU exclusively, with no
     * DMA ownership transitions. Handoff cleans all data caches in assembly.
     */
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t attr = i >= 0x400 && i < 0xc00 ? 0x11c0e : 0xc12;
        translation[i] = (i << 20) | attr;
    }
    uint32_t zero = 0, table = (uint32_t)(uintptr_t)translation, domain = 1, sctlr;
    __asm__ volatile(
        "dsb sy\n"
        "mcr p15,0,%0,c8,c7,0\n"
        "mcr p15,0,%0,c7,c5,0\n"
        "mcr p15,0,%0,c2,c0,2\n"
        "mcr p15,0,%1,c2,c0,0\n"
        "mcr p15,0,%2,c3,c0,0\n"
        "isb\n" :: "r"(zero), "r"(table), "r"(domain) : "memory");
    __asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(sctlr));
    sctlr = (sctlr | 0x1005u) & ~2u;
    __asm__ volatile("mcr p15,0,%0,c1,c0,0\nisb" :: "r"(sctlr) : "memory");
}

