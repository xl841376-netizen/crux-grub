#!/bin/bash
# 把 cruxdisplay 模块挂进 GRUB 源码树（追加到 Makefile.core.def 末尾）
set -e
GRUB_SRC=${1:?用法: apply-to-grub.sh <grub源码目录>}
HERE=$(cd "$(dirname "$0")" && pwd)
cp -v "$HERE/cruxdisplay.c" "$GRUB_SRC/grub-core/cruxdisplay.c"
if grep -q "name = cruxdisplay;" "$GRUB_SRC/grub-core/Makefile.core.def"; then
  echo "Makefile.core.def 已含 cruxdisplay"
else
  cat >> "$GRUB_SRC/grub-core/Makefile.core.def" <<'EOD'

module = {
  name = cruxdisplay;
  common = grub-core/cruxdisplay.c;
};
EOD
  echo "已把 cruxdisplay 追加进 Makefile.core.def"
fi
