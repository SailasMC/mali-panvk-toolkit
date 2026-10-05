# CHANGELOG —— 进展台账（按里程碑）

> **口径**：本文件是**里程碑级**的进展台账，每条都标注证据位置
> （`docs/09 §N` = [工程实录](docs/09-mobilegl-integration.md) 第 N 节；`research/NN` = [研究正文](research/) 第 NN 篇；
> `summaries/…` = 中文结论摘要）。
> **版本号 `vNN` 是唯一的时间序**（`versionCode` 单调递增；APK 的 `mtime` 与日志时间戳一并给出以便对照）。
> 凡"未验证/估计/不一致"的内容均已如实标注，不做美化。
>
> 产物详情（文件名 / 大小 / sha256 前缀 / 用途）见 [`MANIFEST.md`](MANIFEST.md)。
> 全部记录日期均为 **2026-10-05**（服务器与设备本地时区；APK `mtime` 亦为同日）。

---

## 时间线速览

```
驱动可用(kbase/PanVK)
  → 4 个编译坑(MobileGL)
  → 走厂商 blob 的 DirectVulkan 满帧(59–60 FPS)
  → 铁证：Android loader 忽略 VK_ICD_FILENAMES
  → 自建 125 入口转发垫片(导出 125/125)
  → loader 语义：123 项 thunk 表 + 三级分派
  → 三处分派修正 + 4 处"物理设备当 instance"修正 → vkCreateDevice 成功
  → WSI 关卡 + MESA_VK_WSI_HEADLESS_SWAPCHAIN=1
  → ★★ 判据行达成
  → VK_ERROR_DEVICE_LOST(-4) 与后续研究
```

---

## M0 · 驱动可用：免 root 打通 Mali kbase，自编 PanVK 可用

**结论**：免 root 的普通 App 可以完整驱动 Mali kbase 内核接口（`/dev/mali0`，CSF，uAPI 1.21），
从而加载并运行自编 Mesa PanVK；独立探针确认它是标准 ICD 形态。

**证据**
- `docs/02-kbase-bringup.md`（握手、属性、内存分配、mmap 校验）
- `docs/09 §8`：从**插件 `nativeLibraryDir`** dlopen 成功 ⇒ `deviceName: Mali-G720 MC12`、
  `apiVersion: 1.4.363`、扩展 **181** 个；
  ICD 形态齐全（`vk_icdNegotiateLoaderICDInterfaceVersion` / `vk_icdGetInstanceProcAddr` / `HMI`）
- **反例（同一探针）**：从 `/storage/...` dlopen ⇒ `Permission denied`（**noexec**，见 M6）
- 工具：`tools/kbase_probe.py`、`tools/kbase_selftest.c`、`tools/vkicd_probe.c`

**同时确立的硬约束**：`/dev/dri/*` 全 EACCES（`card0` 存在但**无 `renderD*`**，`research/06` §10）
⇒ 免 root 路线**不能**走 DRM。

---

## M1 · MobileGL 编译：4 个坑

**结论**：MobileGL（CMake + C++23）编出含 `DirectGLES` + **`DirectVulkan`(Magma)** 两个后端的
`libMobileGL.so`；308 MB（带调试符号）→ `llvm-strip --strip-unneeded` 后 **16.9 MB**。

**四个坑（原文表，`docs/09 §2`）**

| # | 坑 | 现象 | 解法 |
|---|---|---|---|
| 1 | 递归 `git clone --recursive` 卡死 | 子模块目录一直 8K，网络其实很好 | 改用 codeload **tarball** 精确拉取 |
| 2 | glslang 缺 SPIRV-Tools | `ENABLE_OPT set but SPIR-V tools not found` | 从 SPIRV-Tools 的 **DEPS** 读 `spirv_headers_revision`，按那个 commit 拉 |
| 3 | MGL 自带旧 `spirv.hpp11` 抢包含路径 | 报 `OpGraphARM` / `OpMemberDecorateIdEXT` 缺失 | 把 SPIRV-Headers 的 `spirv.hpp11/spirv.h/spirv.hpp` **覆盖**到 `MobileGL/include/glslang/SPIRV/` |
| 4 | **`BUILD_SHARED_LIBS=ON` 把 glslang 编成 `.so`** | 链接期 `undefined symbol: glslang::SetThreadPoolAllocator…` | 改回 **OFF**（MGL 注释明确写它静态内嵌 glslang/SPIRV-Tools/SPIRV-Cross） |

**证据**：`docs/09 §2`；`docs/08-mobilegl-vulkan.md`；`research/07`（MGL 源码结构）。

---

## M2 · 走厂商 blob 的 DirectVulkan：满帧（但**不是**我们的驱动）

**结论**：MobileGL 的 **Direct(Vulkan)/Magma** 后端在这台 Mali 设备上**完全能跑，帧率满血**；
但当时用的是**厂商 blob**。

**证据**（`docs/09 §7`，ZL2 `26.3 Fabric` 启动日志原文）

```
▷ Renderer: MobileGL Magma_1001_Vulkan(OpenGL 4.6,1.17+)
▷ POJAVEXEC_EGL = libMobileGL.so
[DEBUG] DLOPEN: …/com.mio.plugin.renderer.MGL.Magma-…/lib/arm64/libMobileGL.so , success
[06:29:36] Using graphics backend OpenGL, using drivers:
           4.6.0 MobileGL 26.09-dev, Direct (Vulkan) Backend, GIT@cbbaf77
```

游戏内 **FPS 稳定 59–60**。同一份日志里出现 **`Mali-G720-Immortalis MC12`**（厂商 blob 设备名）
⇒ **指纹判据成立**：是否用上我们的驱动**一眼可判**。

**同轮确立的两条工程经验**（`docs/09 §7`）
- 改别人签名的插件装不上（签名不符）⇒ 复刻 meta-data 到自己能更新的包名里；
- 自动化点按两条通道：无障碍通道**可能把 Harness 前台抢回来**导致点按落空；
  ADB 注入不抢前台、成功点中「启动游戏」，但它注入的 `HOME`/`BACK`/手势会被**沉浸式游戏吞掉**。

---

## M3 · 铁证：Android 系统 loader **忽略** `VK_ICD_FILENAMES`

**结论**：环境变量**确实注入**了游戏进程（`VK_ICD_FILENAMES` 在插件 `pojavEnv` 里被 setenv），
但 Android 的系统 `libvulkan.so` 依然选了 **vendor blob**。**Android 上这条路封死。**

**铁证（版本号，不是设备名）**，`docs/09 §11`（实测时间 **07:03**）：

| | deviceName | apiVersion | driverVersion |
|---|---|---|---|
| 实际生效（厂商 blob） | `Mali-G720-Immortalis MC12` | **1.3.247** | **44.1.0** |
| 我们的 PanVK（独立探针） | `Mali-G720 MC12` | **1.4.363** | 26.2.24.3 |

```
[07:03:38] Using graphics backend OpenGL, using drivers: 4.6.0 MobileGL … Direct (Vulkan) Backend, GIT@cbbaf77
[07:03:41] OpenGL Renderer: Magma (MobileGL Core) (Mali-G720-Immortalis MC12, Vulkan 1.3.247, Driver 44.1.0)
```

**本轮版本**：`mgl-panvk-v16`（`1.6-loaderdebug`，sha 前缀 `c26ba62d`，mtime 07:01）
—— 带 `VK_LOADER_DEBUG=all`，**正是为这条铁证做的实验**。

**附带结论（可观测性）**：`VK_LOADER_DEBUG=all` 的输出**没进 logcat**
（Android 丢弃 app 的 `stderr`）⇒ loader 的内部诊断在这个环境下拿不到（`docs/09 §11`）。
另：把 `libvulkan.so.1` 顶替 loader 会让 **JVM 卡死在 `JLI_Launch`**（`docs/09 §9②`，6 分钟无输出）。

**⇒ 只剩两条路**（`docs/09 §11`）：(A) 写导出 Vulkan 核心 API 的**垫片**（推荐，不动 MGL 源码）；
(B) 改 MobileGL 源码直连 ICD。

---

## M4 · 自建 125 入口转发垫片（导出 125/125）

**结论**：`libMobileGL.so` 有 **125 个** loader 风格 `vk*` UND 符号，而我们的 ICD 只导出 `vk_icd*`
⇒ 用**生成器**自动产出**完整转发层**，导出全部 125 个名字，内部 `dlopen` 我们的 ICD 并逐一分发。

**证据与产物**
- `docs/09 §10`（`readelf -sW --dyn-syms` 原文：UND `vk*` **125 个**；
  驱动导出 `vk_icdGetInstanceProcAddr` ✔ / `vk_icdNegotiateLoaderICDInterfaceVersion` ✔，
  但 `vkCreateInstance` / `vkGetInstanceProcAddr` / `vkCreateDevice` ✘）
- `docs/09 §12`（生成器步骤 + 两个必须避开的坑）
- 源码：`source/shim/gen_shim.py`、`source/shim/shim.c`、`source/shim/vkshim_mgl.c`、`source/shim/vkund.txt`
- 生成报告（`source/shim/gen_report.txt`）：
  `总符号 125 · 泛型转发 119 · 手写特殊 6 · {device:61, device_global:42, instance:3, physdev:11, instance_create:2}`
- 双路自查：导出 `vk*` 数 == **125**，与需求求差为空（`docs/09 §12`）

**生成器的两个坑（都实际踩过）**：① 正则**跨 typedef** 吞掉整段 struct/flag 定义
（修法：返回类型组限制为 `[^;{}]*?` + 断言）；② `vkCreateInstance` 等**不是无参**，
分类必须用"**首参类型**"而非"有没有参数"。

**本轮版本**：`mgl-panvk-v17`（`1.7-shim`，`5a0d65e8`，07:08）。

---

## M5 · 顶替 `libvulkan*` 名字这条路**彻底封死**（对照实验）

**结论**：只要插件目录里的 `libvulkan.so.1` 顶替了系统 loader，**ZL2 的 JVM 就卡死**在启动路径上
—— 与垫片是否"完整"**无关**。

| 版本 | 插件 `lib/arm64-v8a/` 里放的 | 结果 |
|---|---|---|
| **v18**（`1.8-shim-as-loader`，`1c990617`，07:20） | 原件 `libMobileGL.so` + `libvulkan.so` = 完整垫片 | 游戏**正常启动**，但日志是 `…Immortalis MC12, Vulkan 1.3.247, Driver 44.1.0` = **厂商 blob** ⇒ **`.so` 那个名字没被命中** |
| **v19**（`1.9-shim-both`，`5aed9bf9`，07:12） | 同上，`.so` 与 **`.so.1` 双名**都放完整垫片 | **卡死在 `[DEBUG] Calling JLI_Launch`**（与 §9② 的裸 ICD 事故**同一症状**） |

⇒ **ZL2/LWJGL 在 JVM 启动早期就会解析/使用 Vulkan**，而"垫片 → PanVK"链在那个时机**不能完成**
（最可能是 PanVK 在 app 域做 kbase 初始化的**时机问题**）（`docs/09 §13`）。

**并列的两条**
- `patchelf --replace-needed` 换 `DT_NEEDED` 到唯一名垫片也**失败**：
  `library "libvkpanvk_shim.so" not found: needed by … in namespace clns-9`（`docs/09 §14`）；
- 直到 `docs/09 §17(a)` 拿到命名空间原文才定性：**`ld_library_paths=""` ⇒ 裸名永远找不到**。

**⇒ 只剩最后一条路**（`docs/09 §13`）：**改 MobileGL 源码，让它直连我们的 ICD**
（不动任何系统库名字 ⇒ JVM 启动路径完全不受影响；驱动只在 MGL 真正要建 instance 时才加载）。

---

## M6 · `DLOPEN` 预加载：机制定案 + `DT_SONAME` 必要条件

**结论（读 ZL2 源码定案，`docs/09 §15`、`§17(b)(c)`）**

1. `DLOPEN=<名字>` 的语义是**纯预加载**：ZL2 拼成 `"$nativeLibraryDir/<名字>"`，
   在 dlopen 渲染器库**之前**调 `ZLBridge.dlopen(path, RTLD_GLOBAL|RTLD_LAZY)`；
   **它不修改任何库搜索路径** ⇒ 之前"v22/v23 的 LWJGL 崩是 `DLOPEN` 造成的"这个怀疑**不成立**。
2. **只有 `pojavEnv` 被解析**；写 `boatEnv` 无效（`pojavEnv` 解析源码原文 + `research/08`）。
3. `getRuntimeLibraryPath()` **不含插件目录**（只有 `super + jnaDir`）⇒ 这正是 `clns-9` 那条报错的根源，
   而 `DLOPEN` 预加载恰好能补上。
4. 垫片**必须带 `DT_SONAME=<同名>`**（`-Wl,-soname,libvkpanvk_shim.so`），否则即便 DLOPEN 成功，
   `DT_NEEDED` 仍报 `not found`（某一版 65080 B / sha `91c4053e` 就死在这里）。
5. **不要**删 `DT_NEEDED` 只靠 `RTLD_GLOBAL` 全局组 ⇒ `cannot locate symbol`（设备原生探针实测）。

**为什么 `RTLD_GLOBAL` 预加载就是"劫持点"**（`docs/09 §16`，JVM 崩溃报告原文）：

```
#  SIGSEGV (0xb)  at pc=0x000079b57af630
# Problematic frame:
# C  [libvulkan_freedreno.so+0xd7b630]  wsi_GetSwapchainImagesKHR+0x20
```

`libvulkan_freedreno.so` **就是我们自己编译的开源 Mali PanVK**，它在这个游戏进程里
**被加载 → 初始化 → 建 instance/device → 走到创建交换链**才崩
⇒ **整条链路已经成立**，只是最末端（WSI）出错；垫片进入全局符号组后，
它导出的 `vkGetInstanceProcAddr` **抢先命中**，MGL 运行期取到的入口全部来自垫片。

**本轮兜底**：`mgl-panvk-v24`（`2.4-restore-good`，07:27）装回设备，保证 ZL2 能正常启动 26.3。

---

## M7 · 垫片静态链入 MGL（UND `vk*` = 0）+ loader 语义：**123 项 thunk 表**

**结论**：把转发层**静态链进 MGL 本体**（不再依赖任何系统库名字），MGL 的 UND `vk*` 符号降为 **0**；
并补上 **loader 语义**：`vkGet*ProcAddr` **只返回自家 thunk**（123 项），保证句柄域统一 + 日志可观测。

**证据**
- `docs/09 §18.1`：`readelf --dyn-syms` ⇒ UND `vk*` = **0**
- `docs/09 §18.2`：错误点从"驱动 WSI 段错"迁移到"**MGL 内 NULL 函数调用**（`libMobileGL.so+0x961380`）"
  → 修法：**三源回退（`gp_inst`/`gp_dev`）+ 123 项 thunk 表** → 出现 `Required extension found: VK_KHR_swapchain ✓`
  （**扩展枚举正确了**）
- `docs/09 §18.1`：MGL 日志 `Enabled optional device extension: VK_EXT_vertex_attribute_divisor`
  （早先走 blob 时它是 `missing` ✗）；`/sdcard/MG/vkshim.log`：`vkCreateDevice: ext=11` + 11 个扩展名
  + `features: 0x7b885f72b4`，且这 11 个扩展在 panvk 里**全部可用**

**本轮版本**：`mgl-panvk-v36`（`3.6-loader-semantics`，`f96884d9`，07:55）。

---

## M8 · `vkCreateDevice = -3` 的完整追踪 → **建设备成功**

**现象**（`docs/09 §19`）：`No graphics queue found on physical device` +
`[FATAL] vkCreateDevice → VK_ERROR_INITIALIZATION_FAILED (-3)`。

**Mesa 源码根因**（`docs/09 §19`）：`panvk_vX_device.c:376` 的队列族 `switch` 里
`default: return panvk_error(dev, VK_ERROR_INITIALIZATION_FAILED)`
⇒ **`-3` 的真身 = MGL 请求的队列族不是 GPU/BIND**。

**根因链的另一半**：MGL 看不到图形队列，是因为**物理设备级查询走了 instance GIPA**，
被返回了**错的函数指针**（同源症状：`apiVersion=540.1018.2112`、`viewport limit=1852401253`、
`timestampPeriod≈2.7e26` 这些**垃圾值**）。

**修正链条（每一环都有版本与证据）**

| 版本 | versionName | sha256 前缀 | 内容 | 证据 |
|---|---|---|---|---|
| v38 | `3.8-filelog` | `383184bd` | **日志双写** `/sdcard/MG/vkshim.log`（不受 logcat 环形缓冲影响） | `docs/09 §20.4` |
| v39 | `3.9-pdpa-first` | `1e4c231f` | `gipa_pd()` **物理设备级优先**（先 pdpa，再退回真实 instance） | `docs/09 §18.3`（原文即给出前缀 `1e4c231f`） |
| v40 | `4.0-featcmp` | `217c6dda` | `feat[i] want/sup/xor`（6 字 = 192 个 feature 位逐位对比） | `docs/09 §20.4` |
| v41 | `4.1-featmask` | `acf64880` | 建 device 前**屏蔽驱动不支持的 feature 位** + 打印 `masked-out bits` | `docs/09 §19`/`§20.4`（原文即给出前缀 `acf64880`） |
| v42 | `4.2-qfam` | `390e64c8` | `QueueFamilyProperties: count / family[i] flags,queues` | `docs/09 §20.4` |
| v43 | `4.3-a-test` | `f1d6a3b7` | `QFam-diag: fn / viaInstanceGIPA / pdpa / gipa / g_inst`（**A/B 二分**） | `docs/09 §20.1`/`§20.4` |
| v45 | `4.5-pd-fixed` | `01313450` | 修正后的分派（4 处） | `docs/09 §20.1` |
| v46 | `4.6-wsi-diag` | `81083a6e` | `SurfaceCaps:` 与 `CreateSwapchain:` 落盘 | `docs/09 §20.4` |

**A/B 二分一次定论**（`docs/09 §20.1`）：

```
QFam-diag: fn=0x0  viaInstanceGIPA=0x79bad17d28  pdpa=0x79bad793b0  gipa=0x79ba9c4d0c  g_inst=0x7b6cf91680
           ↑ NULL ✗   ↑ 有效 ✓    ⇒ 转发层取指针取空了，驱动没问题
QueueFamilyProperties: count=0      ← 因为 fn=NULL，转发器跳过调用，count 保持 0
```

**根因（生成器误判）**：把"**首参含 `VkPhysicalDevice`**"的函数误判成设备级
（`VkPhysicalDevice` 里含子串 `VkDevice`）⇒ 它们走 `gp_inst(physicalDevice, …)`，
**把物理设备当成 instance** 传给 ICD 的 GIPA。**共修正 4 处**：

```c
/* 之前 */ PFN_vkX fn=(PFN_vkX)gp_inst(physicalDevice,"vkX");
/* 之后 */ PFN_vkX fn=(PFN_vkX)gipa_pd(physicalDevice,"vkX");   /* 先 pdpa，再退回【真实 instance】 */
```

**结果（质的飞跃，`docs/09 §20.2`）**

```
SurfaceCaps: min=2 max=4 cur=2376x1080 usage=0x17 alpha=0x9        ← surface 完全有效
QueueFamilyProperties: count=1 / family[0] flags=0x7 queues=2      ← GRAPHICS|COMPUTE|TRANSFER ✓
vkCreateDevice: feature mask applied, masked-out bits=0x00000000
  feat[0] want=0x1 sup=0x1 xor=0x0 …                               ← 请求位 == 支持位，零分歧
vkCreateDevice → 成功（MGL 日志中不再出现 vkCreateDevice FATAL）
```

---

## M9 · WSI 关卡：`VK_ERROR_INVALID_EXTERNAL_HANDLE` → 用 headless 交换链绕开

**现象**（`docs/09 §20.3`/`§21.1`）：MGL 传的交换链参数**全部合法**，仍被拒
`VK_ERROR_INVALID_EXTERNAL_HANDLE (-1000072003)`：

```
SurfaceCaps: min=2 max=4 cur=2376x1080 usage=0x17 alpha=0x9
CreateSwapchain: surf=0x7b8b3f9690 usage=0x13 fmt=37 cs=0 pm=1 alpha=0x1 layers=1 old=0x0
                 usage=0x13 ⊆ caps 0x17 ✓  fmt=37 ✓  pm=1(MAILBOX) ✓  图像数 3 ∈ [2,4] ✓
```

**源码定位到一行**（Mesa `src/vulkan/runtime/vk_android.c`）：

```c
struct u_gralloc *u_gralloc = vk_android_get_ugralloc();
if (u_gralloc_get_buffer_basic_info(u_gralloc, in_hnd, &info) != 0)
   return VK_ERROR_INVALID_EXTERNAL_HANDLE;          /* ← 就是这里 */
```

**为什么必然失败（源码级判定，`docs/09 §21.2` + `research/04`）**：
AUTO 顺序 `CROS → [GRALLOC4 若编入] → LIBDRM → QCOM → FALLBACK` 逐个不通 ——
GRALLOC4 **没编入**（`strings libvulkan_panfrost.so` 里 `android.hardware.graphics.mapper` 计数 **0**）；
LIBDRM 要求 `hw_get_module("gralloc")` 返回模块名严格等于 **`"gbm"`** ⇒ `goto fail`；
QCOM 高通专用；CROS 要求 `"CrOS Gralloc"`；
⇒ 落到 **FALLBACK**，它靠 `/vendor/lib64/hw/gralloc.default.so`，而 **Android 16 上它只是空壳**
⇒ `get_buffer_basic_info` 非 0 ⇒ 报 `-1000072003`。

**与项目开头的老结论闭环**：本机 `/dev/dri/*` 全部 `EACCES`，而 `u_gralloc` 默认后端正是走 `/dev/dri`。

**同时确立的决定性事实**（`research/06`，`summaries/06`）：`/dev/dma_heap/system` 权限 **0444**
⇒ `kbase_kmod.c` 用 `O_RDWR` 打开失败 ⇒ `kbase_kmod_supports_dmabuf()=false`
⇒ panvk 落 `sw_device=true`、`supports_modifiers=false` ⇒ DRI3/raw-fd **一行都没走到**。

**采用的绕开手段**：`research/07` 的"一行杀招" ——

```
pojavEnv += MESA_VK_WSI_HEADLESS_SWAPCHAIN=1
```

Mesa `wsi_common.c` 把**任意 surface（含 android）**换成 headless 交换链，
`queue_present` 是**空操作返回 `VK_SUCCESS`** ⇒ 绕开"Android WSI 转 DRM 描述"这一环。
（`research/07` 明确其定位是**诊断用**：把"建链失败→崩"降级为"干净跑完→黑屏"，
用于**判定坏的只有 WSI**。）

---

## M10 · ★★ 判据行达成：Minecraft 跑在「MobileGL DirectVulkan + 自编 Mesa PanVK」之上

**判据行（游戏日志原文，`docs/09 §22`）**

```
[10:46:13] [Render thread/INFO]: OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)
```

- `Mali-G720 MC12` ⇒ **我们的驱动**（厂商 blob 会显示 `Mali-G720-Immortalis MC12`）
- `Vulkan 1.4.363` ⇒ **我们的 API 版本**（厂商 blob 是 `1.3.247`）
- `Driver 26.2.99` ⇒ **我们的 Mesa 版本**

**达成版本**：**`mgl-panvk-v47`**（versionCode 47，versionName `4.7-headless-wsi`，
sha256 前缀 **`bb689838`**，mtime **10:43:21**）—— 载荷 = `panel v46` +
`pojavEnv` 增加 `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`。
完整 `pojavEnv`：
`LIBGL_ES=3:POJAV_RENDERER=opengles3:MOBILEGL_BACKEND_TYPE=DirectVulkan:MOBILEGL_ESPRYT_USE_ANGLE=0:MOBILEGL_MAGMA_R11G11B10F_FALLBACK=0:MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`

**同一次运行的其它证据**（`/sdcard/MG/vkshim.log`）：surface 有效、队列族 `count=1 flags=0x7 queues=2`、
两次 `CreateSwapchain`（第一次 MAILBOX、重试 FIFO）。

**这条链的完整演进**（`docs/09 §22` 原文）

```
驱动加载不了 → 探针证明 PanVK 可用(Mali-G720 MC12/1.4.363/181 扩展)
→ 4 个编译坑 → MGL 跑通 DirectVulkan(59-60fps, 但走厂商 blob)
→ 铁证：Android loader 忽略 VK_ICD_FILENAMES
→ 自建 125 入口转发垫片 + 静态链入 MGL(UND vk*=0)
→ loader 语义修正(123 项 thunk 表, vkGet*ProcAddr 返回自家 thunk)
→ 三处分派修正 + 4 处"物理设备当 instance"修正 ⇒ 队列族 flags=0x7、feature xor 全 0
→ vkCreateDevice 成功
→ WSI 关卡：Mesa u_gralloc 转 DRM 描述失败(/dev/dma_heap/system 0444 ⇒ kbase dmabuf 关闭)
→ 用 MESA_VK_WSI_HEADLESS_SWAPCHAIN=1 绕开 ⇒ ★ 判据行出现：Mali-G720 MC12, Vulkan 1.4.363
→ 下一关：纹理上传阶段 VK_ERROR_DEVICE_LOST
```

---

## M11 · 下一关：`VK_ERROR_DEVICE_LOST (-4)` 与后续研究

**结论**：判据行已拿到（渲染器链路成立）✓，但**这一版还不能稳定游玩**。
MGL 随后在**纹理上传**阶段报 `-4`（`docs/09 §22`）：

```
[10:46:15] FATAL: Vulkan error VK_ERROR_DEVICE_LOST (-4) at VkTextureManager.cpp
[10:46:15] FATAL: vkQueueSubmit(texture upload batch)
[10:46:15] ERROR: WaitForSubmitIndex: vkWaitForFences returned -4
```

**判据行之后的三个诊断版本**

| 版本 | versionName | sha256 前缀 | mtime | 差异（清单原文） |
|---|---|---|---|---|
| v48 | `4.8-diag-deep` | `e511f980` | 10:50:31 | 在 v47 基础上再加 `MESA_DEBUG=1`、`PANVK_DEBUG=1`、`MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log`、`LIBGL_DEBUG=1`、`EGL_LOG_LEVEL=debug` |
| v49 | `4.9-nodmaheap` | `f1389427` | 10:55:12 | 再加 `PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent`（**针对 M9 的 0444 dmabuf 根因做反向对照**） |
| v50 | `5.0-wsi-patched` | `677d81eb` | 11:03:30 | **真 Android 交换链版**：载荷换成新的 `libvulkan_panfrost.so`（改名 `libvulkan_freedreno.so`）+ v49 的 `libMobileGL.so`；env 含 `PANVK_GRALLOC_NO_FALLBACK=0`、`PANVK_GRALLOC_NO_INFER_LINEAR=0` |

> ⚠️ **v50 的实机结果不在本仓库的既有记录里**（`docs/09` 最后一节是 §22，对应 v47）。
> v48/v49/v50 的"用途"来自其**清单原文**（`versionName` + `pojavEnv`），
> **其运行结论未经验证，本文档不作断言**。

**后续研究（10 篇专题 + 综述论文）**

| 编号 | 主题 | 结论要点 |
|---|---|---|
| 01 | AIDL（imapper5）路线 | ★ 前提被纠正（mapper 从来没有 AIDL 接口，v5 是 native stable-C `AIMapper`）；但 `u_gralloc_imapper5_api.cpp` **已在不建 AOSP 下编译链接成功**（49,016 B，导出 `u_gralloc_imapper_api_create`） |
| 02 | HIDL（imapper4）路线 | **可行且便宜**（0.5–1.5 人天）；VNDK v29–v34 有预生成头（v35/v36 不存在），本机 `ro.vndk.version=34` 正吻合 |
| 03 | libgralloctypes 能力边界 | **不能绕开**：全 Android 公开 API 都没有返回 DRM modifier 的函数，而 panvk **强制要求** modifier |
| 04 | Mesa `u_gralloc` 精读 | 本机 5 个既有后端**全部不可用**（imapper4/5 在 `-Dandroid-stub=true` 下根本没编）⇒ 必须新增后端 |
| 05 | 绕开 `u_gralloc` 的最小补丁 | 方案 A：只改 `vk_android.c` 约 90–105 行、严格加性、重编 30–60 秒、**成功率估计 70–80%（未实测）** |
| 06 | PanVK Android WSI 深挖 | ★ **决定性事实**：`/dev/dma_heap/system` 0444 ⇒ `kbase_kmod_supports_dmabuf()=false`；路线 R1–R5 排名 |
| 07 | MobileGL 能否绕开交换链 | 不存在"只改配置就出画面"的路；★ `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`（**本仓库 M9→M10 采用的正是它**） |
| 08 | ZL2 如何交 Surface | 桥的选择**完全由 `POJAV_RENDERER` 字符串决定**；`gallium_*`/`vulkan_zink` ⇒ OSMesa 桥，**一次都不调 `vkCreateSwapchainKHR`**，代价是每帧 CPU 合成 |
| 09 | 真实世界先例 | `VK_ERROR_INVALID_EXTERNAL_HANDLE` / `u_gralloc` 的公开案例与补丁（⚠️ **尚无中文摘要**） |
| 10 | 验证方案与独立探针 | 7 模式探针 `probe10/`（`panvk_wsi_probe.c` 1394 行，**已编译、未上机**）；把卡点**精确化**到 `vkCreateSwapchainKHR` 第 4 步 |
| — | **综述论文** | [`research/paper.md`](research/paper.md)：5 次错误点迁移 + 三路线对比 + 附录（版本与哈希 / 复现 / 参考文献） |

**后续工作优先级**（详见 [`research/paper.md` §7.2](research/paper.md)）：
P0 查 kbase 侧 fault → P1 完成**构建目录核对**（05 与 06 的矛盾，见下）→ P2 走 R4 杠杆（ARM gralloc 的
`no_afbc_usage` 位，争取 LINEAR）→ P3 落 05 方案 A → P4 走正路接 imapper4/5 → P5 消双栈 → P6 跑 7 模式探针。

---

## 附：本仓库**如实记录**的未解项（不隐藏）

| # | 未解项 | 依据 |
|---|---|---|
| U1 | `VK_ERROR_DEVICE_LOST (-4)`（M11） | `docs/09 §22` |
| U2 | **双栈隐患**：ZL2 自己的 `load_vulkan()` 又 `dlopen("libvulkan.so")`（系统 loader → blob）并把句柄交给 MGL | `docs/09 §17` |
| U3 | `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1` 让判据行出现，但**本身不是出画面的方案**（headless present 是空操作） | `docs/09 §22`、`research/07` |
| U4 | **`research/05` 与 `research/06` 对"出厂 `.so` 出自哪个 build 目录/哪套 platforms"结论矛盾** —— 但实机确实成功创建过 swapchain ⇒ 必须用 md5 + `strings \| grep wsi_x11` 逐个 build 目录核对；**未做** | `summaries/06` §四 |
| U5 | `driverVersion` 两处记录不一致（探针 `26.2.24.3` vs 真机 `26.2.99`） | `docs/09 §8` vs `§11/§17/§22` |
| U6 | 机型写法不一致（`docs/09 §7` 作 `PHX110`，其余作 `PHZ110`）⇒ 取 `PHZ110` | `research/10` |
| U7 | `research/09` 无中文摘要 | [`research/README.md`](research/README.md) §3 |
| U8 | `research/10` 探针未上机；`research/05` 方案 A 未上机（70–80% 是估计） | `research/10`、`research/05` |
| U9 | `docs/09 §15` 的"被 `patchelf` 改过 `DT_NEEDED` 的 MGL 被提前加载"是**假设**，未被证实 | `docs/09 §15` |
| U10 | **v48/v49/v50 的运行结果无记录**（`docs/09` 止于 §22/v47）；本文件只记其**清单用途** | 本文件 M11 注 |
