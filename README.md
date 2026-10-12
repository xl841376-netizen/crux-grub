# crux GRUB 引导层（Xiaomi Mi 9 Pro 5G / SM8150）

crux（小米 9 Pro 5G，SM8150 / 骁龙 855+）主线 Linux 移植所用的 **GRUB 引导层**：一个内嵌配置的 aarch64-efi standalone 引导器 + 自写的显示交接模块。

## 引导链

```
boot 分区：Mu/Silicium UEFI 固件（BootShim + SILICIUM_UEFI.fd）   ← 见 fork: Mu-Silicium
      ↓
ESP:\EFI\BOOT\BOOTAA64.EFI         ← 本仓库产物（GRUB standalone）
ESP:\EFI\crux\grub.cfg             ← 菜单（改它不必重做 .efi）
      ↓
linux /EFI/crux/Image* + initrd + devicetree（sm8150-xiaomi-crux-73.dtb）
      ↓
Ubuntu（root=UUID=…）
```

分工：**Mu/Silicium 固件**负责 SoC/内存/显示控制器初始化与 BootShim；**GRUB**只负责挑内核、传 dtb/initramfs、并把显示电路按内核那套先"重放"好再交接。

## 目录

| 路径 | 说明 |
|---|---|
| `src/cruxdisplay.c` | 自写 GRUB 模块：在交接前把 DSI/DPU 显示电路按内核驱动的初始化序列重放，内核 probe 时校验，对上就直接用。交接时**不往屏幕打任何字符**（避免盖住切换现场），证据写进 GOP 帧缓冲尾部信箱，内核侧用 `/sys/kernel/debug/crux_te_sync/gop` 读回核对 |
| `src/apply-to-grub.sh` | 把 `cruxdisplay` 模块挂进 GRUB 源码树（追加到 `grub-core/Makefile.core.def` 末尾 + 拷贝 `cruxdisplay.c`） |
| `boot/grub.cfg` | 现役菜单（ESP 上为 `/EFI/crux/grub.cfg`）：`crux73te`（默认）/ `crux73old`（回退）/ 6.17 旧内核 / Windows(WoA) 链式引导 |
| `boot/embedded.cfg` | 编译进 `BOOTAA64.EFI` 的引导配置：只做 `search --file /EFI/crux/grub.cfg` + `source`，所以**换菜单不用重做 .efi** |
| `build.sh` | 重建 `BOOTAA64.EFI` + `cruxdisplay.mod` 的完整步骤 |
| `prebuilt/BOOTAA64.efi` | 现役引导器（2026-09-27 构建，7049216 字节） |
| `prebuilt/cruxdisplay.mod` | 现役 `cruxdisplay` 模块（7960 字节，放在 ESP `/EFI/crux/`，由菜单 `insmod` 载入） |

## 部署

ESP = GPT 分区 `PARTNAME=efi`（本机 `sda35`，512M）。把文件放好即可：

```bash
cp prebuilt/BOOTAA64.efi        <ESP>/EFI/BOOT/BOOTAA64.EFI
cp prebuilt/cruxdisplay.mod     <ESP>/EFI/crux/cruxdisplay.mod
cp boot/grub.cfg                <ESP>/EFI/crux/grub.cfg
```

grub.cfg 里关键参数（crux）：

- 显示/时钟：`clk_ignore_unused pd_ignore_unused deferred_probe_timeout=-1`
- 无调频域盲区：`cfg80211.ieee80211_regdom=CN`
- 根：`root=UUID=ed36cd26-63e2-4480-97b6-1742f885cdcf rootwait`
- 日志：`loglevel=5 vt.global_cursor_default=0`，WiFi 诊断 `ath10k_core.debug_mask=0x202a`

## 构建

环境：aarch64 Linux（本移植在设备/容器内原生构建），依赖 `gcc bison flex python3 gettext`。

```bash
GRUB_SRC=/root/grub-2.14 ./build.sh
```

要点（与现役产物一致）：

```bash
./configure --target=aarch64-linux-gnu --with-platform=efi --disable-nls --disable-werror
make -j$(nproc)
./grub-mkstandalone -O arm64-efi \
  --modules="all_video boot cat chain configfile devicetree echo efi_gop ext2 fat font gfxmenu gfxterm halt linux loadenv minicmd normal part_gpt part_msdos read reboot search search_fs_file search_fs_uuid serial sleep terminal test videoinfo" \
  -o BOOTAA64.efi "boot/grub/grub.cfg=embedded.cfg"
```

> `devicetree` 命令用于把 dtb 交给内核；`cruxdisplay` 模块**不内嵌**，单独放 ESP 由菜单 `insmod`。

## 说明

- GRUB 版本：GNU GRUB **2.14**（上游源码 + `cruxdisplay` 模块）。
- 本仓库只放 crux 相关的改动与产物；GRUB 上游代码请取 <https://ftp.gnu.org/gnu/grub/>。
- 与内核移植的关系：内核见 fork `xl841376-netizen/linux` 分支 `crux-sm8150-73`。
