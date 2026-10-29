/* SPDX-License-Identifier: MIT
 * MSDC0 PIO takeover of the fixed vendor Preloader's selected eMMC.
 * Preserve its bus voltage, clock, width and calibrated sampling. No CMD6,
 * write/erase command, GPT parsing, SD/TF host or power-cycle is present.
 */
#include <x20/boot.h>
#include <string.h>
#define MSDC 0x11230000u
#define REG(off) (MSDC + (off))
#define CMD_ERRORS ((1u << 9) | (1u << 10))
#define DATA_ERRORS ((1u << 14) | (1u << 15))
#define R1_ERRORS 0xfdffa080u
static uint8_t sector[512] __attribute__((aligned(4)));

static int wait_clear(unsigned reg, uint32_t mask)
{
    uint32_t start = timer_ticks();
    while (read32(REG(reg)) & mask)
        if (expired(start, 1000)) return E_TIMEOUT;
    return 0;
}
static int fifo_clear(void)
{
    write32(REG(0x14), 1u << 31);
    return wait_clear(0x14, 1u << 31);
}
static int command(unsigned cmd, uint32_t arg, bool data)
{
    if (wait_clear(0x3c, 3)) return E_TIMEOUT;
    write32(REG(0x0c), 0xffffffff);
    if (data) write32(REG(0x50), 1);
    write32(REG(0x38), arg);
    write32(REG(0x34), cmd | (1u << 7) |
            (data ? (1u << 11) | (512u << 16) : 0));
    uint32_t start = timer_ticks(), status;
    do {
        status = read32(REG(0x0c));
        if (status & CMD_ERRORS) return E_IO;
        if (expired(start, 1000)) return E_TIMEOUT;
    } while (!(status & (1u << 8)));
    write32(REG(0x0c), 1u << 8);
    return read32(REG(0x40)) & R1_ERRORS ? E_IO : 0;
}
static int read_block(unsigned cmd, uint32_t arg, void *dest)
{
    uint8_t *p = dest;
    if (fifo_clear()) return E_IO;
    int ret = command(cmd, arg, true);
    if (ret) return ret;
    uint32_t start = timer_ticks();
    unsigned done = 0;
    bool complete = false;
    while (done < 512 || !complete) {
        uint32_t status = read32(REG(0x0c));
        if (status & (CMD_ERRORS | DATA_ERRORS)) return E_IO;
        if (status & (1u << 12)) complete = true;
        unsigned available = read32(REG(0x14)) & 0xff;
        while (available >= 4 && done < 512) {
            uint32_t value = read32(REG(0x1c));
            /* Avoid assuming alignment of the caller's destination. */
            p[done++] = (uint8_t)value;
            p[done++] = (uint8_t)(value >> 8);
            p[done++] = (uint8_t)(value >> 16);
            p[done++] = (uint8_t)(value >> 24);
            available -= 4;
        }
        if (expired(start, 1000)) return E_TIMEOUT;
    }
    write32(REG(0x0c), 1u << 12);
    wdt_kick();
    return 0;
}
int emmc_init(void)
{
    /* The immutable Preloader loaded LK/tee1 from USER with RCA=1. Check
     * transfer state and EXT_CSD before accepting that handover contract.
     */
    if (!(read32(REG(0)) & 1)) return E_ABI;
    write32(REG(0x10), 0);
    write32(REG(0), read32(REG(0)) | 8u);
    if (fifo_clear()) return E_IO;
    int ret = command(13, 1u << 16, false);
    if (ret) return ret;
    uint32_t status = read32(REG(0x40));
    if (((status >> 9) & 15) != 4 || !(status & (1u << 8))) return E_ABI;
    ret = read_block(8, 0, sector);
    if (ret) return ret;
    /* Refuse another hardware partition or an unexpected capacity/card ABI. */
    if ((sector[179] & 7) || get_le32(sector + 212) <= BUNDLE_LBA + BUNDLE_MAX / 512)
        return E_ABI;
    return 0;
}
int emmc_bundle_read(void *dest)
{
    uint8_t *p = dest;
    struct boot_image image;
    uint32_t total = BUNDLE_HEADER;
    for (uint32_t off = 0; off < total; off += 512) {
        int ret = E_IO;
        for (unsigned attempt = 0; attempt < 2; attempt++) {
            ret = read_block(17, BUNDLE_LBA + off / 512, p + off);
            if (!ret) break;
            /* A failed transfer may still occupy the controller. Bound
             * recovery instead of looping indefinitely or resetting eMMC.
             */
            write32(REG(0), read32(REG(0)) | 4u);
            if (wait_clear(0, 4)) break;
        }
        if (ret) { log_error("emmc", ret, BUNDLE_LBA + off / 512); return ret; }
        if (off + 512 == BUNDLE_HEADER) {
            ret = bundle_header(p, BUNDLE_MAX, &image);
            if (ret) return ret;
            total = image.total;
        }
    }
    return (int)total;
}
