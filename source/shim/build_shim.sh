#!/usr/bin/env bash
# build_shim.sh —— 生成 + 编译 libvkpanvk_shim.so，并自检导出符号覆盖度
set -euo pipefail

NDK=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64
CC=$NDK/bin/aarch64-linux-android28-clang
W=/root/shimgen
mkdir -p "$W/build"

echo "=========== 1) 生成转发代码 ==========="
python3 "$W/gen_shim.py"

echo
echo "=========== 2) 编译垫片 ==========="
"$CC" -shared -fPIC -O2 -fvisibility=default \
      -Wall -Wextra -Wno-unused-parameter \
      -o "$W/build/libvkpanvk_shim.so" "$W/shim.c" \
      -I"$NDK/sysroot/usr/include" \
      -Wl,-soname,libvkpanvk_shim.so \
      -Wl,--no-undefined \
      -ldl -llog
echo "编译 OK: $(stat -c%s "$W/build/libvkpanvk_shim.so") bytes"

echo
echo "=========== 3) 导出符号自检 ==========="
readelf -sW --dyn-syms "$W/build/libvkpanvk_shim.so" \
  | awk '$5=="GLOBAL" && $7!="UND" && $8 ~ /^vk/ {print $8}' | sort -u > "$W/build/exported.txt"
echo "垫片导出的 vk* 符号数: $(wc -l < "$W/build/exported.txt")  (期望 125)"
echo "--- MGL 需要但我们没导出的（应为空）---"
comm -23 "$W/vkund.txt" "$W/build/exported.txt" | sed 's/^/  MISSING: /'
echo "--- 我们多导出的（info）---"
comm -13 "$W/vkund.txt" "$W/build/exported.txt" | sed 's/^/  EXTRA: /'

echo
echo "=========== 4) 依赖自检（必须不含 libvulkan.so） ==========="
readelf -dW "$W/build/libvkpanvk_shim.so" | grep -E "NEEDED|SONAME"
if readelf -dW "$W/build/libvkpanvk_shim.so" | grep -q "libvulkan"; then
  echo "!! 垫片竟然依赖 libvulkan —— 失败"; exit 1
fi
echo "OK: 垫片不依赖 libvulkan.so"

echo
echo "sha256: $(sha256sum "$W/build/libvkpanvk_shim.so" | cut -d' ' -f1)"
