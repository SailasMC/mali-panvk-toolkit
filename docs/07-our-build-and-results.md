# 我们自己的构建：结果与卡点（第一阶段实录）

> 这一篇记录**实测结果**：我们自己为 G720/v12 编出的 PanVK 是什么、验证到了哪一步、
> 以及在两个启动器上分别卡在哪里。所有结论都有日志/崩溃报告证据。

---

## 1. 产出：第一份 G720/v12 的 PanVK（Mesa 26.3.0-devel）

```
profile     : g720-v12-csf      (panArch 12 + CSF + mali_kbase)
基线        : zenithblue-oss/panvk-kbase-android  (Mesa pin 5a07217f034, 26.3.0-devel)
补丁序列    : 18 个补丁 (android / kbase-common / app-loader / wsi / csf)
NDK         : r27c, API 35
产物        : libvulkan_panfrost.so
大小        : 20,003,136 字节
sha256      : 2cff5e19a5d253dcd1b99a81a4a325c15725c4c62f3f0344133f17521d69f0b2
```

> 上游 `profiles/g720-v12-csf.json` 的 `status` 是 **`planned`** —— 这份构建是该 profile
> 的**第一份真机产出**（构建侧的坑见 [04](04-build-g720-panvk.md)）。

## 2. 独立验证：驱动本身完全正常

不经过任何启动器，用 [`../tools/vkicd_probe.c`](../tools/vkicd_probe.c) 直接加载：

```
[1] dlopen(驱动)                     ✔ 加载成功
[2] 形态判定                          ✔ ICD（vk_icd* + HMI 双导出）
[3] vk_icdNegotiateLoaderICDInterfaceVersion: 请求 5 → 返回 5   ✔
[4] vkCreateInstance                 ✔ 实例创建成功（真正初始化 GPU）
[5] vkEnumeratePhysicalDevices       ✔ 1 个设备
[6] deviceName  : Mali-G720 MC12          ← 注意：没有 "-Immortalis"
    apiVersion  : 1.4.363
    扩展        : 181 个
    └ VK_KHR_swapchain ✔  VK_ANDROID_external_memory_android_hardware_buffer ✔
      VK_KHR_timeline_semaphore ✔  VK_KHR_dynamic_rendering ✔  VK_EXT_descriptor_indexing ✔ …
```

**结论**：kbase 通路 + 驱动初始化 + 设备枚举**全部正常**，问题只在「启动器把它接进去」这一层。

## 3. 启动器侧：拆锁成功，但暴露了更深的问题

### 3.1 FCL —— 厂商锁已拆掉（有证据）

把启动器自带的 `libpojavexec.so` 的 `checkAdrenoGraphics()` 改成恒真后，
FCL 的日志里出现了这一行（过去从来没有过）：

```
AdrenoSupp: Loaded Turnip, loader address: 0x8a373874375aed85
VULKAN_PTR = 0x8a373874375aed85
```

并且游戏进程里出现 PanVK 自己的输出：

```
WARNING: panvk is not a conformant Vulkan implementation, testing use only.
Checking PIPE_CAP_DMABUF:  KHR_external_memory_fd: true  …
```

**但随后崩溃，且崩溃点不在厂商锁上**：

```
# Problematic frame:
# C  [libpojavexec.so+0xdee0]  Java_org_lwjgl_glfw_CallbackBridge_nativeSetWindowAttrib+0x54
siginfo: SIGSEGV, si_addr: 0x0000000000000000        ← 空指针
```

> 也就是说：**FCL 的"自定义驱动"分支在非 Adreno 设备上还有第二处不成立的地方** ——
> 进入该分支后，窗口属性（`nativeSetWindowAttrib`）这条路径会解引用空指针。
> 这是本项目**新发现的上游缺陷**，与我们的驱动无关（崩在启动器自己的代码里）。

### 3.2 FCL 的注入链细节（为什么必须替换它自带的库）

实测发现 FCL 的 `loadTurnipVulkan()` 是**无参数**版本，**不使用 `DRIVER_PATH`**：

```
崩溃时的内存映射：
  /data/app/.../com.tungsten.fcl-.../lib/arm64/libvulkan_freedreno.so   ← FCL 自带的 Turnip (9.4MB, tu_* 符号)
  （DRIVER_PATH 指向的插件库里那份，根本没被加载）
```

所以对 FCL 而言，「换插件」是不够的 —— 必须**把它自带的 `libvulkan_freedreno.so` 换掉**
（本仓库的做法：重打包 APK，把该文件替换成我们的 panvk，同时把 `checkAdrenoGraphics` 补丁打进去）。

**注意**：FCL/ZL2 的 APK 均为 **GPL-3.0**，本仓库只提供方法，**不分发**修改版 APK。

### 3.3 Zalith Launcher 2 —— 设计正确，但补丁库加载不到

ZL2 的代码是**对的那一种**（会显式把 `DRIVER_PATH` 装进命名空间）：

```c
void* loadTurnipVulkan(const char* driver_path, const char* native_dir, const char* cache_dir) {
    if (!checkAdrenoGraphics()) return NULL;
    if (!native_dir || !linker_ns_load(native_dir)) return NULL;   // ★ 用 DRIVER_PATH
    ...
    linker_ns_dlopen(target_driver /* libvulkan_freedreno.so */, ...);
}
```

**卡点**：ZL2 把 `checkAdrenoGraphics` 放在**另一个库**里，并且是硬依赖：

```
libpojavexec.so  →  DT_NEEDED: libdriver_helper.so   ← 锁在这里
```

我们尝试用「原生库插件 + `patchelf --set-rpath '$ORIGIN'`」覆盖 `libpojavexec.so`，
让它从自己的目录加载打过补丁的 `libdriver_helper.so` —— **未生效**：

```
ZL2 日志里 AdrenoSupp 出现 0 次（根本没进自定义驱动分支）
游戏仍报：OpenGL Renderer: zink (Mali-G720-Immortalis MC12)   ← 厂商 blob
```

> 需要继续的方向：确认 `System.loadLibrary("pojavexec")` 实际命中的是哪一个
> （`java.library.path` 顺序），或直接对 ZL2 的 APK 做同样的"整包替换"。

## 4. 意外发现：MC 原生 Vulkan 后端会触发 FCL 的另一个 bug

FCL 设置里有「图形后端：default / opengl / vulkan」（仅对 26.2+ 生效）。
选 `vulkan`（即游戏原生 Vulkan 后端，理论上效率最高）后：

```
Description: Loading library LWJGL system
java.lang.NoSuchMethodError: Method org.lwjgl.glfw.CallbackBridge.nativeSetUseInputStackQueue(Z)V not found
	at org.lwjgl.system.Library.<clinit>
	at com.mojang.blaze3d.platform.NativeLibrariesBootstrap.loadLWJGLSystem
```

**这是 FCL 的 JNI 方法与 MC 26.2 自带 LWJGL 的版本错配** —— 与我们的驱动无关，
但意味着「原生 Vulkan 后端」这条路需要先修 FCL 才能走通。

## 5. 结论与下一步（按性价比排序）

| # | 下一步 | 预期 | 难度 |
|---|---|---|---|
| 1 | 定位并绕过 FCL `nativeSetWindowAttrib` 的空指针（自定义驱动分支）| ★ 最接近成功 | 中（需要读懂该函数为何拿到 NULL）|
| 2 | 对 ZL2 做整包替换（`libdriver_helper.so` 打补丁 + 驱动替换）| 高（ZL2 的设计本来就支持 DRIVER_PATH）| 中（ZL2 数据在 Android/data，需先备份游戏目录）|
| 3 | 给上游提 PR：① 厂商锁改为「用户是否选了自定义驱动」② `nativeSetWindowAttrib` 空指针 | 一劳永逸 | 低（代码改动小）|
| 4 | 再回到「原生 Vulkan 后端 / MobileGL」优化效率 | 依赖 1~3 先通 | 中 |

**已经确定不需要再做的事**：
- ❌ 不需要写新驱动（我们的 panvk 已验证可用）
- ❌ 不需要 root（kbase 通路已验证）
- ❌ 不需要纠结 `LD_LIBRARY_PATH`（Android 只在进程启动时读，运行时 `setenv` 无效 —— 实测确认）
