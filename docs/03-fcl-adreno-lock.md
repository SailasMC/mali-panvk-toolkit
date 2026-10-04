# FCL / Zalith Launcher 的 Adreno 厂商锁：分析、证据与拆锁

> 这是本仓库**最有价值**的一篇。它解释了为什么「驱动在设备上能跑」和「启动器真的用它」
> 是两件完全不同的事，并给出一个**不改、不重签启动器**的拆锁方案。

---

## 1. 现象

在 Mali 设备上把 PanVK 打包成标准驱动插件、装好、在启动器里选中：

- 插件能装上，能被启动器列出 ✅
- 但游戏启动后，图形设备仍然是**厂商 blob** ❌
- 日志里**完全没有** Mesa / PanVK 的任何输出 ❌

---

## 2. 源码定位（FCL 与 ZL2 同源，逻辑一致）

### 2.1 调用链

```
LWJGL 在 JVM 里 dlopen("libvulkan.so")
  └─ lwjgl_dlopen_hook.c            拦截该次加载
      └─ libpojavexec.so : maybe_load_vulkan()
          └─ load_vulkan()                    （egl_bridge.c）
              └─ #ifdef ADRENO_POSSIBLE
                  └─ loadTurnipVulkan(...)    （driver_helper.c）
                      └─ checkAdrenoGraphics()  ← ★ 锁在这里
```

### 2.2 锁本体

`jni/driver_helper/driver_helper.c`：

```c
bool checkAdrenoGraphics() {
    // ... 新建 EGL 上下文，glGetString(GL_VENDOR) / glGetString(GL_RENDERER)
    bool is_adreno = (vendor && renderer
                      && strcmp(vendor, "Qualcomm") == 0
                      && strstr(renderer, "Adreno") != NULL);
    return is_adreno;
}

void* loadTurnipVulkan(const char* driver_path, const char* native_dir, const char* cache_dir) {
    if (!checkAdrenoGraphics())
        return NULL;                      // ← ★ Mali 永远从这里返回 NULL
    if (!native_dir || !linker_ns_load(native_dir)) return NULL;
    ...
    const char* target_driver = (driver_path && strlen(driver_path) > 0)
                                ? driver_path : "libvulkan_freedreno.so";
    void* h = linker_ns_dlopen(target_driver, RTLD_LOCAL | RTLD_NOW);
    ...
}
```

调用点 `egl_bridge.c`：

```c
void load_vulkan() {
    const char* zinkPreferSystemDriver = getenv("POJAV_ZINK_PREFER_SYSTEM_DRIVER");
    if (zinkPreferSystemDriver == NULL && android_get_device_api_level() >= 28) {
#ifdef ADRENO_POSSIBLE
        const char* native_dir = getenv("DRIVER_PATH");
        void* result = loadTurnipVulkan(NULL, native_dir, cache_dir);
        if (result != NULL) {
            printf("AdrenoSupp: Loaded Turnip, loader address: %p\n", result);
            set_vulkan_ptr(result);
            return;
        }
#endif
    }
    printf("OSMDroid: loading vulkan regularly...\n");     // ← Mali 总是走这里
    void* vulkanPtr = dlopen("libvulkan.so", RTLD_LAZY | RTLD_LOCAL);
    ...
}
```

### 2.3 关键点

| 事实 | 说明 |
|---|---|
| `ADRENO_POSSIBLE` **是打开的** | FCL 的 `CMakeLists.txt`、ZL2 的 `Android.mk` 都 `-DADRENO_POSSIBLE` |
| 没有环境变量开关 | 唯一的 `POJAV_ZINK_PREFER_SYSTEM_DRIVER` 是**反向**的（强制用系统驱动）|
| 锁是**运行时**的 | 编译期就已经包含这段代码，能否进入取决于 `checkAdrenoGraphics()` 的返回值 |
| 检查的是 **GL vendor/renderer** | Mali 上恒为 `ARM` / `Mali-*`，永远 != `Qualcomm` |

### 2.4 日志判据

```
# 锁生效（Mali 上必然如此）
OSMDroid: loading vulkan regularly...
OSMDroid: Loaded Vulkan, ptr=...

# 锁被拆掉（本仓库的目标）
AdrenoSupp: Loaded Turnip, loader address: 0x...
VULKAN_PTR = 0x...
```

> **⚠️ 一个坑**：`egl_bridge.c` 里 `maybe_load_vulkan()` 的实现是
> 「若 `VULKAN_PTR` 环境变量已存在，就直接返回它、**不调用** `load_vulkan()`」。
> 所以在**同一个进程**里第二次调用会打印 `AdrenoSupp: ...`（复用缓存的指针），
> 这是**假阳性**，不代表插件被加载。**必须在全新进程（force-stop 后）里验证。**

---

## 3. 拆锁方案

### 方案 A（推荐）：原生库插件覆盖 `libpojavexec.so`

**原理 —— 利用 `java.library.path` 的解析顺序。**

实测的 FCL `java.library.path`（来自游戏日志）：

```
.../jre25/lib : .../jna :
<我们的原生库插件>/lib/arm64        ← ★ 插件目录
<voxy rocksdb 插件>/lib/arm64
/data/user/0/com.tungsten.fcl/app_runtime_mod
...
/data/app/.../com.tungsten.fcl-.../lib/arm64     ← FCL 自己的库目录（在最后）
```

FCL 是用 `System.loadLibrary("pojavexec")` 加载 `libpojavexec.so` 的，
而 `System.loadLibrary` **按 `java.library.path` 顺序查找** —— 我们的目录排在 FCL 自己前面。

所以：**把打过补丁的 `libpojavexec.so` 放进一个原生库插件**（`FCLNativePlugin=true`
+ `environment` meta-data），FCL 启动游戏时就会加载我们这一份。

**补丁本身**（把 `checkAdrenoGraphics` 改成恒真）：

```
函数序言（arm64）:  ff c3 02 d1   fd 7b 07 a9   →  sub sp, sp, #0xb0 ; stp x29, x30, [sp, #0x70]
打补丁后:            20 00 80 52   c0 03 5f d6   →  mov w0, #1        ; ret
```

偏移量算法：`checkAdrenoGraphics` 的符号地址即**文件偏移**（该库第一个 `LOAD`
段的 `Offset == VirtAddr == 0`）：

```
readelf -lW libpojavexec.so
  LOAD  0x000000  0x0000000000000000  ...   ← vaddr == offset
readelf -sW libpojavexec.so | grep checkAdrenoGraphics
  144: 0000000000009dac  468 FUNC GLOBAL DEFAULT 13 checkAdrenoGraphics
                  ↑ 文件偏移 0x9dac
```

**优点**：不修改、不重签名、不卸载 FCL；**完全可逆**（卸载插件即恢复）。
**代价**：无（插件只多 41 KB）。

一键构建见 [`../fcl-patch/build_plugin.sh`](../fcl-patch/build_plugin.sh)。

### 方案 B：整包二进制补丁 + 重签名

对 `base.apk` 里的 `libpojavexec.so` 打同样的补丁，重打包、`zipalign`、重签名。
**代价**：签名变了 → 必须卸载原版 → **丢应用内设置**（存档在共享目录，通常不丢）。
细节与一个注意点（`resources.arsc` 必须 4 字节对齐，Android 11+ 强制）见
[`04-build-g720-panvk.md`](04-build-g720-panvk.md) 附录。

### 方案 C：上游 PR（最干净）

建议把判据从「是不是 Adreno」改成「用户是否显式选了自定义驱动」：

```c
// 现在：只认高通
if (!checkAdrenoGraphics()) return NULL;

// 建议：自定义驱动由用户选择，不该按厂商拦
if (driver_path == NULL && !checkAdrenoGraphics()) return NULL;
// 或：提供一个 FORCE_CUSTOM_DRIVER 环境变量 / 设置项
```

这会让所有非 Adreno 平台（Mali / PowerVR / 三星 Xclipse）都能用开源驱动。

---

## 4. 拆锁之后的验证清单

按顺序看，任一步不过就是线索：

| # | 日志/现象 | 含义 |
|---|---|---|
| 1 | `AdrenoSupp: Loaded Turnip, loader address: 0x...`（**全新进程**）| 锁已拆，注入路径已进入 |
| 2 | `VULKAN_PTR = 0x...` | 加载器句柄已交给 LWJGL |
| 3 | `WARNING: panvk is not a conformant Vulkan implementation, testing use only.` | ★ **我们的 PanVK 真的被调用了** |
| 4 | `Checking PIPE_CAP_DMABUF: KHR_external_memory_fd: true ...` | Zink 正在探测我们的驱动 |
| 5 | `Using graphics device: zink (<驱动上报的 GPU 名>)` | 出图成功 |

第 3 步是本仓库实测中**最可靠的判据** —— 这行警告由 PanVK 自己在
`vkCreateInstance` 时打印，绕不过去、也不会被缓存伪造。

若在第 3 步之后崩溃，JVM 的 `hs_err_pid*.log` 里的 `Problematic frame`
会直接指出崩在哪个 `.so` 的哪个偏移：

```
# Problematic frame:
# C  [libvulkan_freedreno.so+0x5f7918]     ← 崩在驱动内部
Native frames:
  C  [libvulkan_freedreno.so+0x5f7918]
  C  [libzink_dri.so+0xb59c80]
  C  [libEGL_mesa.so+0x1f28c]  eglInitialize+0xdc
  C  [libpojavexec.so+0xac74]  gl_init+0x48
  C  [libpojavexec.so+0xa4a0]  pojavInitOpenGL+0x340
```

（这正是本项目定位「驱动是 G615/v11 调优、在 G720/v12 上崩」的证据链。）
