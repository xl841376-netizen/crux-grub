# crux-grub —— Xiaomi Mi 9 Pro 5G（crux / SM8150）GRUB 引导层

aarch64-efi GRUB standalone 构建仓库：`cruxdisplay` 模块以 builtin 形式编进镜像，产物为单个 `BOOTAA64.EFI`，ESP 上不需要额外 `.mod`，菜单里也不需要 `insmod` 外部文件。

## 引导链

```
boot 分区  Mu/Silicium UEFI 固件（BootShim + SILICIUM_UEFI.fd）      ← 见仓库 Mu-Silicium（crux-port-next）
     ↓
ESP:/EFI/BOOT/BOOTAA64.EFI   ← 本仓库产物（GRUB standalone，含内建 cruxdisplay）
ESP:/EFI/crux/grub.cfg       ← 菜单（可选；有就用它，换菜单不必重做 .efi）
     ↓
linux /EFI/crux/Image* + initrd + sm8150-xiaomi-crux-73.dtb
     ↓
Ubuntu（root=UUID=…）
```

固件负责 SoC/内存/显示控制器初始化与 BootShim；GRUB 负责选内核、传 dtb/initramfs，并在交接前重放 DSI/DPU 显示初始化。

## 构建

```bash
./build.sh          # 首次自动下载 grub-2.14 源码到 build/
GRUB_SRC=/root/grub-2.14 ./build.sh    # 用现成源码
```

产物：

| 文件 | 说明 |
|---|---|
| `dist/BOOTAA64.efi` | 引导器（`cruxdisplay` 内建，内嵌配置与菜单） |
| `dist/BOOTAA64.efi.sha256` | 校验值 |
| `prebuilt/BOOTAA64.efi` | 仓库内产物副本（CI 更新） |

依赖：aarch64 工具链（原生 aarch64，或 x86_64 上 `gcc-aarch64-linux-gnu` 交叉编译）、`make bison flex python3 gettext curl patch xz-utils`。

`build.sh` 的动作：

1. 取 `grub-2.14` 源码（`GRUB_SRC` 或自动下载解包到 `build/`）；
2. 应用 `patches/0001-add-cruxdisplay-module.patch`（把 `cruxdisplay` 声明进 `grub-core/Makefile.core.def`）并放入 `src/cruxdisplay.c`；
3. `./configure --target=aarch64-linux-gnu --with-platform=efi --disable-nls --disable-werror` → `make`；
4. `grub-mkstandalone -O arm64-efi --modules="… cruxdisplay"` 把 `cruxdisplay` 预加载进镜像，并内嵌 `boot/embedded.cfg`（运行时配置）与 `boot/grub.cfg`（ESP 无菜单时的兜底）。

## 目录

| 路径 | 说明 |
|---|---|
| `src/cruxdisplay.c` | GRUB 模块：交接前按内核驱动的初始化序列重放 DSI/DPU 电路；交接时不往屏幕打字符，证据写进 GOP 帧缓冲尾部信箱，内核侧 `/sys/kernel/debug/crux_te_sync/gop` 读回核对 |
| `patches/0001-add-cruxdisplay-module.patch` | 把模块声明进 `grub-core/Makefile.core.def` |
| `boot/embedded.cfg` | 编进 EFI 的运行时配置：`insmod cruxdisplay`（内建，幂等）→ 找 `/EFI/crux/grub.cfg` → 有则 source，无则用内嵌菜单 |
| `boot/grub.cfg` | 菜单（部署为 ESP `/EFI/crux/grub.cfg`；同时内嵌为兜底菜单） |
| `build.sh` / `Makefile` | 构建脚本 |
| `prebuilt/BOOTAA64.efi` | 产物副本（云端 CI 更新） |
| `.github/workflows/build.yml` | 云端编译：push 即构建，更新 release `crux-grub-latest` |

## 部署

ESP = GPT 分区 `PARTNAME=efi`（本机 `/dev/sda35`，512M）。

```bash
cp prebuilt/BOOTAA64.efi  <ESP>/EFI/BOOT/BOOTAA64.EFI
cp boot/grub.cfg          <ESP>/EFI/crux/grub.cfg      # 可选：改菜单不必重做 .efi
```

早期版本的 `cruxdisplay.mod` 单独放 ESP 并 `insmod (<路径>)/cruxdisplay.mod`；现版本模块已内建，菜单写 `insmod cruxdisplay`（已加载时幂等），旧 `.mod` 可留可删。

crux 内核参数：`clk_ignore_unused pd_ignore_unused deferred_probe_timeout=-1`、`cfg80211.ieee80211_regdom=CN`、`root=UUID=ed36cd26-63e2-4480-97b6-1742f885cdcf rootwait`、`loglevel=5 vt.global_cursor_default=0`、`ath10k_core.debug_mask=0x202a`。

## 云端构建

push 到 `main` 或手动 `workflow_dispatch` 即云端交叉编译，成功后更新 release `crux-grub-latest`（含 `BOOTAA64.efi` 与 sha256）：

```bash
gh release download crux-grub-latest --repo xl841376-netizen/crux-grub -p 'BOOTAA64*' -D .
```
