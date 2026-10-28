/* SPDX-License-Identifier: MIT */
#include <x20/boot.h>
#include <libfdt.h>
#include <string.h>

/* Fixed X20-96 GPT contracts. A TF Debian installation must use this distinct
 * GPT partition GUID; a plain filesystem UUID cannot be resolved without an
 * initramfs. There is deliberately no device-number or label fallback.
 */
#if defined(X20_ROOT_TF)
#define ROOT_PARTUUID "f20d0001-6797-4000-8000-000000000001"
#else
#define ROOT_PARTUUID "8273e1ab-846f-4468-b999-ee2ea8e50a16"
#endif
static const char production_args[] =
    "console=ttyS0,921600n8 root=PARTUUID=" ROOT_PARTUUID
    " rootfstype=ext4 rootwait loglevel=4";
static const char rescue_args[] =
    "console=ttyS0,921600n8 earlycon=uart8250,mmio32,0x11002000,921600n8"
    " root=/dev/ram0 rdinit=/init loglevel=4";

static int reserved_range(void *fdt, const char *path, uint32_t addr, uint32_t size)
{
    int node = fdt_path_offset(fdt, path), len;
    const fdt32_t *reg = fdt_getprop(fdt, node, "reg", &len);
    if (!reg || len != 16 || fdt32_to_cpu(reg[0]) ||
        fdt32_to_cpu(reg[1]) != addr || fdt32_to_cpu(reg[2]) ||
        fdt32_to_cpu(reg[3]) != size) return E_FDT;
    if (!fdt_getprop(fdt, node, "no-map", &len)) return E_FDT;
    return 0;
}
int fdt_prepare(void *fdt, size_t source_size, size_t capacity, uint32_t initrd_size)
{
    if (source_size < sizeof(struct fdt_header) || source_size > capacity ||
        capacity > DTB_MAX || initrd_size > INITRD_MAX ||
        fdt_check_full(fdt, source_size)) return E_FDT;
    if (fdt_node_check_compatible(fdt, 0, "archermind,x20-96") ||
        fdt_address_cells(fdt, 0) != 2 || fdt_size_cells(fdt, 0) != 2 ||
        reserved_range(fdt, "/reserved-memory/memory@43f00000", 0x43f00000, 0x1000) ||
        reserved_range(fdt, "/reserved-memory/memory@44600000", 0x44600000, 0x10000))
        return E_FDT;
    if (fdt_open_into(fdt, fdt, (int)capacity)) return E_FDT;
    int memory = fdt_path_offset(fdt, "/memory@40000000");
    fdt32_t reg[] = {0, cpu_to_fdt32(DRAM_START), 0, cpu_to_fdt32(0x7ffa0000)};
    /* The fixed Preloader reserves the last 384 KiB; preserve the deployed
     * Linux memory boundary rather than exposing secure/firmware DRAM.
     */
    if (memory < 0 || fdt_setprop(fdt, memory, "reg", reg, sizeof(reg))) return E_FDT;
    int chosen = fdt_path_offset(fdt, "/chosen");
    if (chosen == -FDT_ERR_NOTFOUND) chosen = fdt_add_subnode(fdt, 0, "chosen");
    if (chosen < 0 || fdt_setprop_string(fdt, chosen, "bootargs",
            initrd_size ? rescue_args : production_args)) return E_FDT;
    if (initrd_size) {
        if (fdt_setprop_u64(fdt, chosen, "linux,initrd-start", INITRD_LOAD) ||
            fdt_setprop_u64(fdt, chosen, "linux,initrd-end", INITRD_LOAD + initrd_size))
            return E_FDT;
    } else {
        int ret = fdt_delprop(fdt, chosen, "linux,initrd-start");
        if (ret && ret != -FDT_ERR_NOTFOUND) return E_FDT;
        ret = fdt_delprop(fdt, chosen, "linux,initrd-end");
        if (ret && ret != -FDT_ERR_NOTFOUND) return E_FDT;
    }
    return fdt_pack(fdt) ? E_FDT : 0;
}
