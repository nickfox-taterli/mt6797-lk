/* SPDX-License-Identifier: MIT
 * Execute the production protocol against a scripted UART and a mapped RAM
 * receive window. Hardware I/O alone is replaced; rescue.c is unchanged.
 */
#include <x20/boot.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static uint8_t input[20000], output[2000], bundle[2048];
static size_t in_size, in_pos, out_size;
static unsigned boots;
static jmp_buf finished;
static void put32(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (i * 8));
}
static void append(const void *p, size_t n)
{
    assert(in_size + n <= sizeof(input));
    memcpy(input + in_size, p, n); in_size += n;
}
static void word(uint32_t n)
{
    uint8_t p[4]; put32(p, n); append(p, 4);
}
static void packet(uint32_t off, uint32_t n, bool bad_crc)
{
    append("DATA", 4); word(off); word(n);
    word(x20_crc32(bundle + off, n) ^ (bad_crc ? 1 : 0));
    append(bundle + off, n);
}
int uart_read(void *buf, size_t n, uint32_t timeout)
{
    (void)timeout;
    if (in_pos + n > in_size) longjmp(finished, 1);
    memcpy(buf, input + in_pos, n); in_pos += n;
    return 0;
}
int uart_write(const void *buf, size_t n)
{
    assert(out_size + n <= sizeof(output));
    memcpy(output + out_size, buf, n); out_size += n;
    return 0;
}
void wdt_disable(void) {}
void log_line(const char *s) { (void)s; }
void log_error(const char *s, int e, uint32_t c) { (void)s; (void)e; (void)c; }
int image_prepare(const void *p, size_t n)
{
    assert(n == sizeof(bundle) && !memcmp(p, bundle, n));
    boots++;
    return E_GZIP;
}
static void expect(size_t i, const char *code, uint32_t next)
{
    assert(i * 8 + 8 <= out_size);
    assert(!memcmp(output + i * 8, code, 4));
    assert(get_le32(output + i * 8 + 4) == next);
}
int main(void)
{
    assert(mmap((void *)RECEIVE_LOAD, BUNDLE_MAX, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)RECEIVE_LOAD);
    memcpy(bundle, "X20BNDL\0", 8);
    put32(bundle + 8, 1); put32(bundle + 12, 1024); put32(bundle + 16, 64);
    put32(bundle + 20, 2); put32(bundle + 24, 1); put32(bundle + 32, 2048);
    put32(bundle + 40, 1024);
    const char *names[] = {"kernel", "initramfs"};
    for (unsigned i = 0; i < 2; i++) {
        uint8_t *e = bundle + 64 + i * 64;
        put32(e, i ? 4 : 2); put32(e + 4, 1); put32(e + 8, 1024 + i * 512);
        put32(e + 16, 1); bundle[1024 + i * 512] = (uint8_t)('K' + i);
        put32(e + 40, x20_crc32(bundle + 1024 + i * 512, 1));
        strcpy((char *)e + 48, names[i]);
    }
    put32(bundle + 48, x20_crc32(bundle, 1024));
    append("PINGLOAD", 8); word(sizeof(bundle)); word(x20_crc32(bundle, sizeof(bundle)));
    packet(0, 1024, true);
    packet(0, 1024, false);
    packet(0, 1024, false); /* lost ACK -> idempotent retransmit */
    packet(1536, 512, false); /* out-of-order */
    packet(1024, 1024, false);
    append("BOOTCANCBOOTLOAD", 16); word(BUNDLE_MAX + 1); word(0);
    if (!setjmp(finished)) rescue();
    expect(0, "PONG", 0); expect(1, "RDY!", 0);
    expect(2, "RETR", 0); expect(3, "OKAY", 1024); expect(4, "OKAY", 1024);
    expect(5, "RETR", 1024); expect(6, "OKAY", 2048);
    expect(7, "BOOT", 2048); expect(8, "FAIL", 0);
    expect(9, "CANC", 0); expect(10, "FAIL", 0); expect(11, "FAIL", 0);
    assert(out_size == 12 * 8 && boots == 1);
    munmap((void *)RECEIVE_LOAD, BUNDLE_MAX);
    puts("UART CRC/retry/duplicate/order/cancel/boot/size bounds: PASS");
    return 0;
}
