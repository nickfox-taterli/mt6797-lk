/* SPDX-License-Identifier: MIT */
#include <x20/boot.h>
#include <libfdt.h>
#include <string.h>
extern int fdt_prepare(void *, size_t, size_t, uint32_t);

int image_prepare(const void *bundle, size_t len)
{
    struct boot_image image;
    const uint8_t *p = bundle;
    size_t consumed, written;
    int ret = bundle_check(bundle, len, &image);
    if (ret) return ret;
    /* All metadata and CRCs pass before the first destination is touched. */
    memcpy((void *)KERNEL_LOAD, p + image.kernel.offset, image.kernel.size);
    wdt_kick();
    ret = gzip_decode((void *)KERNEL_LOAD, image.kernel.size,
            (void *)KERNEL_ENTRY, KERNEL_MAX, &consumed, &written);
    if (ret) return ret;
    const uint8_t *kernel = (void *)KERNEL_ENTRY;
    /* Linux arm64 Image, little endian. Current 6.18 Image uses text_offset=0;
     * the existing X20 entry remains 0x40080000. Also accept the older header's
     * 512 KiB offset at this same proven entry address.
     */
    if (written < 64 || get_le32(kernel + 56) != 0x644d5241 ||
        (get_le64(kernel + 8) != 0 && get_le64(kernel + 8) != 0x80000) ||
        !get_le64(kernel + 16) || get_le64(kernel + 16) > KERNEL_MAX ||
        get_le64(kernel + 16) < written || (get_le64(kernel + 24) & 1))
        return E_IMAGE;
    const uint8_t *dtb;
    size_t dtb_size;
    if (image.dtb.size) {
        /* Explicit separate-DTB bundle; trailing data in gzip is an error. */
        if (consumed != image.kernel.size) return E_GZIP;
        dtb = p + image.dtb.offset;
        dtb_size = image.dtb.size;
    } else {
        dtb = (const uint8_t *)KERNEL_LOAD + consumed;
        dtb_size = image.kernel.size - consumed;
    }
    if (dtb_size < sizeof(struct fdt_header) || dtb_size > DTB_MAX)
        return E_FDT;
    /* An appended FDT need not be aligned. libfdt only sees the aligned copy. */
    memcpy((void *)DTB_LOAD, dtb, dtb_size);
    if (fdt_totalsize((void *)DTB_LOAD) != dtb_size) return E_FDT;
    ret = fdt_prepare((void *)DTB_LOAD, dtb_size, DTB_MAX, image.initrd.size);
    if (ret) return ret;
    if (image.initrd.size)
        memcpy((void *)INITRD_LOAD, p + image.initrd.offset, image.initrd.size);
    wdt_kick();
    log_line("[X20] bundle verified; Linux");
    wdt_disable();
    linux_enter();
}
