#!/usr/bin/env bash
# =============================================================================
#  make_driver_apk.sh —— 把任意一个 Vulkan 驱动 .so 打包成 FCL / ZL2 可识别的
#                       驱动插件 APK（不需要 Gradle，纯 aapt2 + zip + apksigner）
#
#  原理：FCL 与 ZL2 用**同一套**扫描契约（ZL2 的 DriverPluginManager 直接读 fclPlugin）：
#      <meta-data android:name="fclPlugin" android:value="true"/>
#      <meta-data android:name="driver"    android:value="<启动器里显示的名字>"/>
#      驱动本体放在 APK 的 lib/arm64-v8a/libvulkan_<x>.so
#      启动器取 applicationInfo.nativeLibraryDir 当驱动目录，用 dlopen hook 注入。
#   所以 android:extractNativeLibs 必须为 true（否则 .so 不会被解出到 nativeLibraryDir）。
#
#  用法：
#    bash make_driver_apk.sh <驱动.so> <显示名> <包名后缀> [输出.apk]
#  例：
#    bash make_driver_apk.sh libvulkan_panfrost.so "PanVK KRAID (G720)" panvk720
# =============================================================================
set -euo pipefail

SO="${1:?用法: $0 <驱动.so> <显示名> <包名后缀> [输出.apk]}"
NAME="${2:?缺少显示名}"
SUFFIX="${3:?缺少包名后缀（小写字母数字）}"
OUT="${4:-$(pwd)/driver-${SUFFIX}.apk}"

SDK="${ANDROID_SDK_ROOT:-/opt/android-sdk}"
BT="$(ls -d $SDK/build-tools/* 2>/dev/null | sort -V | tail -1)"
JAR="$SDK/platforms/android-34/android.jar"
KS="${KEYSTORE:-/root/dsh-driver.keystore}"
[ -f "$SO" ]  || { echo "✘ 找不到驱动: $SO"; exit 1; }
[ -n "$BT" ]  || { echo "✘ 找不到 build-tools（$SDK/build-tools）"; exit 1; }
[ -f "$JAR" ] || { echo "✘ 缺少 android.jar：请先 sdkmanager --install 'platforms;android-34'"; exit 1; }
[ -f "$KS" ]  || { echo "✘ 缺少签名密钥 $KS（keytool -genkeypair 生成）"; exit 1; }

AAPT2="$BT/aapt2"; ZIPALIGN="$BT/zipalign"; APKSIGNER="$BT/apksigner"
PKG="com.dsh.plugin.driver.${SUFFIX}"
SONAME="libvulkan_${SUFFIX}.so"

WORK="$(mktemp -d)"
echo "==> [1/6] 工作目录 $WORK"
echo "    驱动   : $SO ($(stat -c%s "$SO") 字节)"
echo "    显示名 : $NAME"
echo "    包名   : $PKG"
echo "    载荷名 : lib/arm64-v8a/$SONAME"

# ---- 1. 驱动文件放进 lib/arm64-v8a/ ----
mkdir -p "$WORK/lib/arm64-v8a"
cp "$SO" "$WORK/lib/arm64-v8a/$SONAME"

# ---- 2. 生成 AndroidManifest.xml ----
#     无任何组件（不显示在桌面、无图标），只带两个 meta-data
cat > "$WORK/AndroidManifest.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="$PKG"
    android:versionCode="1"
    android:versionName="1.0">

    <uses-sdk android:minSdkVersion="26" android:targetSdkVersion="34" />

    <application
        android:label="$NAME"
        android:extractNativeLibs="true"
        android:hasCode="false">

        <meta-data android:name="fclPlugin" android:value="true" />
        <meta-data android:name="driver"    android:value="$NAME" />
    </application>
</manifest>
EOF

# ---- 3. aapt2 link 出基础 APK ----
echo "==> [2/6] aapt2 link"
"$AAPT2" link -o "$WORK/base.apk" \
    --manifest "$WORK/AndroidManifest.xml" \
    -I "$JAR" \
    --min-sdk-version 26 --target-sdk-version 34

# ---- 4. 把驱动塞进 APK ----
echo "==> [3/6] 写入 lib/arm64-v8a/$SONAME"
( cd "$WORK" && zip -q -X -r base.apk lib )

# ---- 5. 对齐 ----
echo "==> [4/6] zipalign -p 4"
"$ZIPALIGN" -f -p 4 "$WORK/base.apk" "$WORK/aligned.apk"

# ---- 6. 签名 ----
echo "==> [5/6] apksigner 签名"
"$APKSIGNER" sign \
    --ks "$KS" --ks-key-alias dshdriver \
    --ks-pass pass:android --key-pass pass:android \
    --out "$OUT" "$WORK/aligned.apk"

echo "==> [6/6] 校验"
"$APKSIGNER" verify --print-certs "$OUT" | head -4 | sed 's/^/    /'
"$AAPT2" dump badging "$OUT" 2>/dev/null | grep -E "^package|fclPlugin" | sed 's/^/    /' || true
echo
echo "    ✔ 产物: $OUT"
echo "      大小: $(stat -c%s "$OUT") 字节"
echo
echo "    安装到手机（手机侧）： adb install -r $(basename "$OUT")  或直接点 APK 安装"
echo "    装完后 FCL/ZL2 的渲染器/驱动列表里会出现「$NAME」"
echo "    注：临时目录 $WORK 保留着，里面 base.apk / aligned.apk 可用于排查"
