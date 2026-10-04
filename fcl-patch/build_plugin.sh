#!/usr/bin/env bash
# build_plugin.sh — 把「拆掉 Adreno 厂商锁」的补丁打包成启动器原生库插件
#
# 原理见 ../docs/03-fcl-adreno-lock.md：
#   启动器的 java.library.path 里，插件目录排在启动器自身库目录之前，
#   而它用 System.loadLibrary("pojavexec") 加载 libpojavexec.so ——
#   所以把我们打过补丁的副本放进插件，JVM 就会加载这一份。
#
# 用法:
#   build_plugin.sh <libpojavexec.so> <checkAdrenoGraphics偏移> [包名] [keystore] [alias] [ks密码]
#
# 例:
#   build_plugin.sh ./libpojavexec.so 0x9dac \
#       com.example.mali.fclpatch ~/.android/debug.keystore android android
set -euo pipefail

LIB="${1:?用法: build_plugin.sh <lib.so> <偏移> [包名] [keystore] [alias] [密码]}"
OFF="${2:?需要 checkAdrenoGraphics 的文件偏移，例如 0x9dac}"
PKG="${3:-com.example.mali.fclpatch}"
KS="${4:-}"
ALIAS="${5:-android}"
KSPASS="${6:-android}"
LABEL="${LABEL:-PanVK Unlock}"

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="${OUT:-$PWD/out}"
mkdir -p "$OUT"

# ---- 工具链定位（Android SDK build-tools）----
BT="${BUILD_TOOLS:-}"
if [ -z "$BT" ]; then
  for c in "${ANDROID_HOME:-/opt/android-sdk}/build-tools"/*; do
    [ -x "$c/aapt2" ] && BT="$c"
  done
fi
[ -n "$BT" ] && [ -x "$BT/aapt2" ] || {
  echo "找不到 aapt2。请设置 BUILD_TOOLS=<build-tools 目录> 或 ANDROID_HOME。" >&2; exit 1; }
ANDROID_JAR="$(ls "${ANDROID_HOME:-/opt/android-sdk}"/platforms/android-*/android.jar 2>/dev/null | sort -V | tail -1)"
[ -n "$ANDROID_JAR" ] || { echo "找不到 android.jar" >&2; exit 1; }
echo "  build-tools : $BT"
echo "  android.jar : $ANDROID_JAR"

# ---- 1) 打字节补丁 ----
PATCHED="$OUT/libpojavexec.patched.so"
python3 - "$LIB" "$OFF" "$PATCHED" <<'PY'
import sys
lib, off, out = sys.argv[1], int(sys.argv[2], 0), sys.argv[3]
d = bytearray(open(lib, 'rb').read())
orig = bytes(d[off:off+8])
# mov w0, #1 ; ret   —— 让 checkAdrenoGraphics() 恒返回 true
patch = bytes([0x20, 0x00, 0x80, 0x52, 0xC0, 0x03, 0x5F, 0xD6])
print(f"  偏移 0x{off:x} 原字节: {orig.hex(' ')}")
if orig == patch:
    print("  （已经是补丁版）")
d[off:off+8] = patch
open(out, 'wb').write(d)
print(f"  补丁后      : {patch.hex(' ')}   → mov w0,#1 ; ret")
PY
file "$PATCHED" | sed 's/^/  /' || true

# ---- 2) 生成清单 ----
W="$OUT/work"; rm -rf "$W" 2>/dev/null || true
mkdir -p "$W/lib/arm64-v8a"
cp "$PATCHED" "$W/lib/arm64-v8a/libpojavexec.so"
cp "$HERE/stub_classes.dex" "$W/classes.dex"      # 提供一个 LAUNCHER Activity，启动器才认这个插件

cat > "$W/AndroidManifest.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="$PKG" android:versionCode="1" android:versionName="1.0">
  <uses-sdk android:minSdkVersion="26" android:targetSdkVersion="34" />
  <application android:label="$LABEL" android:extractNativeLibs="true">
    <activity android:name="com.dsh.driverplugin.MainActivity"
              android:exported="true" android:label="$LABEL">
      <intent-filter>
        <action android:name="android.intent.action.MAIN" />
        <category android:name="android.intent.category.LAUNCHER" />
      </intent-filter>
    </activity>
    <!-- 启动器靠这两个 meta-data 把它认成「原生库插件」 -->
    <meta-data android:name="FCLNativePlugin" android:value="true" />
    <!-- 注意：这里的值是 JVM 参数，不是环境变量！必须写成 -Dname=value -->
    <meta-data android:name="environment" android:value="-Dmali.fclpatch=1" />
  </application>
</manifest>
EOF

# ---- 3) 组包 ----
APK="$OUT/fcl-adreno-unlock.apk"
"$BT/aapt2" link -o "$W/base.apk" --manifest "$W/AndroidManifest.xml" \
    -I "$ANDROID_JAR" --min-sdk-version 26 --target-sdk-version 34 >/dev/null
( cd "$W" && zip -q -X -r base.apk lib classes.dex )
"$BT/zipalign" -f -p 4 "$W/base.apk" "$W/aligned.apk"

if [ -n "$KS" ] && [ -f "$KS" ]; then
  "$BT/apksigner" sign --ks "$KS" --ks-key-alias "$ALIAS" \
      --ks-pass "pass:$KSPASS" --key-pass "pass:$KSPASS" \
      --out "$APK" "$W/aligned.apk"
else
  echo "  ⚠️ 未提供 keystore，输出未签名 APK（安装前需自行签名）" >&2
  cp "$W/aligned.apk" "$APK"
fi
echo
echo "✔ 产物: $APK  ($(stat -c%s "$APK" 2>/dev/null || stat -f%z "$APK") 字节)"
echo "  安装后在启动器里启动游戏，日志应出现："
echo "    AdrenoSupp: Loaded Turnip, loader address: 0x..."
echo "    WARNING: panvk is not a conformant Vulkan implementation, testing use only."
