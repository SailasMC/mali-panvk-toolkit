#!/usr/bin/env bash
# pack_mgl_plugin.sh <libMobileGL.so> [我们的panvk.so]
set -e
SO="$1"; PANVK="${2:-/root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so}"
BT=$(ls -d /opt/android-sdk/build-tools/* | sort -V | tail -1); JAR=/opt/android-sdk/platforms/android-34/android.jar
W=/root/mglplug; mkdir -p $W/lib/arm64-v8a
cp "$SO" $W/lib/arm64-v8a/libMobileGL.so
cp "$PANVK" $W/lib/arm64-v8a/libvulkan_freedreno.so 2>/dev/null || true
cat > $W/lib/arm64-v8a/panvk_icd.json <<'J'
{ "file_format_version": "1.0.0",
  "ICD": { "library_path": "./libvulkan_freedreno.so", "api_version": "1.4.363" } }
J
cp /root/final/dex/classes.dex $W/classes.dex 2>/dev/null || true
cat > $W/AndroidManifest.xml <<'M'
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.dsh.plugin.driver.g720" android:versionCode="11" android:versionName="1.0">
  <uses-sdk android:minSdkVersion="26" android:targetSdkVersion="34" />
  <application android:label="MobileGL (Vulkan)" android:extractNativeLibs="true">
    <activity android:name="com.dsh.driverplugin.MainActivity" android:exported="true" android:label="MobileGL (Vulkan)">
      <intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter>
    </activity>
    <meta-data android:name="fclPlugin" android:value="true" />
    <meta-data android:name="des" android:value="MobileGL (Vulkan)" />
    <meta-data android:name="renderer" android:value="opengles3:libMobileGL.so:libMobileGL.so" />
    <meta-data android:name="pojavEnv" android:value="MOBILEGL_BACKEND_TYPE=DirectVulkan:VK_ICD_FILENAMES=/storage/emulated/0/mali-icd/panvk_icd.json" />
    <meta-data android:name="minMCVer" android:value="1.20" />
    <meta-data android:name="maxMCVer" android:value="26.99" />
  </application>
</manifest>
M
"$BT/aapt2" link -o $W/base.apk --manifest $W/AndroidManifest.xml -I "$JAR" --min-sdk-version 26 --target-sdk-version 34
( cd $W && zip -q -X -r base.apk lib classes.dex )
"$BT/zipalign" -f -p 4 $W/base.apk $W/aligned.apk
"$BT/apksigner" sign --ks /root/dsh-driver.keystore --ks-key-alias dshdriver \
  --ks-pass pass:android --key-pass pass:android --out /root/final/mobilegl-renderer.apk $W/aligned.apk
echo "OK $(stat -c%s /root/final/mobilegl-renderer.apk) $(sha256sum /root/final/mobilegl-renderer.apk | cut -c1-16)"
