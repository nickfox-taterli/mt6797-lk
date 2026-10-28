/* SPDX-License-Identifier: MIT */
#include <x20/boot.h>
#include <lib/miniz.h>
#include <string.h>

uint32_t get_le32(const void *ptr)
{
    const uint8_t *p = ptr;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
uint64_t get_le64(const void *ptr)
{
    const uint8_t *p = ptr;
    return (uint64_t)get_le32(p) | (uint64_t)get_le32(p + 4) << 32;
}
uint32_t x20_crc32(const void *ptr, size_t size)
{
    return (uint32_t)mz_crc32(0, ptr, size);
}
int bundle_header(const void *ptr, size_t available, struct boot_image *out)
{
    const uint8_t *p = ptr;
    uint8_t header[BUNDLE_HEADER];
    uint32_t count;
    uint64_t total;
    struct boot_image image = {0};
    if (!ptr || !out || available < BUNDLE_HEADER)
        return E_HEADER;
    if (memcmp(p, "X20BNDL\0", 8) || get_le32(p + 8) != 1 ||
        get_le32(p + 12) != BUNDLE_HEADER || get_le32(p + 16) != 64 ||
        get_le32(p + 24) != 1 || get_le64(p + 40) != BUNDLE_HEADER)
        return E_HEADER;
    memcpy(header, p, sizeof(header));
    memset(header + 48, 0, 4);
    if (x20_crc32(header, sizeof(header)) != get_le32(p + 48))
        return E_CRC;
    total = get_le64(p + 32);
    count = get_le32(p + 20);
    if (count < 1 || count > 3 || total < BUNDLE_HEADER ||
        total > BUNDLE_MAX || total > available || (total & 511))
        return E_RANGE;
    image.total = (uint32_t)total;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = p + 64 + i * 64;
        uint32_t type = get_le32(e), flags = get_le32(e + 4);
        uint64_t offset = get_le64(e + 8), size = get_le64(e + 16);
        uint64_t load = get_le64(e + 24), pc = get_le64(e + 32);
        struct entry *dest;
        const char *name;
        uint32_t address, limit, entry = 0;
        if (flags & ~3u || (type != 2 && (flags & 2)))
            return E_ENTRY;
        switch (type) {
        case 2: dest = &image.kernel; name = "kernel";
            address = KERNEL_LOAD; limit = BUNDLE_MAX; entry = KERNEL_ENTRY; break;
        case 3: dest = &image.dtb; name = "dtb";
            address = DTB_LOAD; limit = DTB_MAX; break;
        case 4: dest = &image.initrd; name = "initramfs";
            address = INITRD_LOAD; limit = INITRD_MAX; break;
        default: return E_ENTRY;
        }
        /* Canonical names/types, destinations and entry points are a whitelist.
         * Zero load/pc means the same fixed default as the deployed v1 packer.
         */
        if (dest->size || memcmp(e + 48, name, strlen(name) + 1))
            return E_ENTRY;
        for (size_t k = strlen(name) + 1; k < 16; k++)
            if (e[48 + k]) return E_ENTRY;
        if (!size || size > limit || offset < BUNDLE_HEADER ||
            (offset & 511) || offset > total || size > total - offset ||
            (load && load != address) || (pc && pc != entry))
            return E_RANGE;
        for (uint32_t j = 0; j < i; j++) {
            const uint8_t *old = p + 64 + j * 64;
            uint64_t old_offset = get_le64(old + 8), old_size = get_le64(old + 16);
            if (offset < old_offset + old_size && old_offset < offset + size)
                return E_RANGE;
        }
        *dest = (struct entry){(uint32_t)offset, (uint32_t)size,
                               address, entry, get_le32(e + 40)};
    }
    if (!image.kernel.size) return E_ENTRY;
    *out = image;
    return 0;
}
int bundle_check(const void *ptr, size_t available, struct boot_image *out)
{
    struct boot_image image;
    int ret = bundle_header(ptr, available, &image);
    if (ret) return ret;
    const struct entry *entries[] = {&image.kernel, &image.dtb, &image.initrd};
    for (unsigned i = 0; i < 3; i++) {
        const struct entry *e = entries[i];
        if (e->size && x20_crc32((const uint8_t *)ptr + e->offset, e->size) != e->crc)
            return E_CRC;
    }
    *out = image;
    return 0;
}
