# X20 Debian 最小引导器

仅面向固定的 2 GiB X20-96：eMMC MSDC0 USER LBA `0x4000` 读取不超过 8 MiB
的 v1 bundle，解压 ARM64 Linux，设置 FDT，经现有 BL31 `0x82000200` 进入内核。
不读取 TF 上的内核，不解析 Preloader tag，不带 Android、USB/Fastboot、显示、
菜单、存储写入或其他板型。代码随树完整保存，构建不联网、不依赖旧 LK 源码；
复用库的版权及许可证声明保留在源码和 LICENSE 中。

只有三个独立固件目标，默认 `emmc`，全部配置写在代码中：

| BUILD / CMake target | Linux 根目录 | 产物目录 |
| --- | --- | --- |
| emmc（默认） | 现有 eMMC Debian | out/build-lk/emmc/ |
| tf | 固定 TF Debian GPT 分区 | out/build-lk/tf/ |
| rescue | UART 下载的独立 Debian initramfs | out/build-lk/rescue/ |

```sh
sh mainline/x20-sbc/tools/build-x20-lk.sh
BUILD=tf sh mainline/x20-sbc/tools/build-x20-lk.sh
BUILD=rescue sh mainline/x20-sbc/tools/build-x20-lk.sh
# 等价 CMake 入口；默认构建只产生 emmc 固件
cmake -S mainline/lk -B out/build-lk -G Ninja
cmake --build out/build-lk --target emmc
cmake --build out/build-lk --target tf
cmake --build out/build-lk --target rescue
```

依赖已安装的 CMake、Ninja、Python 3、`arm-linux-gnueabi-` GCC；不需要 ARM libc。
每个目标输出 `lk.elf`、`lk.map`、`lk.raw`、`lk.bin` 和部署校验用的 `lk.json`。
无 ROOT/MODE/诊断组合参数，无上游仓库、来源清单或旧 Make 依赖。

根分区写死在 `app/x20_boot/fdt.c`：eMMC PARTUUID 为
`8273e1ab-846f-4468-b999-ee2ea8e50a16`；TF 为
`f20d0001-6797-4000-8000-000000000001`。TF Debian 安装时使用该 GPT 分区 GUID；
构建不会格式化当前 TF 测试盘或安装系统。不依赖易变化的 mmcblk 编号。

bundle 只接受 kernel、dtb、initramfs 三种条目；地址固定，写目标内存前检查全部
CRC、长度、重叠和地址。兼容现存 Image.gz-dtb；独立 DTB 则通过明确条目提供。
gzip 按实际结束位置拆分，禁止扫描猜测格式。

```sh
python3 mainline/lk/tools/bundle.py pack --kernel Image.gz --dtb x20.dtb --output boot.bundle
python3 mainline/lk/tools/bundle.py verify boot.bundle
python3 mainline/lk/tools/rescue-initramfs.py --rootfs out/debian-headless-rootfs --output rescue.cpio.gz
python3 mainline/lk/tools/bundle.py pack --kernel Image.gz-dtb --initramfs rescue.cpio.gz --output rescue.bundle
```

`emmc`/`tf` 在启动错误、`reboot bootloader` 的 RTC 标记或启动窗口收到 `r` 时进入
UART 救援；`rescue` 无条件进入。V2 协议支持有界超时、CRC、重传、重复包及取消。
UART 下载启动强制要求 initramfs。RAM shell 只挂载 devtmpfs/proc/sysfs，不自动挂载
MMC 或写存储。停止串口采集后，使用：

```sh
python3 mainline/lk/tools/uart-rescue.py --port /dev/ttyACM0 --cancel
python3 mainline/lk/tools/uart-rescue.py --port /dev/ttyACM0 --bundle rescue.bundle --boot
```

`tools/deploy.py --kind lk --image out/build-lk/emmc/lk.bin` 校验产物后，运行时发现
带 boot0 的 eMMC，核对固定 CID、lk 分区名和尺寸，写后 sync、回读 SHA；拒绝 rescue
固件常驻。`--kind bundle` 还检查 raw 范围与 GPT 分区不重叠。`--check-only` 仅做本地
校验。构建不部署，部署不自动重启。

主机测试独立于三个固件目标：

```sh
cmake -S mainline/lk/tests -B out/test-lk -G Ninja
cmake --build out/test-lk
ctest --test-dir out/test-lk --output-on-failure
```

实板验收、当前入口和回退位置见工作区 `docs/PLAN-PROGRESS.md`。
