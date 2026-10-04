#!/usr/bin/env bash
# deps-debian.sh — 构建 PanVK（kbase 后端 / Android 目标）所需的全部 Debian 依赖
#
# 这份清单是实测踩坑的产物：下面每一项都对应一次真实的构建失败，
# 详细因果见 ../docs/04-build-g720-panvk.md 第 4 节。
set -euo pipefail

if [ "$(id -u)" != "0" ]; then
  echo "需要 root（或 sudo）执行 apt。" >&2; exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq

apt-get install -y --no-install-recommends \
  git ninja-build meson pkg-config ccache \
  python3 python3-pip python3-mako python3-yaml python3-setuptools \
  bison flex gettext g++ make patch zip unzip file \
  clang llvm-dev llvm-19-dev libllvm19 libclang-19-dev libclang-cpp19-dev \
  libllvmspirvlib-19-dev spirv-tools glslang-tools \
  libdrm-dev libudev-dev libexpat1-dev libzstd-dev zlib1g-dev \
  libx11-dev libxext-dev libxrandr-dev libxfixes-dev libx11-xcb-dev \
  libxcb1-dev libxcb-randr0-dev libxcb-shm0-dev libxcb-glx0-dev \
  libxcb-present-dev libxcb-sync-dev libxcb-dri3-dev libxcb-xfixes0-dev \
  libxshmfence-dev

# --- 坑 5：Debian 把 xf86drm.h 放在 /usr/include，而 NDK 交叉编译器看不到宿主 /usr/include。
#     构建脚本只加了 -I/usr/include/libdrm，所以做软链补上。
if [ -f /usr/include/xf86drm.h ] && [ ! -e /usr/include/libdrm/xf86drm.h ]; then
  ln -sf ../xf86drm.h /usr/include/libdrm/xf86drm.h
  echo "  ✔ 链接 /usr/include/libdrm/xf86drm.h"
fi
if [ -f /usr/include/xf86drmMode.h ] && [ ! -e /usr/include/libdrm/xf86drmMode.h ]; then
  ln -sf ../xf86drmMode.h /usr/include/libdrm/xf86drmMode.h
  echo "  ✔ 链接 /usr/include/libdrm/xf86drmMode.h"
fi

# --- 坑 2：部分发行版的 llvm-config 会因缺可选静态库而报 "missing:"，
#     导致 meson 判定 LLVM 不可用。缺哪个就补一个空归档。
LLVMLIB="$(ls -d /usr/lib/llvm-*/lib 2>/dev/null | sort -V | tail -1)"
if [ -n "$LLVMLIB" ] && command -v llvm-config >/dev/null; then
  llvm-config --shared-mode >/dev/null 2>&1 || {
    echo "  llvm-config 报缺库，尝试补空归档…"
    llvm-config --libs core 2>&1 | grep -o 'missing: [^ ]*' | sed 's/missing: //' | while read -r f; do
      ar rcs "$f" && echo "    + $f"
    done
  }
fi

echo
echo "✔ 依赖安装完成。接着："
echo "     export ANDROID_NDK_ROOT=/opt/android-ndk-r27c"
echo "     bash build_g720.sh"
