# [crux-grub] 薄封装：make = 构建，make clean = 清构建目录
all: build dist

build:
	./build.sh

dist: build

clean:
	rm -rf build/obj dist/*.efi dist/*.sha256
	$(MAKE) -C build/grub-2.14 clean || true

distclean: clean
	rm -rf build

.PHONY: all build dist clean distclean
