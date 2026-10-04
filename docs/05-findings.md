# 实测对比：两个开源 PanVK 成品在这台 G720 上的表现

> 一句话：**两个成品各缺一半能力** ——
> 一个「Android 能用但 GPU 架构不对」，另一个「GPU 架构对了但 Android 路径崩」。
> 这就是为什么要自己编（见 [`04-build-g720-panvk.md`](04-build-g720-panvk.md)）。

测试条件统一为：OPPO PHZ110 / 天玑 9300 / **Immortalis-G720 MC12** / Android 16 / 无 root，
启动器 FCL 1.3.3.5，已拆掉 Adreno 厂商锁（[见 03](03-fcl-adreno-lock.md)），
渲染器 = 内置 Zink（`POJAV_RENDERER=opengles3_desktopgl_zink_kopper`）。

---

## 1. 对照表

| | zenithblue（`panvk-kbase-android` beta.13） | wonderkast02（`panvk-g720-kbase-csf` beta.2） |
|---|---|---|
| 面向 GPU | G615（panArch **11**）| **G720（panArch 12）** ✅ |
| 面向场景 | Android 应用（PanPlay / Samba 等）✅ | Winlator / X11（DXVK）|
| 文件 | `libvulkan_panfrost-android-aarch64.so`（30.8 MB）| `libvulkan_panfrost.so`（21.5 MB）|
| 构建 NDK | r30-beta1 | r29 |
| 形态 | **ICD + HMI 双导出** | **仅 HMI**（HAL）|
| 离线探测（`vkicd_probe`）| `Mali-G720 MC12`、API **1.4.363**、188 扩展 | 能加载、能枚举 |
| 关键扩展 | `logicOp` / `fillModeNonSolid` / `vertexAttributeInstanceRateDivisor` / `VK_EXT_vertex_attribute_divisor` / `VK_KHR_push_descriptor` **全有**（厂商 blob 全缺）| — |
| WSI | `VK_KHR_swapchain` ✅、`VK_KHR_android_surface` ✅、`VK_EXT_headless_surface` ✅ | — |
| **在 FCL 里加载** | ✅ 被加载（崩前已进入初始化）| ✅ 被加载 |
| **在 FCL 里跑** | ❌ **静默失败**：Zink 初始化失败 → `There is no OpenGL context current in the current thread.` | ❌ **SIGSEGV 崩在驱动内部** |
| 推测原因 | panArch 11 的寄存器/CS 配置在 v12 上不成立（社区实测：v12 的 stream 最多用到 **123** 号寄存器，v11 的 **96** 不够）| 该构建以 X11/DXVK 为主，Android 表面路径未验证 |

## 2. 崩溃证据（wonderkast02 版）

JVM 崩溃报告 `hs_err_pid*.log`：

```
# Problematic frame:
# C  [libvulkan_freedreno.so+0x5f7918]        ← 崩在驱动内部

Native frames: (J=compiled Java code, j=interpreted, Vv=VM code, C=native code)
  C  [libvulkan_freedreno.so+0x5f7918]
  C  [libzink_dri.so+0xb59c80]
  C  [libzink_dri.so+0x488998]
  C  [libzink_dri.so+0xa8a1e0]
  C  [libzink_dri.so+0x488cc0]
  C  [libzink_dri.so+0x48c50c]
  C  [libEGL_mesa.so+0x30288]
  C  [libEGL_mesa.so+0x34660]
  C  [libEGL_mesa.so+0x31180]
  C  [libEGL_mesa.so+0x1f28c]  eglInitialize+0xdc
  C  [libpojavexec.so+0xac74]  gl_init+0x48
  C  [libpojavexec.so+0xa4a0]  pojavInitOpenGL+0x340

siginfo: si_signo: 11 (SIGSEGV), si_code: 1 (SEGV_MAPERR)
        si_addr: 0x003200004b5668bc        ← 明显是野指针
```

**这张栈说明了三件事**：

1. 厂商锁确实被拆掉了（否则不会有 `libvulkan_freedreno.so` 出现在栈里）；
2. 驱动**已经被真正调用**（`eglInitialize` → Zink → 我们的驱动）；
3. 崩点在驱动自己的代码里，与启动器无关。

## 3. 硬件侧的自检（说明设备本身没问题）

同一个驱动在**不进游戏**的情况下，用 `tools/vkicd_probe.c` 直接验证：

```
driverName      : panvk
deviceName      : Mali-G720 MC12      ← 注意：没有 "-Immortalis"，与厂商 blob 可区分
apiVersion      : 1.4.363
driverVersion   : 26.2.24.3
扩展            : 188 个
导出符号        : vk_icd* + HMI
```

也就是说：**kbase 通路、内存分配、Vulkan 初始化全部正常**，问题只出现在
「Zink/EGL 真正开始用它做图形工作」的阶段。

## 4. 怎么区分「厂商 blob」和「我们的驱动」

最方便的指纹是 **GPU 名**：

| 驱动 | 上报的名字 |
|---|---|
| 厂商 blob | `Mali-G720-Immortalis MC12` |
| PanVK | `Mali-G720 MC12`（**没有** `-Immortalis`）|

以及 PanVK 每次 `vkCreateInstance` 都会打印：

```
WARNING: panvk is not a conformant Vulkan implementation, testing use only.
```

这行是**最可靠的判据**：它由驱动自己打印，不会被启动器的指针缓存伪造。

## 5. 顺带分析：一个 Adreno 驱动的打包形态（可作模板）

社区流传的 `PurpleVK … A6XX_1.0.apk` 是 **Turnip/freedreno**（Adreno 用，源码里
有 `src/freedreno/ir3/instr-a3xx.h`、访问 `/dev/kgsl`），在 Mali 上毫无用处，
但它的**打包形态**正好是启动器驱动插件的标准模板：

```
AndroidManifest.xml
  <meta-data android:name="fclPlugin" android:value="true"/>
  <meta-data android:name="driver"    android:value="..."/>
  + 一个带 LAUNCHER intent-filter 的 Activity      ← 少了这个启动器不认
  android:extractNativeLibs="true"
lib/arm64-v8a/libvulkan_freedreno.so                ← 载荷文件名是启动器硬编码的
classes.dex
（自签名证书）
```

`tools/make_driver_apk.sh` 就是按这个契约生成插件 APK 的。

> 💡 这个契约**同时适用于 FCL 和 Zalith Launcher 2**（同源），本仓库实测两者都能识别。
