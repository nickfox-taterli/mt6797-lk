/* SPDX-License-Identifier: MIT */
#include <x20/boot.h>

void exception_stop(uint32_t kind, uint32_t pc)
{
    log_error("exception", -(int)kind, pc);
    /* Preserve registers and the watchdog reset path after an unrecoverable
     * CPU fault. Exceptions cannot safely reuse the download stack. */
    for (;;) __asm__ volatile("wfi");
}
void kmain(void)
{
    /* This image is for the one fixed 2 GiB X20 board. The vendor tag blob
     * is not needed to derive memory, storage policy or kernel arguments.
     */
    platform_init();
    log_line("[X20] " X20_BUILD_ID);
    mmu_enable();
#ifdef X20_RAM_RESCUE
    rescue();
#endif
    int ret = rescue_requested();
    if (ret < 0) { log_error("RTC", ret, 0); rescue(); }
    if (ret) rescue();
    /* An explicit 'r' during this short window enters RAM rescue. */
    uint32_t start = timer_ticks();
    do { if (uart_get() == 'r') rescue(); } while (!expired(start, 100));
    ret = emmc_init();
    if (ret) { log_error("eMMC takeover", ret, 0); rescue(); }
    ret = emmc_bundle_read((void *)RECEIVE_LOAD);
    if (ret < 0) { log_error("bundle read", ret, 0); rescue(); }
    ret = image_prepare((void *)RECEIVE_LOAD, (size_t)ret);
    log_error("boot", ret, 0);
    rescue();
}
