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
  → VK_ERROR_DEVICE_LOST(-4) 定位到纹理上传 vkQueueSubmit(v48)
  → 关掉 dma-heap 反向对照：错误码不变(v49)
  → 05 方案 A + LINEAR 推断 ⇒ 真 Android 交换链补丁(v50，未上机)
  → ★★ 独立探针：驱动渲染被证明 · 交换链三路全 VK_SUCCESS · 原假设不能复现
  → ★★ 掉线真身 = kbase CSF fatal exception 0xc3（发生在执行 draw 时）
  → 首要目标转向：查 CSF exception 0xc3
  → v50 上机：首次真交换链(2376x1080, imageCount=3) + 主界面干净渲染约 10s(不花屏/不乱跳/不撕裂)
  → v51 空载荷 APK(unzip 匹配失败) ⇒ 废弃，勿用
  → v52 = v50 载荷 + 全套调试 env ⇒ 落盘 logcat 抓到 kbase CSF group 0 tiler heap OOM
  → v53(P1) 补 uAPI 1.18 档 + csi_handlers ⇒ ★ 真机无效 ✗(无 TILER_OOM CSI handler 行)
  → v54(P2)：去掉 submit->tiler_work_estimate 前置门 ⇒ kbase_renew_tiler_heap() 真正触发
  → v56(P5 AFBC)⇒ exception 0xc3 ✗ · v57/v58(缓存失效)⇒ 无效 ✗ · v59–v61(C1 抑制 heap ops)⇒ 第 3~4 个作业即卡死 ✗
  → v62：C2(上游 open MR !44173)首次编入；v63 = v54 逐位基线 + C2(干净单变量，确定性对照通过)
  → ★★ 存活从「秒级」跃升到「分钟级」(C2)
  → v64 = v63 + PANVK_KBASE_HEAP_RENEW_INTERVAL=32(默认 128 太晚：堆在第 ~107 次提交就 OOM)
  → ★★★ P2 消灭 tiler heap OOM ⇒ 得以进入存档/世界(现场口述；见 §29.3 证据状态分栏)
  → v64 仍然挂起：三子队列 ring extract 精确停在 CALL、active 0、error 0x0、无 fault 无 OOM
  → v65(删掉 wrapper 重写 SB_MASK_STREAM 的 2 行)已出包，★ 未上机
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

> ⚠️ **本段已于 M12.4 更正**：上面这句 `O_RDWR` 读的是**旧树** `/root/mesa`（md5 `0f4d40ca…`）；
> **构建树** `/root/zenithblue/work/mesa`（md5 `e2e92db6…`，出厂件的来源）用的是 **`O_RDONLY`**，
> 对 0444 节点**能打开成功** ⇒ `supports_dmabuf()` 实为 **true**。
> 详见 `docs/09 §23.4`。**本段的 `sw_device=true` 结论对出厂件不成立。**

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

> ✅ **本注已被 M12 取代**：v48/v49 的**运行结论**与 v50 的**构建/补丁结论**现已记录在
> **M12**（证据 `docs/09 §23`/`§24`/`§25`、`research/11`、`research/12`）。
> **唯一仍然"未验证"的是 v50 的实机运行**（该补丁从未在真机上启动过，见 M12.2）。

**后续研究（12 篇专题 + 综述论文）**

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
| 10 | 验证方案与独立探针 | 7 模式探针 `probe10/`（`panvk_wsi_probe.c`）；把卡点**精确化**到 `vkCreateSwapchainKHR` 第 4 步 —— **已上机，结果见 `research/12`** |
| 11 | WSI 补丁实施（v50） | 3 个文件**严格加性**改动 + 产物哈希 + **构建目录实测判定**（`android-v4` 唯一产物；`strings \| grep -c wsi_x11`=0、`platforms=['android']` ⇒ **闭合 U4**）；附三份 diff 原文 |
| 12 | 真机探针 8 模式结果 | ★ **驱动渲染被独立证明**；`win`/`headless`/`winimpdef` 三路 `vkCreateSwapchainKHR` 全 `VK_SUCCESS`（**原假设不能复现**）；唯一可复现的 `-1000072003` 只在 AHB `fmt=0x22`；★ **`-4` 真身 = CSF exception `0xc3`（执行 draw 时）** |
| — | **综述论文** | [`research/paper.md`](research/paper.md)：**6 次**错误点迁移 + 三路线对比 + 附录（版本与哈希 / 复现 / 参考文献） |

**后续工作优先级**（详见 [`research/paper.md` §7.2](research/paper.md)）：~~P1 构建目录核对~~（**已闭合**，见 M12.2）、
~~P6 跑 7 模式探针~~（**已上机**，见 M12.3）⇒ 现行优先级：
**P0 查 CSF exception `0xc3`**（唯一拦路者，`docs/09 §25.7`）→ P1 用设备侧探针判定
`O_RDONLY` 下 `DMA_HEAP_IOCTL_ALLOC` 与 `supports_dmabuf()`（`docs/09 §23.3/§23.4`）
→ P2 v50 上机看 WSI 补丁是否生效 → P3 真实 App 端到端复现 → P4 走正路接 imapper4/5 → P5 消双栈。

---

## M12 · ★★ 第六次错误点迁移：从「WSI」转到「绘制执行」（v48/v49/v50 + 真机探针）

**结论（一句话）**：v48/v49 把 `VK_ERROR_DEVICE_LOST (-4)` 钉在**纹理上传的 `vkQueueSubmit`**；
v50 落成"真 Android 交换链"补丁（**未上机**）；而**独立探针**把原假设**推翻**并给出新病灶 ——
`-4` 的真身是 **kbase CSF fatal exception `0xc3`**，发生在**真正执行 draw** 时，
与 WSI / u_gralloc / dma_heap **无直接关系**。⇒ **首要目标改为"查 CSF exception 0xc3"。**

### M12.1 v48 / v49：诊断加深 + dma_heap 反向对照（证据 `docs/09 §23`）

| 版本 | versionName | sha256 前缀 | 与前一版的**唯一自变量差异** | 运行结论 |
|---|---|---|---|---|
| v48 | `4.8-diag-deep` | `e511f980` | 追加 `MESA_DEBUG=1`、`PANVK_DEBUG=1`、`LIBGL_DEBUG=1`、`EGL_LOG_LEVEL=debug`、`MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log` | `-4` 的**首次出现**被钉在 MGL **纹理上传批次**的 `vkQueueSubmit`（`VkTextureManager.cpp`）；错误码/位置与 v47 相同 |
| v49 | `4.9-nodmaheap` | `f1389427` | 再追加 `PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent`（关掉 dma-heap ⇒ 退回 kbase 原生分配） | **无效**：错误码不变，`-4` 照旧 ⇒ **dma-heap / dma-buf 不是该 `-4` 的成因** |

**同时复核的源码事实**：构建树 `kbase_kmod.c` 用 **`O_RDONLY`** 打开 `/dev/dma_heap/system`（:1285），
却用该 fd 做 `DMA_HEAP_IOCTL_ALLOC`（:1569）；本机 dma_heap 节点全 **0444**、`/dev/mali0` 为 **0666**。
"ALLOC 必然失败"目前是**源码推断，未在设备侧取证**（探针日志里没有 ALLOC 的尝试/失败行）⇒ **标为未验证**。

### M12.2 v50：WSI 补丁（证据 `docs/09 §24`、`research/11`）

**三个文件、严格加性**：① `u_gralloc_fallback.c` 的 `-EINVAL → -EAGAIN`（一行修复）；
①b 新增 `panvk_infer_linear_modifier()`（MR !43659 式 **LINEAR 推断**）；
② `vk_android.c` 的 **AHB 自描述回退**（约 150 行，仅在既有调用失败后可达）；
②b `nativewindow_stub.cpp` 补 `AHardwareBuffer_lockPlanes` 桩（链接需要，运行时仍走系统真实库）。

| 产物 | 值 |
|---|---|
| 新 `libvulkan_panfrost.so` | size **20,005,320**，md5 **`e08e07645c16d8ebaa11ca70a09884fd`**，sha256 `a0b2451e…53f2d` |
| 旧件（出厂件，双份 `.bak-1791169250`） | size 20,003,136，md5 `4417b369591fc2b3df27e22019ccf3a2` |
| `mgl-panvk-v50.apk` | size 10,187,311，sha256 **`677d81eb29c7c57938573dfd9de20d39ff7f0557d466d3ed7ade9510033fc4b9`** |

**与 v49 的唯一自变量差异 = 删掉 `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`**（`grep -c HEADLESS` 实测 **0**）；
保留 v49 的 `PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent`、`MESA_DEBUG=1`、`PANVK_DEBUG=1`。
⇒ 让真机实验**只有一个自变量**（真 Android 交换链 vs headless）。

> ⚠️ **v50 的实机运行结论仍然缺失（未验证）**：该补丁只有**静态/链接层**验证（两次增量编译 `exit=0`、
> `SONAME`/`NEEDED` 与旧件逐条一致、APK 载荷 sha256 与新 `.so` 逐位相同），**从未在真机启动过**。
> 且 `-4` **不能声称已消除**：它出现在 headless 配置下、不经过 AHB/u_gralloc/本补丁。

### M12.3 ★★ 真机探针 8 模式：原假设不能复现，掉线真身是 CSF `0xc3`（证据 `docs/09 §25`、`research/12`）

探针：`panvk_wsi_probe`（277,800 B，md5 **`f735e1f4d03c40f248e76e02565f51e6`**），
加载的是**补丁前**出厂件 `4417b369…` ⇒ **本节结论独立于 v50 补丁**。
设备：OPPO PHZ110 / MT6989 / Immortalis-G720 MC12 / Android 16 / **无 root**（特权 shell uid 2000）。
⚠️ `--mode=all` 不可用（`tri` 的 CSF 掉线会污染同进程后续步骤）⇒ **必须一模式一进程**。

| 模式 | 结果 | 关键 VkResult |
|---|---|---|
| `render` | ✅ PASS `failures=0` | Submit 0 → Fences 0 → **三点像素 64/128/191/255 精确** |
| `tri` | ❌ FAIL | Submit 0 → **Fences −4** + CSF group 0/1/2 `fatal error status 0x7dc002c3 (exception 0xc3)` |
| `ahb`(0x1) | ✅ PASS `failures=0` | `vkGetAHBProps 0`（allocSize=16384）→ AllocateMemory 0 → 三点像素精确 |
| `ahbimpdef`(0x22) | ❌ FAIL（可复现 ×2） | `vkGetAHBProps −1000072003`；**唯一可复现的 `-1000072003` 只在 0x22 上** |
| `mapper` | ✅ PASS | `AIMapper_loadIMapper rc=0 version=5`、`importBuffer rc=0`、5 类 metadata 全正值 |
| `headless` | ✅ PASS `failures=0` | **CreateSwapchainKHR 0** → images 4 → Present 0 |
| `win`(0x1) | ✅ PASS `failures=0` | **CreateSwapchainKHR 0** → Present 0 → **窗口像素精确（WINDOW PIXEL PASS）** |
| `winimpdef` | ✅ PASS（swapchain 段） | **CreateSwapchainKHR 0** → Present 0 |

**三条要点**：
1. **驱动渲染被独立证明**：`render`（无 surface / 无 Activity / 无 root）提交并等到完成，像素**精确**回读。
2. **原假设不能复现**：`win` / `headless` / `winimpdef` 三路 `vkCreateSwapchainKHR` **全部 `VK_SUCCESS`**
   ⇒ "交换链创建必然失败"**在合成探针里不成立**；唯一可复现的 `-1000072003` 只在
   AHB 用 `IMPLEMENTATION_DEFINED(0x22)` 分配时出现（MESA `Failed to get u_gralloc_buffer_basic_info`）。
3. ★ **真正让 GPU 掉线的是「绘制」**：`tri` 的 `vkQueueSubmit` 返回 0 但 `vkWaitForFences = -4`，
   kbase 报三个 CSF group fatal（exception `0xc3`）；**不含 draw 的 clear+copy 全正常**
   ⇒ **命令提交/内存/导入链路通，坏在实际光栅化执行。**

⇒ **目标转向**：从"修 WSI"改为"**查 CSF exception `0xc3`**"。即使交换链建起来（v50 做的正是这件事），
**只要真的画东西，GPU 就掉**。（探针结论只覆盖合成探针；推广到真实 App 需一次端到端复现，本轮未做。）

### M12.4 一处**更正**：`O_RDWR` 那句话读的是另一棵树（证据 `docs/09 §23.4`）

存在**两份 `kbase_kmod.c`**，此前混引：`/root/mesa` 版（68,849 B，md5 `0f4d40ca…`）用 **`O_RDWR`**（`research/06` 读的就是它）；
**构建树** `/root/zenithblue/work/mesa` 版（72,941 B，md5 `e2e92db6…`，与 `patches/kbase-common/files/` 副本逐字节相同）
用 **`O_RDONLY`**，且其 `mtime`（2026-10-04 22:58）**早于**产物 `mtime`（2026-10-05 01:51）
⇒ **出厂件编自 `O_RDONLY` 那一份**。`O_RDONLY` 对 0444 节点能打开成功 ⇒
`kbase_kmod_supports_dmabuf()`（函数体 = `return dma_heap_fd >= 0;`）返回 **true**
⇒ 上面 M9 的"`supports_dmabuf()=false` ⇒ `sw_device=true`"**对出厂件不成立**。
这也正好解释 M12.1 的 v49"关掉 dma-heap 无效"。

---

## M13 · v50 上机（首次真画面）· v51 废弃 · v52 抓到 tiler heap OOM · v53(P1) **无效 ✗**

### M13.1 v50 上机：★ **首次出现真交换链 + 真画面**（证据 `docs/09 §28.4`）

删掉 `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`（与 v49 的**唯一自变量差异**）后：

- MGL 日志**首次出现真交换链**：`Swapchain created, extent = 2376x1080, swapchain imageCount = 3`；
- 判据行仍达成：`OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)`；
- ★ **用户实测**：**MC 主界面（含 3D 全景）正常渲染约 10 秒、画面干净**
  （**不花屏 / 不乱跳 / 不撕裂**），之后**黑屏并崩溃**；日志
  `vkQueuePresentKHR` / `vkAcquireNextImageKHR` → **`-4`**。
- ⇒ `-4` 只是**从「约 2 秒」推迟到「约 10 秒」**，**并未消除**；但它把「真交换链」这一步做实了。

### M13.2 v51（`5.1-wsi-patched-debug`）：**空载荷 APK ⇒ 废弃，勿用**（证据 `docs/09 §26.1`）

打包时 `unzip` 匹配载荷条目失败 ⇒ 产物里**只剩 manifest / `resources.arsc` / `classes.dex` / 签名**，
**一条 `lib/arm64-v8a/*` 都没有**（size **8,595 B**、sha256 `6e9ce7d2…b0313`），
而 `apksigner verify` **仍然通过**。
⇒ 教训：打包后必须 `unzip -l` 数载荷条目 + `unzip -p … | sha256sum` 与源件比对（v53 已据此加固）。

### M13.3 v52（`5.2-wsi-patched-debug`）：v50 载荷 + 全套调试 env ⇒ ★ **抓到决定性一行**（证据 `docs/09 §26.2/§28.5`）

- v52 = **v50 载荷一字节不改** + `MESA_DEBUG=1` / `PANVK_DEBUG=1` / `LIBGL_DEBUG=1` /
  `EGL_LOG_LEVEL=debug` / `MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log`；sha256 `0bbef030…`；
  `MESA_VK_WSI_HEADLESS_SWAPCHAIN` 出现次数 = 0（已核）。
- ★ **关键教训（方法论）**：**logcat 环形缓冲会冲掉证据** ——
  前期日志量极大（`PANVK_DEBUG` + `LIBGL_DEBUG`），事后 `logcat -d` 回读时那行**已经不在缓冲区**。
  ⇒ 治本做法 = **起进程前就后台落盘**：`logcat -b all -v time > /data/local/tmp/cap.txt`。
  另记一条自杀式命令：`pkill -f "<模式里含自身命令行的字符串>"` 会把**执行它的 shell 自己**杀掉
  ⇒ 要用 **`pkill -x logcat`**。
- 抓到的决定性一行：`E/MESA: kbase: CSF group 0 tiler heap OOM notification`；
  **时间线**：判据行 → **+5~10 s** 该行 → **+9 s** `VK_ERROR_DEVICE_LOST`。

### M13.4 v53（`5.3-p1-tiler-oom-csi`，P1）：补 uAPI 1.18 档 + `csi_handlers` ⇒ ★ **真机无效 ✗**

（证据 `docs/09 §27`/§28.6、[`research/16`](research/16-p1-p2-implementation.md)）

- 改动**唯一一处**：`kbase_kmod.c` 的 `kbase_kmod_csf_group_create()`，
  版本阶梯 `1.25 / 1.6` → **`1.25 / 1.18 / 1.6`**；新增 1.18 档 =
  ioctl **`0xc028803a`**（58）、结构体 **40 B**、置 `csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG`；
  uAPI 判断原样保留、`<1.18` 老路径不变（1.6 兜底仍在最后）。
- 产物：新 `.so` size **20,005,600** / md5 **`7f3a0e8f…b404`** / sha256 **`58ef996f…bec4`**；
  v53.apk sha256 **`9c99af82…d48e`**；增量编译 `NINJA_EXIT=0`；
  **反汇编确认新分支真的进了二进制**（`0xc028803a` + `csi_handlers(29)=1`）。
- ★ **真机结果：无效 ✗** —— 仍出现 `kbase: CSF group 0 tiler heap OOM notification`，
  且**没有**出现期望的 `kbase: created CSF group N with TILER_OOM CSI handler (1.18 layout, ioctl 58)`
  ⇒ **推断 1.18 分支被版本门挡住、根本没走到**（⚠️ 另外两条分支日志——1.25 档成功行 / 1.6 兜底行——
  本轮**无记录**，标为**未验证**）。
- ⇒ **P1 单独不够**：[`research/14`](research/14-tiler-heap-oom.md) 已定案
  **「tiler heap 只涨不落」才是 OOM 的直接成因**（`kbase_renew_tiler_heap()` 是死代码）
  ⇒ **下一步 = P2**（[`research/16`](research/16-p1-p2-implementation.md) §6 / [`research/14`](research/14-tiler-heap-oom.md) Fix B1）。

> **产物台账**：v50–v53 的文件名 / 大小 / sha256 见 [`MANIFEST.md`](MANIFEST.md) §B 与 §E.1。

---


## M14 · ★★★ 里程碑：分钟级存活 —— 两个决定性修复（C2 / P2）与三个被撤回的自造改动

> 证据：[`docs/09 §29–§34`](docs/09-mobilegl-integration.md)、[`research/21`](research/21-p5-modifier-fallback.md)–[`research/31`](research/31-v65-next-contract-fix.md)、[`MANIFEST.md`](MANIFEST.md) §B.1。
> **口径**：驱动侧两道门（C2/P2）是**逐位可复核**的；"分钟级 → 进世界"这一跳目前**只有现场口述**，
> 本节按 [`README.md` §6.6](README.md) 如实标注为"**未复核**"，不写成事实。

### M14.1 C2 = 上游 open MR `!44173`：复用 tiler heap 前先等自家 tiling 工作退休

- **位置**：`src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c` 的 `get_tiler_desc()` 内、
  `panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER)` **之后**，
  插入 `cs_wait_slots(b, dev->csf.sb.all_iters_mask);`（本树 `:1232`；v62 树在 `:1244-1252`）。
- **效果**：存活时间从**秒级**跃升到**分钟级**、直至（现场口述）进入存档/世界 ✓
- **落地版本**：v62 首次编入（`6.2-c2-tiler-heap-wait`，驱动 `61d35b1c…`）；
  **v63 = v54 逐位基线 + C2**（干净单变量，驱动 `c03f0e7b…`）。
- ★ **判据纪律**：C2 的**独有判据**是归属注释 **`upstream MR !44173`**；
  **`cs_wait_slots(all_iters_mask)` 的出现次数不能单独作判据** —— v54 本身就有一处同形调用
  （`mark_crc_valid_after_fragment()` 的 CRC 路径，v54 `:1613` → v63 `:1623`），清洁基线计数本就是 1。
- **确定性对照**：撤 C1 + 撤 v58 两行后重编 ⇒ **逐位 = `a9cba64a…`（= v54）** ✓；贴回 C2 ⇒ `c03f0e7b…`，二次重编逐位相同 ✓

### M14.2 P2：让 `kbase_renew_tiler_heap()` 真正触发（★ 区间是决定性的）

- **位置**：**`src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c`**（**不是** `kbase_kmod.c`）；
  函数起 `:2204`、调用点 `:2834`。
- **死代码根因**：触发条件带 `submit->tiler_work_estimate &&`，而该字段**全树无写入点**（只在 `panvk_cmd_buffer.h` 声明）
  ⇒ 计数永不推进 ⇒ **续期是死代码** ⇒ 堆只涨不落 ⇒ `-ENOMEM` ⇒ 内核 `term_queue_group()` 杀组。
- **修法**：`kbase_tiler_submit_count` 改为**每 N 次图形提交**（`touched & graphics_mask`，clear-only 不计）就触发续期；
  工作阈值那条支路保留（等将来真有生产者）。
- **开关**：`PANVK_KBASE_HEAP_RENEW_INTERVAL`（默认 `KBASE_TILER_HEAP_RENEW_INTERVAL 128`；`0` = 关闭）。
- ★ **区间对照（决定性）**：**128 太晚**（实测堆在**第 ~107 次提交**就 `tiler heap OOM`）→
  **32**（v64 现用）**消灭 OOM** ✓ → `100000`（等价关闭，反证）**OOM 立刻复现** ✓。
- **正面证据**：v64 现场打印 `kbase: tiler heap renewal (uAPI %u.%u, submits %u, renew interval %u)`
  共 **23 次 / 19 次**（两次运行），`interval 32` 生效 ✓；
  归档 `cap.txt` **13:05–14:47 全窗口 `tiler heap OOM` = 0 次**（v52/v53 时代同一条通知是常客）✓

### M14.3 v63 = v54 + C2（干净单变量）

- versionName `6.3-v54-plus-c2`；驱动 `c03f0e7b7e391b20…`（20 007 048 B）；APK `e0c249da36b111b6…`（10 187 311 B）。
- **撤回干净性**：撤掉 C1 + 撤掉 v58 的 2 行后重编 ⇒ **逐位等于 v54 载荷 `a9cba64a…`（20 006 408 B）** ✓

### M14.4 v64 = v63 + `PANVK_KBASE_HEAP_RENEW_INTERVAL=32`

- versionName `6.4-fast-renew32`；**驱动逐位同 v63**（`c03f0e7b…`）；APK `db9a816317008d4b…`（10 187 311 B）。
- ★ **v64 与 v63 的唯一差异是 `pojavEnv` 多一项 env** —— 本项目**最便宜的一次决定性改动**（无需重编驱动）。
- **结果**：★ **消灭 tiler heap OOM**，得以进入存档/世界（现场口述）。

### M14.5 ⛔ 三个已确认有害/无效的自造改动（**必须撤回**）

| 代号 | 改动 | 判定 | 证据 |
|---|---|---|---|
| **C1** | kbase 路径上抑制 `cs_vt_end` / `cs_finish_fragment` / `cs_frag_end` 与 **TILER_OOM** 处理器注册（`cmdbuf_skips_gpu_heap_ops()` 守卫，6 处） | ✗ **有害** | 流水线在**第 3~4 个作业即卡死** |
| **v58 的 2 行** | `kbase_subqueue_publish()` 读 `*active` 前加 `kbase_cache_invalidate_range()`（`:733`/`:737`） | ✗ **无效** | 真机无改善 |
| **P5** | AHB modifier 回退从 `LINEAR` 改成 AFBC **`0x0800000000000072`**（`PANVK_GRALLOC_AFBC_FALLBACK`） | ✗ **有害** | 触发 **`exception 0xc3`**（MMU `TRANSLATION_FAULT_3`） |

- **撤回纪律**：**不用** `git checkout/restore/reset`（kbase 后端文件未跟踪）；改前 `cp <f> <f>.bak-$(date +%s)`；
  撤回 = `cp` 回备份 + 重编，并用"**撤掉后逐位等于上一版 `.so`**"证明干净。

### M14.6 四条工程教训（已写入 [`docs/09 §33`](docs/09-mobilegl-integration.md)）

1. **静默回落陷阱**：垫片找 ICD 的候选顺序含 `/data/local/tmp`（[`source/shim/vkshim_mgl.c:45`](source/shim/vkshim_mgl.c)）**且不打印选中路径**
   ⇒ 探针时代遗留的**一份旧驱动**让 **v50–v54 的改动全部未被加载**（"加了日志却没输出"的真因）。
   ★ **铁律：每次上机前必须把驱动显式放到 `/data/local/tmp/libvulkan_freedreno.so` 并校验 sha256。**
2. **空载荷 APK 陷阱**：`unzip` 模式不匹配 ⇒ 打出没有 `lib/` 的 APK，而 `apksigner verify` 照样通过（v51）。
   ★ 必做 `unzip -p <apk> lib/arm64-v8a/libvulkan_freedreno.so | sha256sum`：非空且等于构建产物。
3. **日志落盘位置**：`/data/local/tmp/cap.txt` 在 Shizuku 掉线后 App 通道读不到 ✗ ⇒ 改写到 **`/sdcard/MG/cap.txt`** ✓。
   ⚠️ 追加：该文件**会被轮转截断**（取证中由 81 MB 截到 6.9 MB）⇒ **关键证据必须当场摘录**。
4. **确定性对照纪律**：每次改动都要有"**撤掉本改动后重编 ⇒ 逐位等于上一版 `.so`**"的对照（本轮已多次执行 ✓）。

### M14.7 v64 的下一道门：`ring extract` 卡在 `CALL`（v65 待验）

- v64 三次运行**同形收场**：三个子队列的 ring `extract` **精确停在各自最后一条 ring entry 的 `CALL` 指令上**，
  `CS_ACTIVE = 0`、`cell->error = 0`、**无 CS fault、无 TILER_OOM**、10 s 内 20 次 rekick 一字节未动；
- ⇒ 病灶从"OOM"前移到"**流切换（`CALL`）处的 CS 状态被卡**"；嫌疑 = kbase wrapper 在 ring entry 头部重写 `SB_MASK_STREAM`；
- **v65**（`6.5-no-wrapper-sbmaskstream`，驱动 `b9952f75…`）删掉那 2 行，**已出包、未上机**（见 U15）。

---

## 附：本仓库**如实记录**的未解项（不隐藏）

| # | 未解项 | 依据 |
|---|---|---|
| U1 | **`VK_ERROR_DEVICE_LOST (-4)` 的真身 = kbase CSF fatal exception `0xc3`**：`vkQueueSubmit` 返回成功、`vkWaitForFences` 得 `-4`，三个 CSF group 同时 fatal（`status 0x7dc002c3`），**发生在真正执行 draw 时**；不含 draw 的 clear+copy 全正常。已定位到"绘制执行"，**尚未修** | `docs/09 §25`、`research/12` |
| U2 | **双栈隐患**：ZL2 自己的 `load_vulkan()` 又 `dlopen("libvulkan.so")`（系统 loader → blob）并把句柄交给 MGL | `docs/09 §17` |
| U3 | `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1` 让判据行出现，但**本身不是出画面的方案**（headless present 是空操作） | `docs/09 §22`、`research/07` |
| U5 | `driverVersion` 两处记录不一致（探针 `26.2.24.3` vs 真机 `26.2.99`） | `docs/09 §8` vs `§11/§17/§22` |
| U6 | 机型写法不一致（`docs/09 §7` 作 `PHX110`，其余作 `PHZ110`）⇒ 取 `PHZ110` | `research/10` |
| U7 | `research/09` 无中文摘要 | [`research/README.md`](research/README.md) §3 |
| U9 | `docs/09 §15` 的"被 `patchelf` 改过 `DT_NEEDED` 的 MGL 被提前加载"是**假设**，未被证实 | `docs/09 §15` |
| U11 | **`O_RDONLY` 下 `DMA_HEAP_IOCTL_ALLOC` 是否成功 / `kbase_kmod_supports_dmabuf()` 的实际返回值**：本轮只做了源码推断（"必然失败"），未在设备侧取证；`research/06` 的注释给出的是**相反**推断 | `docs/09 §23.3` |
| U12 | **v50（真 Android 交换链补丁）** ⇒ ✅ **已上机**（`docs/09` §28.4：真交换链 + 主界面干净渲染约 10 秒）；⚠️ `-4` 仍在（该补丁不解决它） | `docs/09 §24.5`、`research/11` §7 |
| U13 | **可复现的 `-1000072003` 只在 AHB `IMPLEMENTATION_DEFINED(0x22)` 上出现**（`Failed to get u_gralloc_buffer_basic_info`）；真实 App 的 Surface 用的正是该格式 ⇒ 与 U1 是**两条独立线** | `docs/09 §25.5`、`research/12` §6 |
| U15 | ★ **v64 仍会挂起（当前第一拦路者）**：三个子队列的 ring `extract` **精确停在各自最后一条 ring entry 的 `CALL`**，`CS_ACTIVE=0`、`error 0x0`、**无 CS fault、无 TILER_OOM**、10 s 内 20 次 rekick 一字节未动 ⇒ 病灶 = **流切换（`CALL`）处的 CS 状态被卡**；嫌疑 = kbase wrapper 在每个 ring entry 头部重写 `SB_MASK_STREAM`。**v65 已出包（驱动 `b9952f75…`）但未上机** | `docs/09 §29.4/§34.3`、[`research/31`](research/31-v65-next-contract-fix.md) |
| U16 | ⚠️ **「2 分 51 秒」最长存活** 与 **「成功进入存档/世界」**：**会话现场口述，归档工件中未复核** —— 归档 `cap.txt` 仅含三次运行（进程存活 55.2 / 82.2 / 50.2 s，渲染期最长 68 s 无 `-4`），且该文件已被轮转截断；[`research/31`](research/31-v65-next-contract-fix.md) §1.1/§5 独立得出同一判断 | `docs/09 §29.3`、[`research/31`](research/31-v65-next-contract-fix.md) |

**✅ 本轮已闭合（从上方未解项中移出）**

| 原 # | 事项 | 闭合依据 |
|---|---|---|
| ~~U4~~ | `research/05` 与 `research/06` 对"出厂 `.so` 出自哪个 build 目录/哪套 platforms"的**矛盾** | ✅ **已闭合**：md5 + `strings` 实测判定 —— 4 个 build 目录中**只有 `/root/zenithblue/build/android-v4` 有产物**；`strings <so> \| grep -c wsi_x11` = **0**、`build.ninja` 中 x11 = **0** 次、meson `platforms=['android']` ⇒ **05 号对、06 号的"出厂件含 x11 WSI"不成立**（`android-deps-x11` 只是 include 目录名）。见 `docs/09 §24.4`、`research/11` §1 |
| ~~U8~~ | `research/10` 探针未上机；`research/05` 方案 A 未上机 | ✅ **探针已上机**（8 模式，见 `docs/09 §25`、`research/12`）；**方案 A 已实施并编入 v50**（见 `docs/09 §24`、`research/11`）——但 v50 本身仍未上机（保留为 **U12**） |
| ~~U10~~ | v48/v49/v50 的运行结果无记录（`docs/09` 止于 §22/v47） | ✅ **已闭合**：`docs/09` 新增 **§23（v48/v49）**、**§24（v50）**、**§25（探针 8 模式）**，本文件新增 **M12** |
| ~~U12~~ | v50（真交换链补丁）从未在真机运行 | ✅ **已上机**：`Swapchain created, extent = 2376x1080, swapchain imageCount = 3` + 主界面（含 3D 全景）干净渲染约 10 秒（`docs/09 §28.4`）；⚠️ `-4` 仍在 |
| ~~U14~~ | tiler heap OOM 线（真实 App 现场的直接拦路者） | ✅ **OOM 线已闭合**：**P2（§31）真的触发续期** —— v64 现场打印 `kbase: tiler heap renewal (… renew interval 32)` **23 次 / 19 次**，且归档 `cap.txt` **13:05–14:47 全窗口 `tiler heap OOM` = 0 次**（v52/v53 时代同一条通知是常客）。⚠️ **但这是"绕过"不是"修好"**：P1（v53）的 uAPI 1.18 档 + `csi_handlers` 是否送达内核**仍未定案**（保留为 **R3**）；且第一拦路者已换成 **U15**（`CALL` 处卡住） | `docs/09 §31/§34.3`、[`research/14`](research/14-tiler-heap-oom.md)、[`research/31`](research/31-v65-next-contract-fix.md) |

> **未闭合项的口径**：本轮（M14）的净变化是 —— **tiler heap OOM 线闭合（~~U14~~，靠 P2 续期）**，
> 但**第一拦路者前移到 `CALL` 处的 CS 状态卡住（U15）**，且"分钟级 → 进世界"这一跳
> **只有现场口述、归档未复核（U16）**。U11（dma_heap 的 `O_RDONLY`/ALLOC 判定）与
> U5/U6/U7/U9 等其余未解项**未被本轮证据触及**，口径与上一版一致。
> 归档侧新增一条**方法论**未决：`/sdcard/MG/cap.txt` 会被轮转截断（`docs/09 §33.3`），
> 长跑取证的落盘策略需要重新设计。
