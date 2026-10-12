#!/bin/bash
# [crux-grub] 一条命令构建“可直接使用”的 aarch64-efi GRUB 引导器：
#   · 自动获取 GRUB 源码（或用 GRUB_SRC 指定现成的）
#   · cruxdisplay 以“内建模块”形式编进镜像 —— ESP 上不需要额外的 .mod，也不需要 insmod 外部文件
#   · 内嵌运行配置与菜单：ESP 上有 /EFI/crux/grub.cfg 就优先用它（换菜单不必重做 .efi），没有就用镜像里的菜单
#
# 用法： ./build.sh          GRUB_SRC=/path/to/grub-2.14 ./build.sh
# 依赖： aarch64 工具链（原生 aarch64，或 x86_64 上 gcc-aarch64-linux-gnu）、make bison flex python3 gettext curl patch xz-utils
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
VER=${GRUB_VER:-2.14}
JOBS=${JOBS:-$(nproc)}
SRC=${GRUB_SRC:-$HERE/build/grub-$VER}
OUT=$HERE/dist
MODNAME=cruxdisplay
MODULES="all_video boot cat chain configfile echo efi_gop ext2 fat font fdt gfxmenu gfxterm halt linux loadenv minicmd normal part_gpt part_msdos read reboot search search_fs_file search_fs_uuid serial sleep terminal test videoinfo $MODNAME"

# ---------- 1) 源码 ----------
if [ ! -d "$SRC" ]; then
  echo "[1/6] 获取 GRUB $VER 源码"
  mkdir -p "$HERE/build"
  TGZ="$HERE/build/grub-$VER.tar.xz"
  [ -s "$TGZ" ] || curl -fL --retry 3 -o "$TGZ" "https://ftp.gnu.org/gnu/grub/grub-$VER.tar.xz"
  tar -C "$HERE/build" -xf "$TGZ"
else
  echo "[1/6] 使用已有源码 $SRC"
fi

# ---------- 2) 注入模块源码与声明 ----------
echo "[2/6] 注入 $MODNAME"
patch -d "$SRC" -p1 --forward --batch < "$HERE/patches/0001-add-cruxdisplay-module.patch" >/dev/null 2>&1 || true
cp "$HERE/src/cruxdisplay.c" "$SRC/grub-core/$MODNAME.c"
grep -q "name = $MODNAME;" "$SRC/grub-core/Makefile.core.def" || { echo "补丁未生效：Makefile.core.def 里没有 $MODNAME"; exit 1; }
# tarball 里 Makefile.core.am 是预生成的（automake 版本与系统不匹配，重建会失败），
# 这里只把 .def 的时间戳压回去避免触发 autotools，模块本身在下面第 5 步用 GRUB 官方 genmod.sh 单独编
touch -r "$SRC/configure.ac" "$SRC/grub-core/Makefile.core.def" 2>/dev/null || touch "$SRC/grub-core/Makefile.core.def"

# ---------- 3) 配置 ----------
if [ ! -f "$SRC/Makefile" ]; then
  echo "[3/6] configure"
  ( cd "$SRC" && ./configure --target=aarch64-linux-gnu --with-platform=efi --disable-nls --disable-werror )
else
  echo "[3/6] 已配置，跳过 configure"
fi

# ---------- 4) 编译 GRUB ----------
echo "[4/6] 编译（-j$JOBS）"
make -C "$SRC" -j"$JOBS" >/dev/null

# ---------- 5) 编 cruxdisplay 模块（GRUB 官方 genmod.sh 流程，产出标准 .mod）----------
echo "[5/6] 编 $MODNAME.mod"
(
  cd "$SRC/grub-core"
  . ./modinfo.sh
  CPP="${grub_target_cppflags//'$(top_srcdir)'/$SRC}"
  CPP="${CPP//'$(top_builddir)'/$SRC}"
  # 与 GRUB 官方模块编译同参（含 -DHAVE_CONFIG_H / -DGRUB_FILE / -mcmodel=large）
  "$grub_target_cc" -DHAVE_CONFIG_H -D_FILE_OFFSET_BITS=64 -DGRUB_FILE=\"\" \
      $CPP -I. -I.. -I"$SRC" -I"$SRC/include" -I"$SRC/grub-core/lib/libgcrypt-grub/src" \
      $grub_target_cflags -mcmodel=large -ffreestanding -nostdlib -no-pie -Wl,-r \
      -o "$MODNAME.module" "$MODNAME.c"
  grep -q "^$MODNAME:" moddep.lst || printf '%s: \n' "$MODNAME" >> moddep.lst
  TARGET_OBJ2ELF= sh genmod.sh moddep.lst "$MODNAME.module" build-grub-module-verifier "$MODNAME.mod"
)
ls -l "$SRC/grub-core/$MODNAME.mod"

# ---------- 6) 生成自足镜像 ----------
echo "[6/6] 生成 BOOTAA64.EFI"
mkdir -p "$OUT"
( cd "$SRC" && ./grub-mkstandalone -d grub-core -O arm64-efi --modules="$MODULES" \
    -o "$OUT/BOOTAA64.efi" \
    "boot/grub/grub.cfg=$HERE/boot/embedded.cfg" \
    "boot/grub/crux-menu.cfg=$HERE/boot/grub.cfg" )

mkdir -p "$HERE/prebuilt"
cp "$OUT/BOOTAA64.efi" "$HERE/prebuilt/BOOTAA64.efi"
( cd "$OUT" && sha256sum BOOTAA64.efi > BOOTAA64.efi.sha256 )
echo
echo "产物： $OUT/BOOTAA64.efi  ($(stat -c %s "$OUT/BOOTAA64.efi") 字节)"
echo "$MODNAME 已内建进镜像；ESP 只需要这一个文件"
cat "$OUT/BOOTAA64.efi.sha256"
