# fcl-patch —— 拆掉 FCL / Zalith Launcher 的 Adreno 厂商锁

原理与完整分析见 [`../docs/03-fcl-adreno-lock.md`](../docs/03-fcl-adreno-lock.md)。
这里只讲怎么用。

## 方案 A（推荐）：原生库插件覆盖 `libpojavexec.so`

**不修改、不重签名、不卸载启动器**；卸载插件即可完全恢复。

### 它为什么有效

启动器用 `System.loadLibrary("pojavexec")` 加载 `libpojavexec.so`，
而 `System.loadLibrary` **按 `java.library.path` 顺序查找**。
实测启动器把**插件的库目录排在它自己的库目录之前**：

```
java.library.path =
  .../jre25/lib : .../jre25/lib/jli : .../jna
  : <插件>/lib/arm64          ← ★ 我们的补丁库放这里
  : <voxy 插件>/lib/arm64
  : /data/user/0/<pkg>/app_runtime_mod
  : ...
  : /data/app/.../<启动器>-.../lib/arm64      ← 启动器自带的（在最后）
```

于是 JVM 命中的是**我们打过补丁的那一份**。

### 补丁内容

把 `checkAdrenoGraphics()` 改成恒返回真（arm64 指令）：

```
原始:  ff c3 02 d1   fd 7b 07 a9    →  sub sp, sp, #0xb0 ; stp x29, x30, [sp, #0x70]
补丁:  20 00 80 52   c0 03 5f d6    →  mov w0, #1        ; ret
```

### 步骤

```bash
# 1) 从设备上取出启动器的 libpojavexec.so（免 root 可读）
#    路径：/data/app/~~xxxx/<包名>-xxxx/lib/arm64/libpojavec.so
#    （或直接从 base.apk 里取 lib/arm64-v8a/libpojavexec.so）

# 2) 定位符号偏移（该库第一个 LOAD 段 Offset==VirtAddr，所以符号地址==文件偏移）
readelf -sW libpojavexec.so | grep checkAdrenoGraphics
#   144: 0000000000009dac   468 FUNC  GLOBAL DEFAULT 13 checkAdrenoGraphics

# 3) 生成"补丁 + 打包成原生库插件 APK"
bash build_plugin.sh ./libpojavexec.so 0x9dac \
     com.example.fcl.patch /path/to/your.keystore youralias
```

产物是一个 `FCLNativePlugin=true` 的插件 APK，装到设备上即可。

### 验证（务必在**全新进程**里）

```bash
adb shell am force-stop <启动器包名>
# 启动游戏后看日志：
#   ✅ AdrenoSupp: Loaded Turnip, loader address: 0x...
#   ✅ WARNING: panvk is not a conformant Vulkan implementation, testing use only.
#   ❌ 若只有 "OSMDroid: loading vulkan regularly..."  → 补丁没生效
```

> ⚠️ **假阳性陷阱**：同一个进程内第二次调用会因为缓存的 `VULKAN_PTR` 也打印
> `AdrenoSupp: ...`。判断必须基于**新进程**，或直接找上面那行 panvk 警告。

## 方案 B：整包补丁 + 重签名

```bash
# 取 base.apk → 替换打过补丁的 lib/arm64-v8a/libpojavexec.so
#   （去掉 META-INF/*.SF *.RSA MANIFEST.MF，resources.arsc 必须未压缩且 4 字节对齐）
zipalign -f -p 4 app-unsigned.apk app-aligned.apk
apksigner sign --ks your.keystore --out app-patched.apk app-aligned.apk
```
代价：签名变了 → **必须卸载原版**（丢应用内设置；游戏存档一般不受影响）。

## 方案 C：提上游

建议把判据从「是不是 Adreno」改成「用户是否显式选了自定义驱动」：

```c
- if (!checkAdrenoGraphics()) return NULL;
+ if (driver_path == NULL && !checkAdrenoGraphics()) return NULL;
```

这样所有非 Adreno 平台（Mali / PowerVR / Xclipse）都能用开源驱动。
（`driver_path` 非空即表示用户在设置里显式选了驱动插件。）
