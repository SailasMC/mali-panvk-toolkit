#!/usr/bin/env bash
# task-11: 打包 mgl-panvk-v50（真 Android 交换链版）
# 载荷 = 新的 libvulkan_panfrost.so（改名 libvulkan_freedreno.so）+ v49 的 libMobileGL.so + classes.dex
set -e
SRC_SO="${1:-/root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so}"
BT=$(ls -d /opt/android-sdk/build-tools/* | sort -V | tail -1)
JAR=/opt/android-sdk/platforms/android-34/android.jar
W=/root/v50
rm -rf $W/base.apk $W/aligned.apk
mkdir -p $W/lib/arm64-v8a
cp -f "$SRC_SO" $W/lib/arm64-v8a/libvulkan_freedreno.so
cp -f /root/v49/lib/arm64-v8a/libMobileGL.so $W/lib/arm64-v8a/libMobileGL.so
cp -f /root/v49/classes.dex $W/classes.dex
cat > $W/AndroidManifest.xml <<'M'
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.dsh.plugin.driver.g720" android:versionCode="50" android:versionName="5.0-wsi-patched">
  <uses-sdk android:minSdkVersion="26" android:targetSdkVersion="34" />
  <application android:label="MobileGL Magma + PanVK" android:extractNativeLibs="true">
    <activity android:name="com.dsh.driverplugin.MainActivity" android:exported="true" android:label="MobileGL Magma + PanVK">
      <intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter>
    </activity>
    <meta-data android:name="fclPlugin" android:value="true" />
    <meta-data android:name="des" android:value="MobileGL Magma + PanVK" />
    <meta-data android:name="renderer" android:value="magma_panvk:libMobileGL.so:libMobileGL.so" />
    <meta-data android:name="pojavEnv" android:value="LIBGL_ES=3:POJAV_RENDERER=opengles3:MOBILEGL_BACKEND_TYPE=DirectVulkan:MOBILEGL_ESPRYT_USE_ANGLE=0:MOBILEGL_MAGMA_R11G11B10F_FALLBACK=0:PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent:MESA_DEBUG=1:PANVK_DEBUG=1:PANVK_GRALLOC_NO_FALLBACK=0:PANVK_GRALLOC_NO_INFER_LINEAR=0:MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log" />
    <meta-data android:name="boatEnv" android:value="LIBGL_ES=3:POJAV_RENDERER=opengles3:MOBILEGL_BACKEND_TYPE=DirectVulkan:MOBILEGL_ESPRYT_USE_ANGLE=0:MOBILEGL_MAGMA_R11G11B10F_FALLBACK=0:PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent:MESA_DEBUG=1:PANVK_DEBUG=1:PANVK_GRALLOC_NO_FALLBACK=0:PANVK_GRALLOC_NO_INFER_LINEAR=0:MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log" />
    <meta-data android:name="minMCVer" android:value="1.17" />
    <meta-data android:name="maxMCVer" android:value="" />
  </application>
</manifest>
M
"$BT/aapt2" link -o $W/base.apk --manifest $W/AndroidManifest.xml -I "$JAR" --min-sdk-version 26 --target-sdk-version 34
( cd $W && zip -q -X -r base.apk lib classes.dex )
"$BT/zipalign" -f -p 4 $W/base.apk $W/aligned.apk
"$BT/apksigner" sign --ks /root/dsh-driver.keystore --ks-key-alias dshdriver \
  --ks-pass pass:android --key-pass pass:android --out /root/final/mgl-panvk-v50.apk $W/aligned.apk
echo "OK size=$(stat -c%s /root/final/mgl-panvk-v50.apk) sha256=$(sha256sum /root/final/mgl-panvk-v50.apk | cut -c1-32)"
