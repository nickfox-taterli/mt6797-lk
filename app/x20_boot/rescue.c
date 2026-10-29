/* SPDX-License-Identifier: MIT */
#include <x20/boot.h>
#include <string.h>

static void reply(const char *code, uint32_t next)
{
    uint8_t bytes[8];
    memcpy(bytes, code, 4);
    for (unsigned i = 0; i < 4; i++) bytes[4 + i] = (uint8_t)(next >> (i * 8));
    uart_write(bytes, sizeof(bytes));
}
void rescue(void)
{
    uint8_t command[4], header[12], chunk[4096];
    uint8_t *data = (void *)RECEIVE_LOAD;
    uint32_t length = 0, next = 0, expected = 0;
    struct boot_image image;
    wdt_disable();
    log_line("rescue: UART V2; RAM only");
    for (;;) {
        if (uart_read(command, 4, 1000)) continue;
        if (!memcmp(command, "PING", 4)) {
            reply("PONG", next);
        } else if (!memcmp(command, "CANC", 4)) {
            length = next = 0;
            reply("CANC", 0);
        } else if (!memcmp(command, "LOAD", 4)) {
            length = next = 0;
            if (uart_read(header, 8, 1000)) { reply("FAIL", 0); continue; }
            uint32_t n = get_le32(header);
            if (n < BUNDLE_HEADER || n > BUNDLE_MAX) { reply("FAIL", 0); continue; }
            length = n;
            expected = get_le32(header + 4);
            reply("RDY!", 0);
        } else if (!memcmp(command, "DATA", 4)) {
            if (uart_read(header, 12, 1000)) { length = next = 0; reply("FAIL", 0); continue; }
            uint32_t off = get_le32(header), n = get_le32(header + 4);
            if (!length || !n || n > sizeof(chunk) || off > length || n > length - off) {
                length = next = 0;
                reply("FAIL", 0);
                continue;
            }
            if (uart_read(chunk, n, 1000)) { length = next = 0; reply("FAIL", 0); continue; }
            if (x20_crc32(chunk, n) != get_le32(header + 8)) {
                reply("RETR", next);
                continue;
            }
            if (off == next) {
                memcpy(data + off, chunk, n);
                next += n;
            } else if (off > next || n > next - off || memcmp(data + off, chunk, n)) {
                reply("RETR", next);
                continue;
            }
            /* Duplicate packets are acknowledged without appending again. */
            reply("OKAY", next);
        } else if (!memcmp(command, "BOOT", 4)) {
            if (!length || next != length || x20_crc32(data, length) != expected ||
                bundle_check(data, length, &image) || !image.initrd.size) {
                reply("FAIL", next);
                continue;
            }
            reply("BOOT", next);
            int ret = image_prepare(data, length);
            log_error("ram-boot", ret, next);
            length = next = 0;
            reply("FAIL", 0);
        } else {
            reply("FAIL", next);
        }
    }
}
