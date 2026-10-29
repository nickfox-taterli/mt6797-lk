/* SPDX-License-Identifier: MIT */
#include <x20/boot.h>
#include <string.h>

#define UART 0x11002000u
#define GPT 0x10008000u
#define RGU 0x10007000u
#define PWRAP 0x1000d000u
#define PIO 0x10005000u

uint32_t timer_ticks(void) { return read32(GPT + 0x48); }
bool expired(uint32_t start, uint32_t ms)
{
    return (uint32_t)(timer_ticks() - start) >= ms * 13000u;
}
void wdt_kick(void) { write32(RGU + 8, 0x1971); }
void wdt_disable(void) { write32(RGU, 0x22000000); }
void platform_init(void)
{
    /*
     * Board marker: light all six indicator LEDs (GPIO196-201, high-active
     * through NMOS gates; schematic "KEY_LED_EXT26M" sheet) so a solid row
     * proves BL31 reached LK.  GPIO mode 0, then DOUT high before switching
     * DIR to output to avoid a glitch-low window (register layout from the
     * kernel pinctrl-mt6797 tables: MODE 0x300+0x10*(pin/8), 4-bit fields;
     * DIR 0x10*(pin/32) and DOUT 0x100+0x10*(pin/32), SET alias at +4).
     * Linux gpio-leds reclaims the pins and turns them into its triggers
     * early in boot, so a row stuck solid means the kernel never took over.
     */
    write32(PIO + 0x480, read32(PIO + 0x480) & ~0xffff0000u); /* 196-199 */
    write32(PIO + 0x490, read32(PIO + 0x490) & ~0x000000ffu); /* 200-201 */
    write32(PIO + 0x164, 0x3f0); /* DOUT bank 6 bits 4-9: high */
    write32(PIO + 0x064, 0x3f0); /* DIR bank 6 bits 4-9: output */

    write32(GPT + 0x40, 0);
    write32(GPT + 0x40, 2);
    write32(GPT + 0x44, 0);
    write32(GPT + 0x40, 0x31);
    /* Keep the Preloader's UART mux, source clock and 921600 baud divisor. */
    write32(UART + 4, 0); /* polling only */
    write32(RGU + 4, (10u * 64u << 5) | 8u);
    write32(RGU, 0x2200004du); /* 10s dual, IRQ + reset, active-low external */
    wdt_kick();
    /* No SCP firmware is loaded. Keep the unused core quiescent, matching
     * the proven headless bridge's hardware state before Linux takes over.
     */
    write32(0x100a402c, 0xffff);
    write32(0x100a4000, 1);
    write32(0x100a4004, 0);
    write32(0x100a0000, 0);
    write32(0x100a4020, 0x100);
}
int uart_get(void)
{
    uint32_t status = read32(UART + 0x14);
    if (status & 0x1e) {
        (void)read32(UART);
        return E_IO;
    }
    return status & 1 ? (int)(read32(UART) & 0xff) : E_TIMEOUT;
}
int uart_read(void *buf, size_t n, uint32_t timeout)
{
    uint8_t *p = buf;
    uint32_t start = timer_ticks();
    for (size_t i = 0; i < n;) {
        int c = uart_get();
        if (c == E_IO) return c;
        if (c >= 0) p[i++] = (uint8_t)c;
        if (expired(start, timeout)) return E_TIMEOUT;
        wdt_kick();
    }
    return 0;
}
int uart_write(const void *buf, size_t n)
{
    const uint8_t *p = buf;
    uint32_t start = timer_ticks();
    for (size_t i = 0; i < n; i++) {
        while (!(read32(UART + 0x14) & 0x20))
            if (expired(start, 1000)) return E_TIMEOUT;
        write32(UART, p[i]);
    }
    return 0;
}
void log_line(const char *s)
{
    uart_write(s, strlen(s));
    uart_write("\r\n", 2);
}
static void hex(uint32_t value)
{
    char text[8];
    for (unsigned i = 0; i < 8; i++)
        text[i] = "0123456789abcdef"[(value >> (28 - 4 * i)) & 15];
    uart_write(text, sizeof(text));
}
void log_error(const char *stage, int error, uint32_t context)
{
    uart_write("[X20] ", 6);
    uart_write(stage, strlen(stage));
    uart_write(" error=0x", 9);
    hex((uint32_t)error);
    uart_write(" context=0x", 11);
    hex(context);
    uart_write("\r\n", 2);
}
static int pmic_wait(unsigned state)
{
    uint32_t start = timer_ticks();
    do {
        uint32_t status = read32(PWRAP + 0xa4);
        if (!(status & (1u << 21))) return E_IO;
        if (((status >> 16) & 7) == state) return 0;
    } while (!expired(start, 10));
    return E_TIMEOUT;
}
static int pmic_access(bool write, uint16_t addr, uint16_t *value)
{
    if (pmic_wait(0)) return E_IO;
    write32(PWRAP + 0xa0, (write ? 1u << 31 : 0) |
            (uint32_t)(addr >> 1) << 16 | (write ? *value : 0));
    if (pmic_wait(write ? 0 : 6)) return E_IO;
    if (!write) {
        *value = (uint16_t)read32(PWRAP + 0xa4);
        write32(PWRAP + 0xa8, 1);
    }
    return 0;
}
static int rtc_write(uint16_t addr, uint16_t value)
{
    uint16_t trigger = 1, status;
    if (pmic_access(true, addr, &value) ||
        pmic_access(true, 0x403c, &trigger)) return E_IO;
    uint32_t start = timer_ticks();
    do {
        if (pmic_access(false, 0x4000, &status)) return E_IO;
        if (!(status & 0x40)) return 0;
    } while (!expired(start, 10));
    return E_TIMEOUT;
}
int rescue_requested(void)
{
    uint16_t pdn;
    if (pmic_access(false, 0x402c, &pdn)) return E_IO;
    if (!(pdn & 0x2000)) return 0;
    if (rtc_write(0x4036, 0x586a) || rtc_write(0x4036, 0x9136) ||
        rtc_write(0x402c, pdn & ~0x2000u)) return E_IO;
    return 1;
}
