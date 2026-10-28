/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <x20/boot.h>
#include <setjmp.h>
#include <sys/mman.h>
static jmp_buf entered;
void wdt_kick(void) {}
void wdt_disable(void) {}
void log_line(const char *s) { (void)s; }
void linux_enter(void) { longjmp(entered, 1); }
int test_image_prepare(const void *data, size_t length)
{
    const uint32_t addr[] = {KERNEL_ENTRY, KERNEL_LOAD, DTB_LOAD, INITRD_LOAD};
    const uint32_t size[] = {KERNEL_MAX, BUNDLE_MAX, DTB_MAX, INITRD_MAX};
    unsigned mapped = 0;
    int ret = E_IO;
    for (; mapped < 4; mapped++)
        if (mmap((void *)(uintptr_t)addr[mapped], size[mapped], PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == MAP_FAILED)
            goto cleanup;
    ret = setjmp(entered) ? 1 : image_prepare(data, length);
cleanup:
    while (mapped--) munmap((void *)(uintptr_t)addr[mapped], size[mapped]);
    return ret;
}
