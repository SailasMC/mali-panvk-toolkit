# 07 — MobileGL(DirectVulkan/Magma) 侧有没有"绕开交换链"的路

> 源码：`/root/MobileGL`（只读）。本文件的所有 文件:行号 均针对该 checkout（HEAD `08124c9`）。
> 我们实际发布的产物：`/root/mglplug/lib/arm64-v8a/libMobileGL.so`（16.9 MB，`CMAKE_BUILD_TYPE=Release`）。
> 结论先行：**MGL(DirectVulkan) 在架构上把默认帧缓冲直接绑死到交换链图像上，没有任何非 WSI 的呈现路径。**
> 所以"只改 MGL 配置就让画面出来"——**不存在**。下面给出全部证据与可行性排序。

---

## 1) 所有交换链 / surface 调用点，谁创建、什么条件

### 1.1 调用点清单（全量，已排除 3rdparty）

`MobileGL/MG_Backend/DirectVulkan/Renderer/SwapchainObject.cpp`

| 行 | 调用 | 所在函数 |
|---|---|---|
| 95 | `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` | `GetSwapchainCapabilities` |
| 99, 102 | `vkGetPhysicalDeviceSurfaceFormatsKHR` | 同上 |
| 107, 111 | `vkGetPhysicalDeviceSurfacePresentModesKHR` | 同上 |
| **279** | **`vkCreateSwapchainKHR`** | `SwapchainObject::Create` |
| **282, 285** | **`vkGetSwapchainImagesKHR`**（先查数量再填充） | 同上 |
| 455 | `vkDestroySwapchainKHR` | `SwapchainObject::Shutdown` |

`MobileGL/MG_Backend/DirectVulkan/Renderer/FrameContext.cpp`

| 行 | 调用 | 所在函数 |
|---|---|---|
| **297** | **`vkAcquireNextImageKHR`** | `WaitAndAcquireNextImage` |

`MobileGL/MG_Backend/DirectVulkan/Renderer/VulkanRenderer.cpp`

| 行 | 调用/含义 | 所在函数 |
|---|---|---|
| **12979** | **`vkQueuePresentKHR`** | `Present` |
| 12989, 13001, 13056, 13067, 3219 | 仅日志/`VK_VERIFY` 文本里出现 "vkAcquireNextImageKHR"，**不是调用** | `Present` / `Initialize` |
| 3347 | `vkDestroySurfaceKHR` | `Shutdown` |
| 14537 | `CreateSurface()` 定义 | — |
| 14634 | `vkCreateAndroidSurfaceKHR` | `CreateSurface`（窗口路径 + AImageReader 回退路径） |
| 14550 | `vkCreateHeadlessSurfaceEXT` | `CreateSurface`（无窗口 + 有 `VK_EXT_headless_surface`） |
| 14715 | `vkCreateXlibSurfaceKHR` | `CreateSurface`（X11 窗口） |

> 注意：`VulkanRenderer.cpp` 里**没有**直接调 `vkAcquireNextImageKHR`——它一律走
> `m_frameContext.WaitAndAcquireNextImage`（调用点 3215、3222、12904、13045）。

### 1.2 谁创建交换链

**唯一创建者**是 `SwapchainObject::Create`，**唯一调用者**是
`VulkanRenderer::CreateSwapchain()`（`VulkanRenderer.cpp:14507`，调用点在 `:14512`）。

`CreateSwapchain()` **只被** `VulkanRenderer::RecreateSwapchain()`（`:14883`）调用；
`RecreateSwapchain()` 共 **6 个调用点**：

| 行 | 场景 | 返回值 |
|---|---|---|
| **3134** | `VulkanRenderer::Initialize()` 结尾（首次建链） | **被丢弃** ⚠ |
| **3220** | `Initialize()` 首次 acquire 遇 `VK_ERROR_OUT_OF_DATE_KHR` 后重建 | **被丢弃** ⚠ |
| 12880 | `Present()` 里延迟的首次 acquire 失败后重建 | 检查 |
| 12990 | `Present()` 收到 `VK_ERROR_OUT_OF_DATE_KHR` 后重建 | 检查 |
| 13032 | `Present()` 尾部按需 resize / 换 present mode | 检查 |
| 13057 | `Present()` 第 4 步 acquire 遇 OUT_OF_DATE 后重建 | 检查 |

### 1.3 创建条件（DirectVulkan 后端）

**前置条件链**

1. **必须显式选后端**：`MOBILEGL_BACKEND_TYPE=DirectVulkan`
   （`ConfigLoader.cpp:215-229`）。**默认值是 `DirectGLES`**（`:217` 的 `defaultValue`），
   不设这个变量，Magma 这条链根本不会启动。
2. 必须有 `VulkanRenderer` 实例，两条构造路径（`BackendObject_DirectVulkan.cpp`）：
   - **窗口路径** `InitWindowSurface()` `:336-353` → `VulkanRenderer(nativeWindow, config)`（`:349`），`m_window != null`；
   - **pbuffer 路径** `InitPbufferSurface()` `:355-366` → `VulkanRenderer(NativeWindowType{}, config)`（`:362`），`m_window == null`。
3. `pVulkanRenderer->Initialize()`（`:351` / `:364`）→ `VulkanRenderer::Initialize()`（`:3025`）顺序：
   `CreateInstance()`(3026) → `CreateSurface()`(3027) → `PickPhysicalDevice()` →
   `CreateLogicalDeviceAndQueues()` → `CreateAllocator()` → `CreateCommandPool()` →
   frame context → `RecreateSwapchain()`(3134)。
4. **逻辑设备强制要求 `VK_KHR_swapchain`**：`VulkanRenderer.h:1497`
   `s_deviceExtensionNames[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME}`。
5. `RecreateSwapchain()` 在 `currentExtent` 为 0×0 时**直接返回 false**（`:14862-14868`）
   ——即窗口最小化态不建链。

**`SwapchainObject::Create` 内部条件（`SwapchainObject.cpp:171-355`）**

- 需要 `IsComplete()`：`surfaceFormats` 与 `presentModes` 均非空（`:22-25`）；检查仅是 `MOBILEGL_ASSERT`（`:175`）。
- `minImageCount = clamp(max(minImageCountHint, caps.minImageCount), 上界 caps.maxImageCount)`（`:198-201`）。
  `minImageCountHint` = `m_config.MaxFramesInFlight`，来自 `MOBILEGL_MAGMA_FRAMESINFLIGHT`，
  并在 `Initialize()` 里按 surface 的 `maxImageCount` 夹取、下限 2（`VulkanRenderer.cpp:3126-3164`）。
- `imageExtent`：优先用 `currentExtent`；为 `UINT32_MAX` 时用被 clamp 的 `desiredExtent`（`:211-219`）；
  **`ROTATE_90/270` 时宽高互换**（`:221-224`）。
- `imageUsage` 必须含 `COLOR_ATTACHMENT | TRANSFER_DST`（`:227-232`，仅 assert）；
  支持时再加 `TRANSFER_SRC`（`:235-237`）。
- `preTransform = caps.currentTransform`（`:251`）；
  `compositeAlpha` 取 OPAQUE/PRE/POST/INHERIT 中第一个被支持的（`:253-265`）。
- `presentMode` 由 `ChooseSwapchainPresentMode`（`:139-165`）按 `eglSwapInterval` 排序后选取。
- 建链后立即 `CreateImageViews()`（`:532`）与 `CreateDepthStencilResources()`（`:357`），
  然后把默认 FBO 的 color/depth/stencil **当纹理**做 `AllocateStorage`（`:297-354`）。

---

## 2) 关键问题：能离屏渲染而不建交换链吗？画面怎么出来？

### 2.1 答案：**FBO 可以，默认帧缓冲不行；且没有任何 blit/copy 能把离屏结果送到屏幕**

**(a) 默认帧缓冲物理上就是交换链图像——不是"渲染到纹理再拷到交换链"。**

`VkRenderPassManager::GetOrCreateRenderPass`：

- `:892` 注释原文：`Default framebuffer attachments are frontend placeholders; Vulkan framebuffer extent must match the swapchain.`
- `:893-896` 默认 FBO 的 framebuffer 尺寸**取自交换链**：
  `ResolveRenderPassFramebufferExtent(isDefaultFbo, ...)` 定义在 `:199-205`——
  `isDefaultFbo` 时 `return {swapchainExtent.width, swapchainExtent.height}`，**只有非默认 FBO 才用 attachment 自身尺寸**。
- `:1056-1058` 默认 FBO 的 `finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`（直接可呈现）。
- **`:1077-1096`：`isDefaultFbo` 时 attachment view 被替换为
  `m_swapchainObject.GetImageViews()[swapchainImageIndex]`（`:1096`）**，
  格式取 `m_swapchainObject.GetSurfaceFormat().format`（`:1042-1043`）。

也就是说 `SwapchainObject::Create:297-354` 分配的 color/depth/stencil 纹理只是**前端占位符**，
Vulkan 侧从不渲染到它们。**MGL 里不存在"默认 FBO 纹理 → 交换链图像"的 blit/copy。**

**(b) 非默认（离屏）FBO 完全不依赖交换链。**

`VkRenderPassManager.cpp:1097-1137`：非默认 FBO 走
`m_textureManager.SyncTextureAndGetDescriptor(*texture)`（`:1098`）或 renderbuffer 资源（`:942`），
格式来自 `ConvertTextureInternalFormatToVkFormat`（`:1044-1045`），
`finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`（`:1058`），
framebuffer 尺寸来自 attachment（`:1070-1075`）。
**整条路径没有一处触碰 `m_swapchainObject`**（唯一例外是 `:1210-1211` 默认 FBO 的深度布局）。
→ 交换链图像数为 0 时，**纯离屏 FBO 渲染依然成立**。

**(c) 存在的 blit/copy 路径（都是通用 GL 语义，不是呈现路径）**

- `VulkanRenderer::InitializeBlitResources()` `:4221` / `ShutdownBlitResources()` `:4293`
  ——全屏四边形 program（`uSrcRect` / `uDstRect` / `uSurfaceTransform`），服务 `glBlitFramebuffer`，
  **包含默认 FBO 作为源或目的的情形**（见 `:8875` 注释 "a default-framebuffer side forces the vkCmdBlitImage form"），
  实际 `vkCmdBlitImage` 调用在 `:8904`（深度）与 `:9170`（多重采样 resolve）。
- `ReadPixels()` `:9960`，默认 FBO 重映射在 `:10155`（**需要已 acquire 的交换链图像**）。

**(d) 呈现入口只有一条，且强制经过交换链。**

`eglSwapBuffers` → `BackendObject::SwapEGLBuffers`（`BackendObject.cpp:369-398`）
→ `backendFunctions.Present()`（`:396`）→ `VulkanRenderer::Present()` → `vkQueuePresentKHR`（`:12979`）。
`SwapchainObject` 为 NULL 时 `Present()` 唯一出路是 `m_presentSuspended = true` 并 return（`:12899-12901`、`:13035-13037`）。

### 2.2 "离屏模式"的开关：**不存在**

- 全树枚举 `MOBILEGL_*` token + 全部 `QueryEnv*` 调用点：**没有任何
  `*HEADLESS*` / `*NO_WSI*` / `*OFFSCREEN*` / `*PRESENT*` / `*SURFACELESS*` 变量**。
- MGL 里唯一的离屏入口是 **EGL pbuffer**：
  `eglCreatePbufferSurface`（`EGLImpl.cpp:453-475`）
  → `BackendObject::RegisterEGLPbufferSurface`（`BackendObject.cpp:242-263`，仅登记与尺寸校验）
  → `ActivateEGLSurface`（`:288-292`）→ `InitPbufferSurface`（`BackendObject_DirectVulkan.cpp:355-366`）
  → `VulkanRenderer(NativeWindowType{}, config)`。
- **但 pbuffer 路径同样要建交换链**：`m_window == null` 时 `CreateInstance()`
  在 Android 上检查 `VK_EXT_headless_surface`（`VulkanRenderer.cpp:13156-13169`）：
  - 有 → `vkCreateHeadlessSurfaceEXT`（`:14544-14552`，**仍然建交换链**，headless surface 支持 swapchain）；
  - **没有（Mali/我们的 PanVK 走这条）** → 构造一个 **AImageReader 的 ANativeWindow**
    （`:14553-14615`：`dlopen("libmediandk.so")` → `AImageReader_new(W,H,RGBA_8888,8)` → `AImageReader_getWindow`），
    然后落到 `:14634` 的 `vkCreateAndroidSurfaceKHR`。

> **结论：MGL 没有真正的 "no-WSI" 模式。** 连"离屏 pbuffer"最终也变成一个假的 Android 窗口上的交换链。
> 因此仅靠 MGL 自身，"离屏渲染 + 自己把画面送出去"这条链是**断的**。

---

## 3) 与后端行为有关的全部环境变量 / 宏

### 3.1 运行期环境变量（`MG_ConfigLoader::Init()` 一次性解析，`ConfigLoader.cpp:231-240`）

> 只接受 `LIBGL_` / `MOBILEGL_` 前缀的变量（`:27-29`），其余对 MGL 不可见。
> 布尔统一规则：已设置、非空、非 `"0"`、非 `"false"`（大小写不敏感）为真（`:74-82`）。

**后端选择（决定是否走 WSI）**

| 变量 | 取值 | 与 WSI 的关系 |
|---|---|---|
| `MOBILEGL_BACKEND_TYPE` | `DirectGLES`(**默认**) / `DirectVulkan` / `Unknown`（`:215-229`） | 必须为 `DirectVulkan` 才有交换链；`DirectGLES` **完全绕开 Vulkan WSI** |

**DirectVulkan / Magma**

| 变量 | 默认 / 范围 | 含义 | 规避 WSI 的取值 |
|---|---|---|---|
| `MOBILEGL_MAGMA_FRAMESINFLIGHT` | 3，[1,64]（`:179`） | 同时决定 `MaxFramesInFlight` **和 `SwapchainObject::Create` 的 `minImageCountHint`** | **唯一能改变交换链形态的变量**。设 2 让交换链图像数最小化（若 WSI 对图像数敏感）。不能取消交换链 |
| `MOBILEGL_MAGMA_MAX_DRAWS_PER_COMMAND_BUFFER` | 16384，0=无限（`:180-181`） | 单个 command buffer 里最大 draw/dispatch 数，超出即切分提交 | 无 WSI 作用 |
| `MOBILEGL_MAGMA_DESCRIPTOR_TRIM_FRAMES` | 120（`:182`） | 连续多少帧 descriptor pool 低于 1/4 才回收 | 无 |
| `MOBILEGL_MAGMA_DISABLE_ROBUST_BUFFER_ACCESS` | off（`:200`） | 关闭 `robustBufferAccess` 设备特性 | 无 |
| `MOBILEGL_MAGMA_DISABLE_SUBGROUP` | off（`:171`） | 强制禁用 subgroup 及仿真 | 无 |
| `MOBILEGL_MAGMA_EMULATE_SUBGROUP` | off（`:172`） | 无原生 subgroup 时用共享内存仿真 | 无 |
| `MOBILEGL_MAGMA_DERIVE_NUM_SUBGROUPS` | Auto（`:176`） | 用 `ceil(local_size/subgroupSize)` 替换 `gl_NumSubgroups` | 无 |
| `MOBILEGL_MAGMA_FIX_ITERATIONRP_SUBGROUP_SCRATCH` | Auto（`:173-174`） | 修 iterationRP 的 shared 越界 | 无 |
| `MOBILEGL_MAGMA_ITERATIONRP_FIX_BARRIER` | off（`:175`） | 补 iterationRP 缺失的 workgroup barrier | 无 |
| `MOBILEGL_MAGMA_MULTIDRAW_MODE` | Auto，`ext\|indirect\|unroll`（`:201`） | 多绘制的 dispatch 层级 | 无 |
| `MOBILEGL_MAGMA_R11G11B10F_FALLBACK` | off（`:178`） | R11G11B10F 用回退格式 | 无 |
| `MOBILEGL_MAGMA_DISABLE_BLENDED_DEPTH_WRITE` | Auto（`:198-199`） | 叠加混合管线是否抑制深度写 | 无 |
| `MOBILEGL_MAGMA_PRIMGEN_QUERY_REROUTE` | Auto（`:212`） | `GL_PRIMITIVES_GENERATED` 重路由 | 无 |

**跨后端 / 通用**

`MOBILEGL_DISABLE_TIMERQUERY`(`:164`)、`MOBILEGL_ENABLE_SPIRV_VALIDATION`(`:166`)、
`MOBILEGL_ADVERTISE_FP64`(`:177`)、`MOBILEGL_RELAXED_SEMANTICS`(`:197`)、
`MOBILEGL_COHERENT_AS_FLUSH`(`:188`)、`MOBILEGL_DISABLE_LARGE_BUFFER_ADOPTION`(`:194`)、
`MOBILEGL_POINT_SIZE_DEMOTION`(`:187`)、`MOBILEGL_SHADER_CACHE`(`:207`)、
`MOBILEGL_ASYNC_SHADER_COMPILE`(`:203`)、`MOBILEGL_ASYNC_SHADER_COMPILE_THREADS`(`:204`)、
`MOBILEGL_ASYNC_OPTIMISTIC_SHADER_STATUS`(`:205-206`)、`MOBILEGL_TRACE_SKIP_AUTODESTROY`(`:189`)、
`MOBILEGL_TRACE_ANGLE_VARIANT`(`:169`，仅当编译期定义 `MOBILEGL_TRACE_ANGLE_VARIANTS`)。

**DirectGLES（Espryt）——不涉及 Vulkan WSI，但能换掉"谁提供 surface/present"**

`MOBILEGL_ESPRYT_USE_ANGLE`(`:167`) —— **加载 ANGLE 的 EGL/GLES 库**（`Config.h:82-83`）。
这是除 `MOBILEGL_BACKEND_TYPE` 外**第二个纯配置就能换掉 surface/present 所有者的开关**。
其余：`MOBILEGL_ESPRYT_ENABLE_TEXTURE_VIEW`(`:165`)、`MOBILEGL_ESPRYT_AVOID_SAMPLER_MIPMAP_MIN_FILTER`(`:183-184`)、
`MOBILEGL_ESPRYT_AVOID_EXPLICIT_LOD_BIAS`(`:185`)、`MOBILEGL_ESPRYT_UNLOCATED_IO_BLOCKS`(`:186`)、
`MOBILEGL_ESPRYT_DISABLE_UBO_RING`(`:190`)、`MOBILEGL_ESPRYT_DISABLE_UNPACK_RING`(`:191`)、
`MOBILEGL_ESPRYT_DISABLE_UPLOAD_RING`(`:192`)、`MOBILEGL_ESPRYT_DISABLE_INVALIDATE_FLUSH`(`:193`)、
`MOBILEGL_ESPRYT_FORCE_DS_READBACK_EMULATION`(`:195-196`)、
`MOBILEGL_ESPRYT_FORCE_VIEWPORT_ARRAY_EMULATION`(`:208-209`)、
`MOBILEGL_ESPRYT_WIDEN_PACKED16_STORAGE`(`:210-211`)、`MOBILEGL_ESPRYT_MULTIDRAW_MODE`(`:202`)。

**绕开 ConfigLoader 的 live `getenv`**

| 变量 | 位置 | 说明 |
|---|---|---|
| `MOBILEGL_LOG_FILE_PATH` | `MG_Util/Debug/Log.cpp:68` | 直接指定日志文件（**我们排障最该用的一个**） |
| `MOBILEGL_DISABLE_TEXTURE_VIEW` | `MG_Util/BackendLoaders/OpenGL/Loader.cpp:1026` | DirectGLES 关闭 texture view |
| `DISPLAY` | `MG_State/EGLState/Core.cpp:35`、`VulkanRenderer.cpp:14683` | 仅 X11 路径 |
| `MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS` | `VulkanRenderer.cpp:13090`（`setenv`） | 仅 `VK_USE_PLATFORM_METAL_EXT` |
| `EGL_PLATFORM`、`MOBILEGL_ITEST_*`、`MOBILEGL_XFB_INVARIANCE_DUMP_DIR` | 仅集成测试 harness | 与我们无关 |

**编译期宏（不是环境变量，别混淆）**

`MOBILEGL_LOG_ACTIVE_LEVEL`（我们 = `MOBILEGL_LOG_LEVEL_INFO`）、
`MOBILEGL_LOG_ENABLE_FILE|CONSOLE|ANDROID|STACKTRACE`（`Log.cpp:64/80/92/112/117/133` 的 `#if`）、
`MOBILEGL_GL_API|EGL_API|GLX_API|WGL_API|CGL_API|NSOPENGL_API|EXTERNAL_GLES|API`、
`MOBILEGL_HAS_VK_ENUM_STRING_HELPER`、`MOBILEGL_IOS`、`MOBILEGL_DEFINE_FRAMEBUFFER_DEFAULT_SETTER`、
`MOBILEGL_ENABLE_LTO`、`MOBILEGL_TRACE_ANGLE_VARIANTS`、`MOBILEGL_EXPORT`。

### 3.2 真正能"规避 WSI"的旋钮在**驱动侧**（PanVK/Mesa，不是 MGL）

> 任务限定"不碰驱动"，所以这些只作为判定/对照手段列出——它们是**环境变量**，不改 PanVK 代码。

- **`MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`** ← 最关键的一个
  - 定义/读取：`/root/mesa/src/vulkan/wsi/wsi_common.c:296-298`
    `wsi->force_headless_swapchain = debug_get_bool_option("MESA_VK_WSI_HEADLESS_SWAPCHAIN", false);`
  - 生效点：`/root/mesa/src/vulkan/wsi/wsi_common.c:1434`（`wsi_CreateSwapchainKHR` 内）
    ```c
    struct wsi_interface *iface = wsi_device->force_headless_swapchain ?
       wsi_device->wsi[VK_ICD_WSI_PLATFORM_HEADLESS] :
       wsi_device->wsi[surface->platform];
    ```
  - 含义：**任何 surface（含 android surface）建交换链时都改走 Mesa 的 headless WSI**
    （`wsi_common_headless.c`）——图像落在宿主内存，**完全不碰 surfaceflinger / gralloc**。
    - `wsi_headless_swapchain_queue_present`（`wsi_common_headless.c:401-413`）**是空操作，直接返回 `VK_SUCCESS`**；
    - `wsi_headless_swapchain_acquire_next_image`（`:360-399`）纯宿主记账。
  - 效果：MGL 的整条 WSI 流程（建链 → acquire → 渲染 → present）**全部成功、不崩**，
    但 `queue_present` 什么都不做 → **黑屏**。
  - 价值：把"WSI 建链失败导致崩溃"降级为"干净跑完但不出画"，
    从而证明**Magma 其余部分是好的、坏的只有 WSI**；也是第 4 项方案的基础。
- `MESA_VK_WSI_PRESENT_MODE`（`wsi_common.c:279-295`，`fifo|relaxed|mailbox|immediate`）——强制 present mode。
- `VK_ICD_FILENAMES`——我们已经在用（指向自己的 PanVK）。

**已核实的 PanVK 能力**（`/root/v45/lib/arm64-v8a/libvulkan_freedreno.so`，与 v42/shimv25 md5 相同）：
`VK_KHR_swapchain` ✅、`VK_KHR_android_surface` ✅（`panvk_CreateAndroidSurfaceKHR`）、
**`VK_EXT_headless_surface` ✅**（`vkCreateHeadlessSurfaceEXT` / `wsi_headless_*`）。
→ 所以 MGL 侧"缺 extension"不是本期失败原因；失败发生在 android WSI 的**能力查询/建链**内部。

---

## 4) 失败处理：`MOBILEGL_ASSERT` 在 Release 下为空宏，与其它的"失败后继续执行"

### 4.1 已实测确认：我们的 `.so` 里 assert 被**完全编译掉**

- `Defines.h:105-114`：`MOBILEGL_ASSERT` 仅当 `MOBILEGL_LOG_ACTIVE_LEVEL <= MOBILEGL_LOG_LEVEL_DEBUG(0)`
  才有实现（`MGLOG_F` → `TRAP`）；否则 `#define MOBILEGL_ASSERT(condition, ...)` —— **展开为空**。
- 构建配置：`/root/MobileGL/build-android/CMakeCache.txt`
  `CMAKE_BUILD_TYPE:STRING=Release`、`MOBILEGL_LOG_ACTIVE_LEVEL:STRING=MOBILEGL_LOG_LEVEL_INFO`。
- 实测：`strings /root/mglplug/lib/arm64-v8a/libMobileGL.so | grep -c 'Assertion failed'` → **`0`**。
  （对照：同一 `.so` 里 `Vulkan error %s` 存在，`Swapchain created, extent = %dx%d...` 存在。）

### 4.2 `VK_VERIFY` 才是真正的坑：**日志之后继续往下跑**

`MG_Backend/DirectVulkan/VkIncludes.h:89-101`：

```c
#define VK_VERIFY(expr, ...) do {
    VkResult _vk_verify_result = (expr);
    if (_vk_verify_result != VK_SUCCESS) {
        __VA_OPT__(MGLOG_F(__VA_ARGS__);)
        MGLOG_F("Vulkan error %s (%d) at %s:%d", ...);
    }
    MOBILEGL_ASSERT(_vk_verify_result == VK_SUCCESS, ...);   // Release 下 = 空
} while (0)
```

**没有 `return`、没有 `throw`、没有 `goto fail`。** 头文件自己都写明了这一点
（`:82-88`："A soft, recoverable failure must therefore NOT be routed through VK_VERIFY"），
但交换链代码把所有 Vulkan 调用都塞进了它。

**但它是可见的**——`MGLOG_F` 在 INFO 级构建里是活的（`MOBILEGL_LOG_LEVEL_FATAL=4`，
`ACTIVE=INFO=1 <= 4`），且我们的 `.so` 确实编译进了 Android 日志通道：
`readelf -d libMobileGL.so` 有 `NEEDED liblog.so`，字符串里有日志 tag `MobileGL`
（`MG_Util/Debug/Log.cpp:123` `__android_log_print(..., "MobileGL", ...)`）。

→ **不用重编就能看到病灶**：`adb logcat -s MobileGL`（或启动器日志）里 grep
`Vulkan error`——`VK_VERIFY` 会打出
`Vulkan error <VkResultToString> (<code>) at <file>:<line>`。
配合设 `MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log`（`Log.cpp:68`）还能落盘。

### 4.3 "失败后继续执行"的隐患点清单

`SwapchainObject.cpp`（全部在 Release 下静默）：

| # | 行 | 隐患 |
|---|---|---|
| 1 | **175** → **136 / 164** | `IsComplete()`（assert，空）之后立刻 `ChooseSwapchainSurfaceFormat(surfaceFormats)`，其兜底是 **`return availableFormats[0];`**——`surfaceFormats` 为空时是**空向量越界读**；`ChooseSwapchainPresentMode` 的 **`return availablePresentModes[0];`** 同理。**触发条件恰好就是"驱动查不到 surface 格式/present mode"** |
| 2 | **279** → **282** | `VK_VERIFY(vkCreateSwapchainKHR(...))` 失败后 `m_swapchain` 保持 `VK_NULL_HANDLE`，**代码继续**执行 `vkGetSwapchainImagesKHR(device, VK_NULL_HANDLE, ...)`。`Create()` 里**没有任何地方检查 `m_swapchain != VK_NULL_HANDLE`** |
| 3 | **282 / 285** | 数量查询失败时 `imageCount` 保持初值 0 → `m_images.resize(0)`；填充失败时 `m_images` 保持 `VK_NULL_HANDLE`。但 `:297-354` 仍按"交换链尺寸"分配默认 FBO 纹理；0 图像会让后面的 render pass 变成 0 attachment |
| 4 | **86-88** | `FindMemoryType` assert 后 **`return 0`** → 深度图被绑到 memory type 0（无视 `memoryTypeBits`）→ `vkBindImageMemory` 失败被忽略 → 该深度图照样被当 attachment 用 |
| 5 | **229** | 必需 usage 断言后仍以 `COLOR_ATTACHMENT\|TRANSFER_DST` 调 `vkCreateSwapchainKHR`，即使 `supportedUsageFlags` 不允许 → 必然失败，落到隐患 2 |
| 6 | **366** | `m_depthStencilFormat != UNDEFINED` 断言后继续以 `VK_FORMAT_UNDEFINED` 调 `vkCreateImage`/`vkCreateImageView`（逐 image 失败、VK_VERIFY 只记日志）→ `m_depthStencilImageViews[i]` 留 NULL 并被当深度附件视图 |
| 7 | **549** | `VK_VERIFY(vkCreateImageView(...))` 失败后 `m_imageViews[i]` 留 `VK_NULL_HANDLE`；`GetImageViews()` 把它交给 `VkRenderPassManager:1096` → `vkCreateFramebuffer` 失败 / 以 NULL attachment 开 render pass |
| 8 | 467, 472, 477, 483, 495, 500, 505, 510, 515, 521, 527 | 清一色"断言 + 无条件下标"：`m_images` / `m_imageLayouts` / `m_imageContentDefined` / `m_depthStencil*` 的越界读或**越界写** |
| 9 | `Create`(`:171`) 与 `CreateSwapchain`(`:14507`) | **都返回 `void`，没有错误路径**——彻底失败对上层与成功无法区分 |

`SwapchainObject` 之外：

| # | 位置 | 隐患 |
|---|---|---|
| 10 | `FrameContext.cpp:408-409` → **`:249` / `:273`** | `AssertValidSwapchainImageIndex` 仅断言，随后 `m_swapchainImageRenderFinishedSemaphores[swapchainImageIndex]` **越界**。该数组按 `m_swapchainObject.GetImageCount()` 定容（`VulkanRenderer.cpp:14884`），若 `InitializeSwapchainSemaphores` 失败（VK_VERIFY，被忽略）而图像数非 0，数组为空 → 越界 |
| 11 | `VulkanRenderer.cpp:12913-12914` → **`:12979`** | `MOBILEGL_ASSERT(m_imageIndexAcquired < m_swapchainObject.GetImageCount())` 之后直接用该下标 `vkQueuePresentKHR`；`:12886`/`:12889` 的 `GetImageLayout`/`GetImage` 也越界（`SwapchainObject.cpp:495/500`） |
| 12 | **`VkRenderPassManager.cpp:1079-1080` → `:1096`** | `MOBILEGL_ASSERT(swapchainImageIndex < swapchainViews.size())` 之后 **`attachmentViews.emplace_back(swapchainViews[swapchainImageIndex])`**——`m_imageViews` 为空时**空向量越界**。**这是"WSI 建链失败 → 画默认 FBO → 崩溃/垃圾 framebuffer"的直通车** |
| 13 | `VulkanRenderer.cpp:3134` / `:3220` | **丢弃 `RecreateSwapchain()` 的 `Bool`**。初始化期丢弃意味着"Magma 自认为起来了"，且**不会设 `m_presentSuspended`** → 首次 `Present()` 会拿着 NULL swapchain 去 acquire（`FrameContext.cpp:286`） |
| 14 | `VulkanRenderer.cpp:3079-3080`、`:14884-14886` | `FrameContext::Initialize` / `InitializeSwapchainSemaphores` 虽返回 `VkResult`，但调用方一律用 `VK_VERIFY` 包起来 → 失败只记日志，继续用半成品 frame context。（注：`Initialize()` 里**没有** `InitializeSwapchainSemaphores` 调用，只有 `RecreateSwapchain` 里有） |
| 15 | **`VulkanRenderer.cpp:13230-13231`** → `:14634` | 对**每一个必需 instance extension**（含 `VK_KHR_ANDROID_SURFACE_EXTENSION_NAME`）只做 `MOBILEGL_ASSERT(IsExtensionSupported(...))`。Release 下即使 ICD 不支持也照走 `vkCreateAndroidSurfaceKHR`（loader 返回 `VK_ERROR_EXTENSION_NOT_PRESENT`，VK_VERIFY 记日志后继续，`m_surface` 留 `VK_NULL_HANDLE`），接着把 **NULL surface** 交给 `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`。**这正是"Android WSI 缺口"场景的路径** |

---

## 5) 结论：只改 MGL（不碰驱动）让画面出来的可行性排序

### 5.1 结构性事实（决定排序的前提）

**MGL(DirectVulkan) 没有非 WSI 的呈现路径。** 交换链不是可选项：

1. 默认帧缓冲**就是**交换链图像（`VkRenderPassManager.cpp:1077-1096`），不存在默认 FBO 的独立后备纹理；
2. 唯一 present 调用是 `eglSwapBuffers` 后面的 `vkQueuePresentKHR`（`BackendObject.cpp:369-398` → `VulkanRenderer.cpp:12979`）；
3. 全树没有 `AHardwareBuffer` / `ANativeWindow` 直写路径，没有 `VK_ANDROID_native_buffer` 使用，没有 CPU 回读送窗口；
4. 全树没有能关掉 WSI 的 `MOBILEGL_*` 变量。

→ **"靠配置让 MGL 绕开交换链并把画面送出来"不存在。**

### 5.2 排序

| 排名 | 方案 | 改动量 | 能出画面？ | 保留 PanVK 栈？ | 评价 |
|---|---|---|---|---|---|
| **1** | **`MOBILEGL_BACKEND_TYPE=DirectGLES`** | **纯配置，0 行代码** | **能** | ❌ | 完全不走 Vulkan WSI，用真机 GLES 驱动 + `eglSwapBuffers`（`DirectGLES.cpp:10630`）。**最快的"出画面"与最干净的 A/B**：DirectGLES 有画而 Magma 没有 ⇒ 故障被隔离在 Vulkan/WSI 这一半。缺点：放弃 PanVK |
| **2** | `MOBILEGL_BACKEND_TYPE=DirectGLES` + **`MOBILEGL_ESPRYT_USE_ANGLE=1`** | 纯配置 | 能 | ❌ | 同 1，但 GLES 提供方换成 ANGLE（`Config.h:82-83`）。**第二个纯配置就能换掉 surface/present 所有者**的开关 |
| **3** | **`MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`**（驱动侧环境变量，**不改任何代码**） | 环境变量 1 个 | ❌（构造上黑屏） | ✅ | 让 `wsi_CreateSwapchainKHR` 对**任意** surface 都建 Mesa headless 交换链（`wsi_common.c:1434`），`queue_present` 空操作（`wsi_common_headless.c:401-413`）。**MGL 整条 WSI 流程不再碰 surfaceflinger/gralloc 且不再崩**。价值：一行实验即判定"坏的只有 WSI，Magma 其余健康"，并作为第 4 项的地基。可配 `MESA_VK_WSI_PRESENT_MODE` 压住 present mode |
| **4** | **shim 侧换 surface / 改 present 路径** | 写新组件（可复用现成 shim） | **能**（唯一真·绕开交换链且保留全栈的方案） | ✅ | 扩我们自己的 `/root/mgl_icd/vkshim_mgl.c`——**它目前是纯转发+打日志，没有任何 WSI 改写**。但所有 WSI 入口都已挂钩：`:299 vkCreateAndroidSurfaceKHR`、`:515 vkCreateSwapchainKHR`、`:817 vkGetSwapchainImagesKHR`、`:844 vkQueuePresentKHR`，并写 `/sdcard/MG/vkshim.log`。两种做法：<br>**(a) 换 surface**：把 `vkCreateAndroidSurfaceKHR` 重定向到 `vkCreateHeadlessSurfaceEXT`（PanVK 已导出该扩展，已在 `.so` 中核实），让 MGL 永不建 android surface，然后自己实现 present；<br>**(b) 改 present**：在 `vkQueuePresentKHR` 里把 acquire 到的交换链图像拷进 host-visible buffer，用 `ANativeWindow_lock`/`AHardwareBuffer` 推给窗口，或 blit 进第二条"真"surface 的交换链。ANativeWindow 对 shim 可见——MGL 在 `:14634` 是通过 `VkAndroidSurfaceCreateInfoKHR::window` 传进去的。<br>代价：新 present 路径需自己写和调，且有强制 GPU→CPU→窗口 拷贝（慢） |
| **5** | **改 MGL 源码：把默认 FBO 与交换链图像解耦** | 改源码 + 重编 `libMobileGL.so` | 能 | ✅ | 让 `SwapchainObject::Create:297-354` 已分配的默认 FBO 纹理成为**真**目标（而不是前端占位符）：在 `VkRenderPassManager.cpp:1077-1096` 里当没有活交换链时改用纹理视图而非 `swapchainViews[...]`，再补一个"该纹理 → 交换链图像"的 present blit（或送进第 4 项的 buffer）。**只有这个形态能让 Minecraft 自己的渲染保持不变、把交换链降级为可选输出。** 顺带修掉隐患 12。代价最大 |

### 5.3 一句话结论

- 想要**画面**且接受放弃 Vulkan 路径：选 **1（或 2）**，纯配置、立刻可验。
- 想要**判定 WSI 是否是唯一病灶**：选 **3**，一行环境变量，预期黑屏但不崩。
- 想要**真正绕开交换链又保留 Magma→PanVK→kbase 全栈**：只有 **4**（shim 层，不动两侧源码）或 **5**（MGL 源码层）。
- **仅靠 MGL 配置直接出画面：本代码库不存在这条路。**

---

## 摘要（≤14 行）

1. 交换链唯一创建者是 `SwapchainObject::Create`（`SwapchainObject.cpp:279/282/285`），唯一调用者是 `VulkanRenderer::CreateSwapchain()`（`VulkanRenderer.cpp:14512`），只被 `RecreateSwapchain()`（`:14883`）调用，共 6 个调用点（3134、3220、12880、12990、13032、13057），其中 **3134/3220 丢弃返回值**。
2. 前置条件：必须 `MOBILEGL_BACKEND_TYPE=DirectVulkan`（**默认是 DirectGLES**，`ConfigLoader.cpp:217`）；逻辑设备强制 `VK_KHR_swapchain`（`VulkanRenderer.h:1497`）；`currentExtent` 为 0×0 时不建链（`:14862`）。
3. 默认帧缓冲**物理上就是交换链图像**（`VkRenderPassManager.cpp:1096` 用 `swapchainViews[swapchainImageIndex]`），`:297-354` 分配的默认 FBO 纹理只是"frontend placeholder"——**不存在 默认FBO→交换链 的 blit**。
4. 离屏**FBO** 完全不依赖交换链（`VkRenderPassManager.cpp:1097-1137`，尺寸来自 attachment：`:199-205`），所以交换链 0 图像时纯离屏渲染仍成立；但**没有任何路径把离屏结果送到屏幕**。
5. MGL **没有真正的 no-WSI 模式**：连 pbuffer（`BackendObject_DirectVulkan.cpp:362`，`m_window==null`）在无 `VK_EXT_headless_surface` 时也会造 AImageReader 窗口走 `vkCreateAndroidSurfaceKHR`（`VulkanRenderer.cpp:14553-14615, 14634`）。
6. **不存在**任何 `MOBILEGL_*HEADLESS/NO_WSI/OFFSCREEN/PRESENT*` 变量（已全树枚举）；唯一影响交换链形态的是 `MOBILEGL_MAGMA_FRAMESINFLIGHT`（= minImageCountHint，设 2 使图像数最小）。
7. 已**实测** assert 被编译掉：`CMAKE_BUILD_TYPE=Release` + `MOBILEGL_LOG_ACTIVE_LEVEL=INFO`，`strings libMobileGL.so | grep -c 'Assertion failed'` = **0**。
8. 更危险的是 `VK_VERIFY`（`VkIncludes.h:89-101`）：失败只 `MGLOG_F` 一行，**不 return/不 throw**，然后继续跑。
9. 典型连锁：`vkCreateSwapchainKHR` 失败 → `m_swapchain` 仍为 NULL → 继续 `vkGetSwapchainImagesKHR(VK_NULL_HANDLE)`（`:279→282`）；`IsComplete()` 断言后 `return availableFormats[0]` 空向量越界（`:175→136/164`）。
10. 崩溃直通车：`VkRenderPassManager.cpp:1079` 断言后 `:1096` 对空 `m_imageViews` 越界；另有 `FrameContext.cpp:249/273`、`VulkanRenderer.cpp:12979` 同型隐患，以及 `VulkanRenderer.cpp:13230` 对必需 extension 只断言 → 可能把 NULL surface 交给 WSI。
11. PanVK(v45) **支持** `VK_KHR_swapchain` / `VK_KHR_android_surface` / `VK_EXT_headless_surface`，所以缺口不在 extension 缺失，而在 android WSI 的能力查询/建链内部。
12. 驱动侧 `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`（`wsi_common.c:1434`）可让任意 surface 走 headless 交换链、present 变空操作（`wsi_common_headless.c:401-413`）——**一行环境变量即可把"WSI 崩"降级为"不崩但黑屏"**，是最省的病灶判定实验。
13. 可行性排序：**①`MOBILEGL_BACKEND_TYPE=DirectGLES`（纯配置，能出画，放弃 PanVK）→ ②+`MOBILEGL_ESPRYT_USE_ANGLE=1` → ③`MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`（诊断，黑屏）→ ④shim 层换 surface/改 present（`/root/mgl_icd/vkshim_mgl.c`，目前纯转发，挂钩点 299/515/817/844；唯一能真绕开交换链且保留全栈）→ ⑤改 MGL 源码解耦默认 FBO（重编，代价最大）**。
14. **只靠 MGL 配置让画面出来：不存在这条路。**
