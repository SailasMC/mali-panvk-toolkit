#!/usr/bin/env bash
# build_g720.sh — 一键构建 G720/v12 的 PanVK（kbase 后端 + Android WSI）
#
# 封装了 zenithblue 基线 + 本仓库实测踩坑的全部修正：
#   · 交叉文件里补 X11 头文件路径（NDK 看不到宿主 /usr/include）
#   · 把 spirv-tools 等宿主头目录软链进 deps include
#   · 强制使用新的构建目录（meson --reconfigure 不会重读交叉文件）
#
# 用法:
#   export ANDROID_NDK_ROOT=/opt/android-ndk-r27c
#   bash build_g720.sh [工作目录，默认 /opt/panvk-build]
set -euo pipefail

WORK="${1:-/opt/panvk-build}"
PROFILE="${PROFILE:-g720-v12-csf}"
API="${API:-35}"
NDK="${ANDROID_NDK_ROOT:-/opt/android-ndk-r27c}"
REPO_URL="https://github.com/zenithblue-oss/panvk-kbase-android.git"
SRC="$WORK/panvk-kbase-android"

mkdir -p "$WORK"
[ -d "$SRC/.git" ] || git clone --depth 1 "$REPO_URL" "$SRC"
cd "$SRC"

echo "==> 1/4 拉取并 pin 住 Mesa（按 sources.lock）"
./scripts/fetch-mesa.sh

echo "==> 2/4 构建主机侧工具"
./scripts/bootstrap-host-tools.sh

echo "==> 3/4 应用补丁序列"
# apply-patches.sh 对脏工作区会硬失败（PATCH-DRIFT）。先复位到 pin 的 commit。
MESA_COMMIT="$(python3 -c "import json;print(json.load(open('sources.lock'))['mesaCommit'])")"
git -C work/mesa reset --hard "$MESA_COMMIT" >/dev/null
git -C work/mesa clean -fdq -- src/ meson.build meson.options || true
./scripts/apply-patches.sh --profile "$PROFILE"

echo "==> 4/4 打交叉编译修正 + 构建"
# (a) X11 头：NDK 交叉编译器用自身 sysroot，看不到宿主 /usr/include
INC_DIR="$SRC/work/android-deps-x11/include"
mkdir -p "$INC_DIR"
#     把准备脚本拷出来的头补全（build-android.sh 只在 deps 目录存在时才做这步）
[ -f "$INC_DIR/X11/Xlib.h" ] || ./scripts/prepare-x11-headers.sh "$SRC/work/android-deps-x11" >&2

# (b) 其它宿主头目录（SPIR-V 等）软链进同一个 include 目录
for h in spirv-tools spirv vulkan; do
  [ -d "/usr/include/$h" ] && [ ! -e "$INC_DIR/$h" ] && ln -s "/usr/include/$h" "$INC_DIR/$h" || true
done

# (c) 把该 include 目录写进交叉文件模板的 c_args
python3 - "$SRC/meson/android-aarch64.ini" "$INC_DIR" <<'PY'
import sys
p, inc = sys.argv[1], sys.argv[2]
t = open(p).read()
if f"-I{inc}" in t:
    print("    c_args 已包含 deps include")
else:
    assert "c_args = []" in t, "交叉文件模板结构变了，请人工检查"
    t = t.replace("c_args = []", f"c_args = ['-I{inc}']", 1)
    open(p, 'w').write(t)
    print(f"    c_args = ['-I{inc}']")
PY

# (d) 用新的构建目录，避开 meson 的交叉文件缓存
export BDIR="$SRC/build/android-v$(date +%s)"
export DDIR="$SRC/dist/android-$PROFILE"
./scripts/build-android.sh --profile "$PROFILE" --api "$API" --ndk "$NDK"

SO="$(find "$DDIR" -name 'libvulkan_panfrost.so' | head -1)"
echo
echo "✔ 产物: $SO"
[ -n "$SO" ] && cp "$SO" "$WORK/libvulkan_panfrost-$PROFILE.so" && \
  echo "  已复制到: $WORK/libvulkan_panfrost-$PROFILE.so"
echo
echo "下一步：用 ../tools/vkicd_probe.c 验证，再打包成启动器驱动插件（../tools/make_driver_apk.sh）。"
