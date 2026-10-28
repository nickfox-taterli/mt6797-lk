/* SPDX-License-Identifier: MIT */
#ifndef X20_BOOT_H
#define X20_BOOT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BUNDLE_MAX 0x800000u
#define BUNDLE_HEADER 0x400u
#define BUNDLE_LBA 0x4000u
#define KERNEL_LOAD 0x46900000u
#define KERNEL_ENTRY 0x40080000u
#define KERNEL_MAX 0x1c00000u
#define DTB_LOAD 0x44000000u
#define DTB_MAX 0x100000u
#define INITRD_LOAD 0x45000000u
#define INITRD_MAX 0x800000u
#define RECEIVE_LOAD 0x50000000u
#define DRAM_START 0x40000000u
#define DRAM_END 0xc0000000u

enum { E_HEADER = -1, E_CRC = -2, E_ENTRY = -3, E_RANGE = -4,
       E_GZIP = -5, E_FDT = -6, E_IMAGE = -7, E_TIMEOUT = -8,
       E_IO = -9, E_ABI = -10, E_CANCEL = -11 };
struct entry { uint32_t offset, size, load, pc, crc; };
struct boot_image {
    uint32_t total;
    struct entry kernel, dtb, initrd;
};

uint32_t get_le32(const void *p);
uint64_t get_le64(const void *p);
uint32_t x20_crc32(const void *p, size_t n);
int bundle_header(const void *data, size_t available, struct boot_image *out);
int bundle_check(const void *data, size_t available, struct boot_image *out);
int gzip_decode(const void *src, size_t len, void *dst, size_t cap,
                size_t *consumed, size_t *written);
int image_prepare(const void *bundle, size_t len);

static inline uint32_t read32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}
static inline void write32(uintptr_t addr, uint32_t value) {
    *(volatile uint32_t *)addr = value;
    __asm__ volatile("dsb sy" ::: "memory");
}
uint32_t timer_ticks(void);
bool expired(uint32_t start, uint32_t ms);
void platform_init(void);
void wdt_kick(void);
void wdt_disable(void);
int uart_get(void);
int uart_read(void *buf, size_t n, uint32_t timeout);
int uart_write(const void *buf, size_t n);
void log_line(const char *s);
void log_error(const char *stage, int error, uint32_t context);
int rescue_requested(void);
int emmc_init(void);
int emmc_bundle_read(void *dest);
void mmu_enable(void);
void linux_enter(void) __attribute__((noreturn));
void rescue(void) __attribute__((noreturn));
void kmain(void) __attribute__((noreturn));
void exception_stop(uint32_t kind, uint32_t pc) __attribute__((noreturn));
#endif
