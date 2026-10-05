# 12 号报告 — 探针上机执行结果（逐步 VkResult 钉死）

**结论摘要**：驱动渲染**已被独立证明**（render 模式 clear 色 64/128/191/255 三点精确回读，failures=0）。
原假设「Mesa PanVK 创建 Android 交换链失败」**在合成探针里不能复现** —— `win` / `headless` / `winimpdef`
三种 surface 下 `vkCreateSwapchainKHR` 全部 `VK_SUCCESS`，`win` 还证明 present 真的把像素写进了窗口 buffer。
唯一可复现的 `VK_ERROR_INVALID_EXTERNAL_HANDLE` 出现在 **AHardwareBuffer 用 IMPLEMENTATION_DEFINED(0x22) 分配**时
（`Failed to get u_gralloc_buffer_basic_info`）。另有一个**新发现**：真正让 GPU 掉线的是「绘制」——
`tri` 模式 `vkQueueSubmit` 成功但 `vkWaitForFences` 得 `VK_ERROR_DEVICE_LOST`，kbase 报 CSF 固件致命异常 0xc3。

---

## 0. 现场与资源

| 项 | 值 |
|---|---|
| 设备 | OPPO PHZ110 |
| Android | 16（SDK 36） |
| Build | `OPPO/PHZ110/OP5661L1:16/BP2A.250605.015/U.15933e8_fa9483_fa9481:user/release-keys` |
| SoC / 平台 | mt6989（MT6989 / Immortalis-G720 MC12） |
| 内核 | `6.1.157-android14-11-o-ga8ed1c96e9fd` |
| Vulkan 设备名 | `Mali-G720 MC12` vendor=0x13b5 device=0xc8700000 api=1.4 driver=109060195 |
| ICD 接口版本 | `vk_icdNegotiateLoaderICDInterfaceVersion -> 0 ver=7` |
| 驱动 .so | `/root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so`，md5 `4417b369591fc2b3df27e22019ccf3a2`（**与 run-device.sh 的期望值一致**） |
| 权限 | **无 root**；执行走特权 shell `uid=2000(shell)`（Shizuku UserService），每次审计 |
| 探针落地 | `/data/local/tmp/`（`/storage/emulated/0` 是 noexec，只作中转，**未在其中执行**） |
| `android_stub/` | **未创建**（驱动 .so 旁边保持了干净，符合 run-device.sh 的告诫） |

探针二进制 md5 校验：设备侧 `4417b369…` 与服务器 `md5sum` 完全一致后才执行。

---

## 1. 探针有三个「一跑就退」的 bug（已修，已重编）

收到的探针（274064 B）**无法产出任何逐步 VkResult**，不是驱动的问题，是探针自身的门禁写死了：

| 编号 | 位置 | 症状 | 修法 |
|---|---|---|---|
| **A** | `main()` 的 `INST_FNS(GI)` | 无条件要求 `vkCreateAndroidSurfaceKHR` **和** `vkCreateHeadlessSurfaceEXT`；但 `create_instance()` 只启用本模式需要的那个扩展 ⇒ `vk_icdGetInstanceProcAddr` 返回 NULL ⇒ `render` 在第 8 行就退出 | 只把这两个当可选；各自模式用到时才硬校验 |
| **B** | `create_device()` | 只启用过 `VK_KHR_swapchain`，**从不启用** `VK_ANDROID_external_memory_android_hardware_buffer`；而 `DEV_FNS` 无条件要求 `vkGetAndroidHardwareBufferPropertiesANDROID` ⇒ 所有模式死在 `load_dev_fns` | 只要设备宣告了就启用该扩展 |
| **C** | `load_dev_fns()` | 无条件要求 5 个 `VK_KHR_swapchain` 入口（`vkCreateSwapchainKHR` 等）⇒ `render`/`tri`/`ahb` 退出 | 这 5 个按「本模式未启用 swapchain」处理为可选 |
| **D** | 诊断增强（非 bug） | 需要验证 IMPLEMENTATION_DEFINED 路径 | 新增 `--fmt=0xNN` 与 `mode=winimpdef` |

- 服务器原件已备份：`/root/research/probe10/panvk_wsi_probe.c.orig`、`panvk_wsi_probe.orig`
- 只在服务器重编（NDK r27c，`/opt/android-ndk-r27c`，`bash build.sh`，带 `-DHAVE_TRIANGLE`）
- 修补后二进制 277800 B，md5 `f735e1f4d03c40f248e76e02565f51e6`（**本报告所有数据都出自这个二进制**）
- 未触碰 `/root/mesa`、`/root/MobileGL` 及任何 build 目录

### ⚠️ 重要操作告诫：`--mode=all` 不可用
`all` 里 `tri` 排在最前，它的 CSF 掉线会**污染同进程后续所有步骤**：同一次 `all` 运行里
`tri` 之后连 `render` 的 `vkQueueSubmit` 都变成 `-4 (VK_ERROR_DEVICE_LOST)`。
⇒ **必须一模式一进程**（本报告即如此）。这也解释了任务为什么强调「先 render … 最后才 win」。

---

## 2. 逐步 VkResult 结果总表

`-1000072003` = `VK_ERROR_INVALID_EXTERNAL_HANDLE`；`-4` = `VK_ERROR_DEVICE_LOST`。

| 模式 | 结果 | 关键 VkResult 链 |
|---|---|---|
| **render** | ✅ **PASS** failures=0 | createImage 0 → AllocateMemory 0 → BindImageMemory 0 → **Submit 0 → WaitForFences 0** → 三点像素精确 |
| **tri** | ❌ **FAIL** failures=1 | 整条管线全 0（renderPass/framebuffer/shaderModule×2/pipelineLayout/graphicsPipelines）→ Submit 0 → **WaitForFences −4 DEVICE_LOST** |
| **ahb**（fmt=0x1） | ✅ **PASS** failures=0 | AHB_allocate 0 → createImage 0 → **vkGetAHBProps 0**（allocSize=16384 externalFormat=37）→ **AllocateMemory 0** → Bind 0 → Submit 0 → Fences 0 → 三点像素精确 |
| **ahbimpdef**（fmt=0x22） | ❌ **FAIL** failures=1（**可复现 ×2**） | AHB_allocate 0 → createImage 0 → **vkGetAHBProps −1000072003 VK_ERROR_INVALID_EXTERNAL_HANDLE**（allocSize=0 format=UNDEFINED） |
| **mapper** | ✅ PASS（无 Vulkan） | SPHAL 取到 handle → `AIMapper_loadIMapper rc=0 version=5` → `importBuffer rc=0` → 5 类 metadata 全部返回正值 |
| **headless** | ✅ **PASS** failures=0 | HeadlessSurface 0 → support 0(supported=1) → caps 0 → formats 4 → presentModes 2 → **CreateSwapchainKHR 0** → images 4 → Acquire 0 → Present 0 |
| **win**（fmt=0x1） | ✅ **PASS** failures=0 | AndroidSurface 0 → caps 0 → **CreateSwapchainKHR 0** → images 2 → Acquire 0 → Submit 0 → Present 0 → acquireNextImage 0 → **窗口像素精确** |
| **winimpdef**（reader fmt=0x22） | ✅ PASS（swapchain 段） | ANativeWindow format=34(0x22) → **CreateSwapchainKHR 0** → Present 0 →（PRIVATE 不可 CPU 读，平面数据不可用） |
| win `--fmt=0x23`（YUV420） | swapchain 段 PASS | **CreateSwapchainKHR 0**、Present 0；但 `AImageReader_acquireNextImage -> -10000`（post 未落地） |

---

## 3. 逐模式原始证据

### 3.1 render —— 驱动渲染的独立证明（不需要 surface / Activity / root）

```
== mode render: image -> clear -> CopyImageToBuffer -> CPU readback
  vkCreateImage(own, optimal, 64x64 RGBA8, color|src|dst) -> 0 (VK_SUCCESS)
  image mem req: size=73728 typeBits=0x7 align=4096
  vkAllocateMemory(image) -> 0 (VK_SUCCESS)
  vkBindImageMemory -> 0 (VK_SUCCESS)
  [render] vkQueueSubmit(clear+copy) -> 0 (VK_SUCCESS)
  [render] vkWaitForFences(5s) -> 0 (VK_SUCCESS)
  [render] pixel(0,0) =  64 128 191 255  (want  64 128 191 255)
  [render] pixel(32,32) =  64 128 191 255  (want  64 128 191 255)
  [render] pixel(63,63) =  64 128 191 255  (want  64 128 191 255)
  [render] PIXEL PASS (clear colour read back exactly)
=== SUMMARY mode=render failures=0 ===
```

**判据（64/128/191/255）三点全部精确命中**，且**不是 R/B 互换**（互换分支没被触发，走的是 exact 分支）。
⇒ Mesa PanVK 在这台无 root 的 PHZ110 上**真的把命令提交给了 GPU 并等到了完成**，回读的像素就是 GPU 写的。

MESA logcat：`No gralloc hwmodule detected (video buffers won't be supported)` +
`Using fallback gralloc implementation`（**没有** `Failed to get u_gralloc_buffer_basic_info`）。

### 3.2 ahb（fmt=0x1）—— u_gralloc 环全绿

```
  AHardwareBuffer_allocate(fmt=0x1 usage=0x303) -> rc=0
  AHardwareBuffer_describe: w=64 h=64 stride=64 fmt=0x1 usage=0x303 layers=1
  native handle: version=12 numFds=3 numInts=60   (fd sizes 0 / 16384 / 6480)
  vkCreateImage(EXTERNAL AHB handleType) -> 0 (VK_SUCCESS)
  vkGetAndroidHardwareBufferPropertiesANDROID -> 0 (VK_SUCCESS) allocSize=16384 typeBits=0x3 format=R8G8B8A8_UNORM externalFormat=37
  >>> vkAllocateMemory(import AHB, dedicated image) -> 0 (VK_SUCCESS)   <== the u_gralloc step
  vkBindImageMemory(ahb) -> 0 (VK_SUCCESS)
  [ahb] vkQueueSubmit(clear+copy) -> 0 (VK_SUCCESS)
  [ahb] vkWaitForFences(5s) -> 0 (VK_SUCCESS)
  [ahb] pixel(0,0)/(32,32)/(63,63) = 64 128 191 255  → PIXEL PASS
=== SUMMARY mode=ahb failures=0 ===
```

驱动侧 `MESA` tag（§8 判据表命中情况）：

```
[P0A-V19-FULLPLANE] init how=SPHAL rc=0 mapper=0x790b2ec9c0 version=5      ← version≥5 ✔
[P0A-V19-FULLPLANE] metadata layer_rc=0 layers=1 fourcc_rc=0 fourcc=0x34324241
                    modifier_rc=0 modifier=0x0000000000000000 alloc_rc=0 alloc=16384
                    planes_rc=0 free_rc=0                                  ← 所有 *_rc=0 ✔
[P0A-V19-FULLPLANE] accepted fourcc=0x34324241 modifier=0x0 planes=1       ← 没有 refusing guessed layout ✔
[P0A-V19-FULLPLANE] plane=0 fd_index=0 offset=0 stride=256 total=16384 sample_bits=32 samples=64x64 sub=1x1
kbase: import flags=0x4040f -> va=0x41000 pages=4 outflags=0x4540f
kbase_kmod_bo_alloc_dmabuf: succeeded for 15, bo_size=16384, handle=15
```

`0x34324241` = ASCII `'AB24'` = DRM `ABGR8888`；`stride=256`=64×4；`alloc=16384`=64×64×4，全部自洽。

### 3.3 ahbimpdef（fmt=0x22）—— 唯一可复现的 VK_ERROR_INVALID_EXTERNAL_HANDLE

```
  AHardwareBuffer_allocate(fmt=0x22 usage=0x303) -> rc=0
  AHardwareBuffer_describe: w=64 h=64 stride=64 fmt=0x22 usage=0x303 layers=1
  native handle: version=12 numFds=3 numInts=60
  vkCreateImage(EXTERNAL AHB handleType) -> 0 (VK_SUCCESS)
  vkGetAndroidHardwareBufferPropertiesANDROID -> -1000072003 (VK_ERROR_INVALID_EXTERNAL_HANDLE)
                                                 allocSize=0 typeBits=0x0 format=UNDEFINED externalFormat=0
=== SUMMARY mode=ahbimpdef failures=1 ===
```

MESA logcat 命中 §8 判据表的**致命项**：

```
E MESA : Failed to get u_gralloc_buffer_basic_info
```

**同一段代码，唯一变量是 AHB 的 format**：`0x1` 全绿，`0x22` 直接失败。
`0x22` = `AHARDWAREBUFFER_FORMAT_IMPLEMENTATION_DEFINED`，正是真实 Android
Surface / ImageReader / 图形缓冲**最常用**的格式。此模式连 `[P0A-V19-FULLPLANE] init how=`
都没打出来 ⇒ 失败发生在 u_gralloc 更早的 `u_gralloc_get_buffer_basic_info` 里。
两次独立运行结果逐字节一致（**可复现**）。

### 3.4 mapper —— 厂商 IMapper V5 本身是好的

```
  openDeclaredPassthroughHal("mapper","mediatek")            -> 0x0
  openDeclaredPassthroughHal("mapper","default")             -> 0x0
  openDeclaredPassthroughHal("android.hardware.graphics.mapper","mediatek") -> 0x0
  openDeclaredPassthroughHal("android.hardware.graphics.mapper","default")  -> 0x0
  dlopen(libvndksupport.so) -> ok
    android_load_sphal_library("mapper.mediatek.so") -> ok
  mapper impl handle obtained via SPHAL
  AIMapper_loadIMapper -> rc=0 mapper=… version=5        ← version≥5，没有 mapper load failed
  AHardwareBuffer_allocate -> rc=0
  IMapper.importBuffer(raw=…) -> 0 imported=…            ← import rc=0
  getStandardMetadata(5 LAYER_COUNT)   -> 77
  getStandardMetadata(7 PIXEL_FORMAT_FOURCC)  -> 73
  getStandardMetadata(8 PIXEL_FORMAT_MODIFIER)-> 77
  getStandardMetadata(10 ALLOCATION_SIZE)     -> 77
  getStandardMetadata(15 PLANE_LAYOUTS)       -> 505
```

**关键观察（原始 blob 布局）**：Binder-NDK 的 `openDeclaredPassthroughHal` 四条路全 NULL，
驱动真正走的是 **SPHAL**（`android_load_sphal_library("mapper.mediatek.so")`）。
拿到的 metadata **不是把值放在偏移 0**，而是一个自描述包装：

```
LAYER_COUNT (77 B):
  0000: 35 00 00 00 00 00 00 00    ← u64 = 53（名字长度）
  0008: "android.hardware.graphics.common.StandardMetadataType"  ← 53 B 类型名
  0061: 05 00 00 00                ← u32 类型 id（= 请求的 5）
  0065: 00 00 00 00
  0069: 01 00 00 00 00 00 00 00    ← 值 = 1（层数）
PIXEL_FORMAT_FOURCC (73 B)： 类型 id 07，尾部 41 42 32 34 = 'AB24'
ALLOCATION_SIZE (77 B)：    类型 id 0a，尾部 = 0x4000 = 16384
PLANE_LAYOUTS (505 B)：     类型 id 0f
```

**这不是内存垃圾，是真的响应**：五个请求返回的类型 id 与请求一一对应；更重要的是，把尾部值
和驱动自己 `[P0A-V19-FULLPLANE]` 解析出来的结果对照——`fourcc=0x34324241`(=`'AB24'`)、
`alloc=16384`、`layers=1` —— **逐项吻合**。⇒ 这个"包装"就是 MTK mapper 的返回约定，
Mesa 的 u_gralloc 是按约定读的，**读对了**（所以 0x1 路径既没有 `refusing guessed layout`
也没有 `Failed to get u_gralloc_buffer_basic_info`）。

### 3.5 headless —— WSI 框架成立

```
  vkCreateHeadlessSurfaceEXT -> 0 (VK_SUCCESS)
  vkGetPhysicalDeviceSurfaceSupportKHR(qfam=0) -> 0 (VK_SUCCESS) supported=1
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR -> 0 (VK_SUCCESS)
    minImageCount=4 maxImageCount=0 currentExtent=4294967295x4294967295
    minExtent=1x1 maxExtent=65536x65536 usage=0x8009f composite=0x3
  vkGetPhysicalDeviceSurfaceFormatsKHR(count) -> 0 (VK_SUCCESS) n=4
    R8G8B8A8_UNORM / B8G8R8A8_UNORM / R8G8B8A8_SRGB / ?
  vkGetPhysicalDeviceSurfacePresentModesKHR(count) -> 0 (VK_SUCCESS) n=2  (MAILBOX, FIFO)
  >>> vkCreateSwapchainKHR(minImageCount=4 64x64 RGBA8 FIFO) -> 0 (VK_SUCCESS)
  vkGetSwapchainImagesKHR(count) -> 0 (VK_SUCCESS) n=4
  vkAcquireNextImageKHR(no sync objects) -> 0 (VK_SUCCESS) index=0
  vkQueuePresentKHR(image 0) -> 0 (VK_SUCCESS)
=== SUMMARY mode=headless failures=0 ===
```

### 3.6 win —— 卡点步骤实测**成功**

```
  AImageReader_new(64x64 fmt=0x1 max=8) -> 0
  AImageReader_getWindow -> 0 win=…
  ANativeWindow: 64x64 format=1
  vkCreateAndroidSurfaceKHR(window) -> 0 (VK_SUCCESS)
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR -> 0 (VK_SUCCESS)
    minImageCount=2 maxImageCount=4 currentExtent=64x64 minExtent=1x1 maxExtent=4096x4096 usage=0x17 composite=0x9
  vkGetPhysicalDeviceSurfaceFormatsKHR(count) -> 0 (VK_SUCCESS) n=3
  vkGetPhysicalDeviceSurfacePresentModesKHR(count) -> 0 (VK_SUCCESS) n=3  (FIFO, MAILBOX, IMMEDIATE)
  >>> vkCreateSwapchainKHR(minImageCount=2 64x64 RGBA8 FIFO) -> 0 (VK_SUCCESS)  <== 原以为是卡点
  vkGetSwapchainImagesKHR(count) -> 0 (VK_SUCCESS) n=2
  vkAcquireNextImageKHR(no sync objects) -> 0 (VK_SUCCESS) index=0
  vkQueueSubmit(clear swapchain image 0) -> 0 (VK_SUCCESS)
  vkQueuePresentKHR(image 0) -> 0 (VK_SUCCESS)
  AImageReader_acquireNextImage -> 0 img=…
  window pixel(32,32) =  64 128 191 255  stride=256  (want 64 128 191 255)
  WINDOW PIXEL PASS (the presented patch reached the window buffer)
=== SUMMARY mode=win failures=0 ===
```

**端到端打通**：交换链建起来、图像取到、clear 提交、present 成功、**并且窗口 buffer 里真的是那个像素**。
驱动侧同样打出完整的 `[P0A-V19-FULLPLANE]` 全绿块 + 两个 `kbase_kmod_bo_alloc_dmabuf: succeeded`。

⚠️ 进程退出时有一条 `FORTIFY: pthread_mutex_lock called on a destroyed mutex (0x…)`——
发生在 post 校验之后，属拆卸期噪声，不影响上述 VkResult。

### 3.7 补充：换 buffer 格式也压不垮交换链

- `winimpdef`（ImageReader 用 `AIMAGE_FORMAT_PRIVATE` = 0x22，`ANativeWindow format=34`）：
  `vkCreateSwapchainKHR -> 0`，present 0，swapchain 段依然 PASS。
  ⇒ Mesa 的 Android WSI 在给交换链备图时**会强制显式格式**，不会把 IMPLEMENTATION_DEFINED 透传下去。
- `win --fmt=0x23`（YUV_420_888）：`vkCreateSwapchainKHR -> 0`、`vkQueuePresentKHR -> 0`，
  但 `AImageReader_acquireNextImage -> -10000`（该 reader 拿不到已 post 的图，属探针校验手段限制）。

---

## 4. `tri` 新发现：真正让 GPU 掉线的是「绘制」，不是 WSI

```
  ... vkCreateRenderPass/Framebuffer/ShaderModule×2/PipelineLayout/GraphicsPipelines -> 全部 0 (VK_SUCCESS)
  [tri] vkQueueSubmit(draw) -> 0 (VK_SUCCESS)
  [tri] vkWaitForFences(5s) -> -4 (VK_ERROR_DEVICE_LOST)
=== SUMMARY mode=tri failures=1 ===
```

MESA logcat：

```
E MESA : kbase: CSF group 0 fatal error: status 0x7dc002c3 (exception 0xc3), sideband 0x0000005fffe1e000
E MESA : kbase: CSF group 1 fatal error: status 0x7dc002c3 (exception 0xc3), sideband 0x0000005fffe1e000
E MESA : kbase: CSF group 2 fatal error: status 0x7dc002c3 (exception 0xc3), sideband 0x0000005fffe1e000
W MESA : kbase: received CSF CPU queue dump notification
```

`vkCreateGraphicsPipelines` 全过、`vkQueueSubmit` 也返回成功，**是 GPU 固件（CSF）在真正执行 draw 时炸了**
（三个 CSF group 同时 fatal，exception 0xc3）。而**不含 draw 的 clear+copy 路径完全正常**（§3.1 / §3.2）。
⇒ 分界线很清楚：**命令提交与内存/导入链路是通的；出问题的是图形管线的实际光栅化执行**。

---

## 5. 判据表（§8）逐项对照

| §8 判据 | render | ahb(0x1) | ahbimpdef(0x22) | headless | win | mapper |
|---|---|---|---|---|---|---|
| `mapper load failed` | — | 无 | 无 | — | 无 | **无** |
| `init how= rc= version=` | — | `SPHAL rc=0 version=5` ✔ | **未到达此步** | — | `SPHAL rc=0 version=5` ✔ | `rc=0 version=5` ✔ |
| `import rc=` | — | — | — | — | — | `importBuffer rc=0` ✔ |
| `metadata *_rc=` | — | 全 0 ✔ | — | — | 全 0 ✔ | 5/5 返回正值 |
| `refusing guessed layout` | 无 | **无** | 无 | 无 | **无** | 无 |
| `Failed to get u_gralloc_buffer_basic_info` | 无 | 无 | **命中（致命）** | 无 | 无 | 无 |

---

## 6. 交付判定

**① 驱动渲染是否被独立证明：是。**
`render` 模式（无 surface、无 Activity、无 root）`vkQueueSubmit` → `vkWaitForFences` 均
`VK_SUCCESS`，clear 色 64/128/191/255 在 (0,0)/(32,32)/(63,63) 三点**精确**回读，`failures=0`。
`ahb(0x1)` 复现了同样结果；`win` 进一步证明 present 的像素**真的落到了窗口 buffer**。
⇒ Mesa PanVK 在本机**确实能渲染**，这条不再是假设。

**② 卡点确认为哪一步：**
- 原假设「`vkCreateSwapchainKHR` 失败 / `VK_ERROR_INVALID_EXTERNAL_HANDLE`」**在合成探针里不能复现**：
  `win`、`headless`、`winimpdef` 三条 surface 路径下 `vkCreateSwapchainKHR` **全部 `VK_SUCCESS`**，
  `win` 连 present + 窗口像素校验都过了。交换链创建这一步**本身不是无条件坏的**。
- 唯一可复现的 `VK_ERROR_INVALID_EXTERNAL_HANDLE` 被钉在：
  **`AHardwareBuffer` 以 `IMPLEMENTATION_DEFINED`(0x22) 分配时**，u_gralloc 取 basic info 失败
  （`Failed to get u_gralloc_buffer_basic_info`）→ `vkGetAndroidHardwareBufferPropertiesANDROID`
  返回 `-1000072003` → Mesa 向外传播成 `VK_ERROR_INVALID_EXTERNAL_HANDLE`。
  同一路径换成显式 `0x1` 即全绿。**这就是「u_gralloc 环」真正的断点，且只断在 0x22 上。**
- 附带新发现：**真正会让 GPU 掉线的是绘制**（`tri`: `VK_ERROR_DEVICE_LOST` + CSF fatal 0xc3）。
  这条比 WSI 更值得优先处理——即使交换链建起来了，只要真的画东西，GPU 就掉。

**③ 需要人做什么：无。** 全部步骤（取回、落地、8 个模式运行、logcat 采集、重编）均已无人完成，
未使用任何真实屏 UI 操作，未触碰网易云音乐 / Stellar / DSH 本体及其服务。
若要把结论推广到**真实 App（如网易云音乐）**，那需要在允许操作真实屏 UI 的会话里做一次端到端复现——
本会话（virtual-only、禁止真实屏 UI）**没有也无法**做这一步。

---

## 7. 复现清单（服务器 `/root/research/probe10/`）

```
build.sh  panvk_wsi_probe.c  panvk_wsi_probe        ← 修补后的（md5 f735e1f4d03c40f248e76e02565f51e6）
panvk_wsi_probe.c.orig  panvk_wsi_probe.orig        ← 原始带有 A/B/C 三个 bug 的版本
```

设备侧：`/data/local/tmp/{panvk_wsi_probe,runner.sh,libvulkan_panfrost.so}`
（`runner.sh <mode> [额外参数]`，内部 `logcat -c` → 跑 → `logcat -d -s MESA:V P10PROBE:V '*:S'`）。

建议的下一步顺序（每模式独立进程，**不要用 `--mode=all`**）：
`render` → `ahb` → `ahbimpdef` → `mapper` → `headless` → `win` → `winimpdef`。
`ahb` vs `ahbimpdef` 是唯一变量为 AHB format 的对照实验，是定位 u_gralloc 断点的关键对照组。
