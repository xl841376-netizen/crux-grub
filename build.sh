#!/bin/bash
# [crux] 重建 GRUB standalone 引导器（aarch64-efi）+ cruxdisplay 模块
# 环境：aarch64 Linux（本移植在设备/容器内原生构建），依赖 gcc/bison/flex/python3/gettext
# 用法：GRUB_SRC=/root/grub-2.14 ./build.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
GRUB_SRC=${GRUB_SRC:-/root/grub-2.14}
[ -d "$GRUB_SRC" ] || { echo "请先准备 GRUB 源码（grub-2.14）到 $GRUB_SRC"; exit 1; }

# 1) 把 cruxdisplay 模块挂进源码树
"$HERE/src/apply-to-grub.sh" "$GRUB_SRC"

cd "$GRUB_SRC"
# 2) 配置（与现役产物一致）
[ -f Makefile ] || ./configure --target=aarch64-linux-gnu --with-platform=efi --disable-nls --disable-werror
# 3) 编译
make -j"$(nproc)"
# 4) 生成模块 + standalone EFI（内嵌 boot/grub/grub.cfg，用来自动 source ESP 上的菜单）
./grub-mkstandalone -O arm64-efi \
  --modules="all_video boot cat chain configfile devicetree echo efi_gop ext2 fat font gfxmenu gfxterm halt linux loadenv minicmd normal part_gpt part_msdos read reboot search search_fs_file search_fs_uuid serial sleep terminal test videoinfo" \
  -o "$HERE/prebuilt/BOOTAA64.efi" \
  "boot/grub/grub.cfg=$HERE/boot/embedded.cfg"
cp -v grub-core/cruxdisplay.mod "$HERE/prebuilt/cruxdisplay.mod"
echo "产物：$HERE/prebuilt/BOOTAA64.efi  $HERE/prebuilt/cruxdisplay.mod"
