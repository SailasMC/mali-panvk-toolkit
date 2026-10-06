#!/usr/bin/env bash
# 免 Gradle 打包：javac → d8 → NDK 原生 → aapt2 link → 装 dex+so → zipalign → apksigner
set -e
BT=/opt/android-sdk/build-tools/35.0.0
PLATFORM=/opt/android-sdk/platforms/android-34/android.jar
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/root/appbuild; rm -rf "$W"; mkdir -p "$W/classes" "$W/dex" "$W/lib/arm64-v8a"
KS=/root/gputest.keystore
NDK=$(ls -d /opt/android-sdk/ndk/*/ | head -1)
CC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"

echo "==> [1/7] javac --release 11"
find "$SRC/src" -name '*.java' > "$W/srcs.txt"
javac -nowarn --release 11 -d "$W/classes" -cp "$PLATFORM" @"$W/srcs.txt" 2>&1 | grep -v "^Note:" || true
[ -d "$W/classes/com" ] || { echo "X 编译失败"; exit 1; }
echo "    OK: $(find "$W/classes" -name '*.class' | wc -l) 个 class"

echo "==> [2/7] d8 -> classes.dex"
find "$W/classes" -name '*.class' > "$W/classes.txt"
"$BT/d8" --lib "$PLATFORM" --min-api 26 --output "$W/dex" @"$W/classes.txt" 2>&1 | tail -1
[ -f "$W/dex/classes.dex" ] || { echo "X d8 失败"; exit 1; }
echo "    OK: classes.dex $(stat -c%s "$W/dex/classes.dex") 字节"

echo "==> [3/7] NDK 编译原生模块"
if [ -f "$SRC/jni/gputest.c" ]; then
  "$CC" -shared -O2 -fPIC -DVK_NO_PROTOTYPES -o "$W/lib/arm64-v8a/libgputest.so" \
        "$SRC/jni/gputest.c" -llog -ldl 2>&1 | head -14
  [ -f "$W/lib/arm64-v8a/libgputest.so" ] || { echo "X 原生编译失败"; exit 1; }
  echo "    OK: libgputest.so $(stat -c%s "$W/lib/arm64-v8a/libgputest.so") 字节"
else
  echo "    (跳过：无 jni/gputest.c)"; exit 1
fi

echo "==> [4/7] aapt2 link"
"$BT/aapt2" link -o "$W/base.apk" -I "$PLATFORM" --manifest "$SRC/AndroidManifest.xml"
echo "    OK: base.apk $(stat -c%s "$W/base.apk") 字节"

echo "==> [5/7] 装入 classes.dex + lib/arm64-v8a/libgputest.so"
cd "$W" && python3 - <<'PY'
import zipfile, shutil, os
shutil.copy("base.apk", "unsigned.apk")
z = zipfile.ZipFile("unsigned.apk", 'a', zipfile.ZIP_DEFLATED)
z.write("dex/classes.dex", "classes.dex")
so = "lib/arm64-v8a/libgputest.so"
if os.path.exists(so):
    z.write(so, so)
z.close()
zz = zipfile.ZipFile("unsigned.apk")
names = zz.namelist()
print("    包内条目:", [n for n in names if n.endswith('.dex') or n.endswith('.so')])
assert "classes.dex" in names and so in names, "装载不完整"
PY

echo "==> [6/7] zipalign"
"$BT/zipalign" -f -p 4 "$W/unsigned.apk" "$W/aligned.apk"

echo "==> [7/7] 签名"
[ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -alias gputest -keyalg RSA -keysize 2048 \
   -validity 10000 -storepass gputest -keypass gputest \
   -dname "CN=GPU Test, OU=dev, O=dsh, L=., S=., C=CN" >/dev/null 2>&1
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:gputest --key-pass pass:gputest \
  --out "$W/GPUTest-0.1.apk" "$W/aligned.apk" 2>/dev/null
"$BT/apksigner" verify "$W/GPUTest-0.1.apk" && echo "    OK: 签名校验通过"
python3 - <<'PY'
import zipfile, os
p = "/root/appbuild/GPUTest-0.1.apk"
z = zipfile.ZipFile(p)
print("    最终产物:", os.path.getsize(p), "字节")
for n in z.namelist():
    if n.endswith(('.so', '.dex')) or n == "AndroidManifest.xml":
        print("      -", n, z.getinfo(n).file_size, "字节")
PY
cp -f "$W/GPUTest-0.1.apk" /data/dsh_downloads/ && echo "    OK: 已放到 HTTP 分发目录"
