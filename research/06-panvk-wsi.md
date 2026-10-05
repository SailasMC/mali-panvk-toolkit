# 06 · PanVK Android WSI 深挖（MT6989 / Android 16 / kbase）

源码：`/root/mesa`（fork: `funnymdzz/mesa`，HEAD `6598829`，"pan/lib: limit tessellation kernels…"），只读。
本文所有行号均在本机源码上逐条核对过；结论与"未验证项"分开标注。

---

## 0. TL;DR

1. **`panvk_wsi.c` 只有 122 行，它自己完全不实现 `vkCreateSwapchainKHR`。** 它只做三件事：算 kbase 相关开关 → 调 `wsi_device_init()` → 把 `physical_device->vk.wsi_device` 指过去。真正的 swapchain/image/memory 代码在 **`src/vulkan/wsi/`**（通用 WSI 层）。
2. **Android 上通用 WSI 的 swapchain-image 路径被硬编码关闭**：`wsi_common_is_swapchain_image()` 在 `#ifdef VK_USE_PLATFORM_ANDROID_KHR` 下 **永远 return false**（`wsi_common.h:333-338`）。Android 的"WSI"实体是 **`VK_ANDROID_native_buffer` + AHB**，由 `panvk_android.c` 接管。
3. **panvk 不发 `VK_KHR_android_surface`**（`panvk_instance.c` 扩展表里根本没有）。Android 上屏必须走 ANB/AHB。
4. `VK_KHR_swapchain / KHR_surface / present_wait / present_timing` 全部被 `#ifdef PANVK_USE_WSI_PLATFORM` 包住；而 `PANVK_USE_WSI_PLATFORM` 只在 **wayland/xcb/xlib/display** 任一平台被编译时才有定义（`panvk_instance.h:43-48`）。**纯 `platforms=[android]` 的构建里 panvk 不暴露 swapchain。**
5. 本机 `/dev/dma_heap/system` 权限是 **0444**，而 fork 的 kbase 用 **`O_RDWR`** 打开它（`kbase_kmod.c:1269`）→ **必然失败** → `kbase_kmod_supports_dmabuf()=false` → `panvk_wsi_init` 自动落到 **`sw_device=true`（CPU/SHM 软件 WSI）**，且 `supports_modifiers=false`。**这是本机当前 WSI 状态的决定性事实。**
6. u_gralloc 的 modifier 在 Android ANB/AHB 路径上是**强制**的（无 LINEAR 回退）；未知 modifier 会让 panvk **SIGSEGV**（不是干净报错）。详见 §5。

---

## 1. 实测环境事实（本次用 Shizuku shell 在真机读到）

| 项 | 实测值 | 对 panvk 的意义 |
|---|---|---|
| `/dev/mali0` | `crw-rw-rw-` system:graphics，`O_RDWR` **成功** | kbase 可用（与任务前提一致） |
| `/dev/dri/` | **只有 `card0`**（`crw-rw----` system:system），**没有 `renderD128`** | `panvk_physical_device_try_create()` 要求 `available_nodes & (1<<DRM_NODE_RENDER)`（`panvk_instance.c:86-88`）→ card0 **被拒**；且 0660 普通 app 打不开。**比"无 /dev/dri"更精确：有 card0，但没有 render node，不可用** |
| `/dev/dma_heap/` | 19 个节点；`system` 与 `system-uncached` 均为 **`cr--r--r--`（0444）** system:system | `kbase_kmod.c:1269 open(O_RDWR)` → **EACCES**；已实测 `O_RDONLY` 成功 / `O_RDWR` 失败 |
| board / sdk | `mt6989` / `ro.build.version.sdk=36`（Android 16） | 与前提一致 |
| `ro.hardware.gralloc` | `common` | 但下面这些说明实际是 **ARM gralloc** |
| `ro.vendor.arm.gralloc.*` | `afrc_rgba/luma/chroma_usage_*`、`no_afbc_usage_*`、`force_back_buffer_usage_*` 齐备 | 设备带 **ARM 的 gralloc**（支持 AFBC/AFRC，可用 usage 位强制 no-AFBC） |
| gralloc/mapper HAL | `android.hardware.graphics.mapper@4.0-impl-mediatek.so`、`mapper.mediatek.so`、`libgralloctypes_mtk.so` | **IMapper4 存在** → u_gralloc AUTO 会选中 `U_GRALLOC_TYPE_GRALLOC4`，modifier 由 IMapper 元数据决定 |
| `/vendor/lib64/` | `libarm_gralloc_properties_sysprop.so` | 同上，ARM gralloc 血统 |

### 构建产物（服务器上）
| 构建目录 | `platforms` | WSI 头文件定义 | 编译进 `libvulkan_wsi` 的 WSI 后端 |
|---|---|---|---|
| `zenithblue/build/android-v2` | `[android,x11]` | ANDROID + **XCB + XLIB** | drm / entrypoints / headless / **x11** |
| `zenithblue/build/android-bionic` | `[android,x11]` | 同上 | 同上 |
| `zenithblue/build/android-v3` | `[android]` | 仅 ANDROID | drm / entrypoints / headless |
| `zenithblue/build/android-v4` | `[android]` | 仅 ANDROID | drm / entrypoints / headless |

* `/root/drv/libvulkan_panfrost-android-aarch64.so`（30 MB, 10-04 22:58）里 `nm` 有 **`wsi_x11_init_wsi`**、有 `PANVK_KBASE_DRI3` 字符串 → **它是 `android,x11` 那一支的产物**。所以这份 ICD **是带 `VK_KHR_swapchain` + XCB/XLIB surface + headless surface 的**。
* `x11` 分支的 X11 WSI 会在 `wsi_x11_init_wsi()`（`wsi_common_x11.c:3459`）里注册，**init 阶段不连 X server**（懒连接），所以没有 `DISPLAY` 也不会让 physical device 创建失败。
* `VK_KHR_display` 在 Android 上**不可用**：`src/vulkan/meson.build:57` 是 `if system_has_kms_drm and not with_platform_android` → Android 构建**不定义** `VK_USE_PLATFORM_DISPLAY_KHR`，`wsi_common_display.c` 不参与编译（已在 v2/v3 的 build.ninja 里核对：wsi 目标里没有它）。

---

## 2. Q1 · `vkCreateSwapchainKHR` → 图像 → 内存 → `vkGetSwapchainImagesKHR` 完整路径

### 2.1 `panvk_wsi.c` / `panvk_wsi.h` 的真实职责（全部内容）

`panvk_wsi.h:13-14` 只导出两个函数：`panvk_wsi_init()` / `panvk_wsi_finish()`。

`panvk_wsi.c`：

| 行 | 内容 | 依赖的平台能力 |
|---|---|---|
| 19-21 | `#if defined(HAVE_PAN_KMOD_KBASE) #include "lib/kmod/kbase_kmod.h"` | 编译期 kbase 后端 |
| 26-33 | `panvk_wsi_proc_addr()` → `vk_instance_get_proc_addr_unchecked()` | 通用 Vulkan runtime |
| 36-48 | `panvk_can_present_on_device()`：`drmGetDevice2(fd,0,&device)`，只认 `bustype == DRM_BUS_PLATFORM` | **libdrm + 真 DRM fd**（kbase 的 fd 不是 DRM fd） |
| 55 | `uses_kbase = physical_device->kbase_node_path[0] != '\0'` | `/dev/maliN` 存在 |
| 56-67 | `PANVK_KBASE_DRI3` / `WSI_X11_TERMUX` 环境变量 → `termux_raw_dri3` / `kbase_raw_dri3` | fork 私有：Termux:X11/Winlator 的 raw-FD DRI3 |
| 68-76 | `kbase_dmabuf = uses_kbase && (dri3 开关) && kbase_kmod_supports_dmabuf(kmod.dev)` | **`/dev/dma_heap/system` 能以 O_RDWR 打开**（本机否！） |
| 79-88 | `wsi_device_init(&wsi_device, pdev, panvk_wsi_proc_addr, &alloc, -1, &drirc.options, &(struct wsi_device_options){ .sw_device = uses_kbase && !kbase_dmabuf, .wait_present_before_queue = kbase_dmabuf, .x11_use_raw_fd_modifier = kbase_dmabuf && kbase_raw_dri3 })` | 通用 WSI 层 + 可选 x11/wayland/headless 后端 |
| 96 | `wsi_device.disable_unordered_submits = uses_kbase` | kbase sync 是用户态 seqno，不能在空提交上拷贝 |
| 104-105 | `supports_modifiers = !uses_kbase \|\| (kbase_dmabuf && !kbase_raw_dri3)` | 决定 X11 是否走 DRI3 modifier 协商 |
| 106-107 | `can_present_on_device = panvk_can_present_on_device` | 覆盖 wsi 默认的 `wsi_device_matches_drm_fd` |
| 109 | `physical_device->vk.wsi_device = &physical_device->wsi_device` | **这一行才是 swapchain 入口被路由过来的开关** |
| 114-122 | `finish` → `wsi_device_finish()` | — |

> 注：`display_fd` 传的是 **-1**。Android 上 `VK_USE_PLATFORM_DISPLAY_KHR` 未定义，`wsi_display_init_wsi()` 根本不编译，所以 -1 不会导致初始化失败。

**入口怎么路由到 wsi 的**：Mesa 从 `vk.xml` 生成 `wsi_common_entrypoints.{c,h}`（在 build 目录的 `src/vulkan/wsi/` 下），把 `wsi_CreateSwapchainKHR` / `wsi_GetSwapchainImagesKHR` 声明为 **weak** 符号（`VK_ENTRY_WEAK`）。驱动自己的 `panvk_entrypoints.c` 对这些名字不提供实现 → 由弱符号解析到通用 WSI 实现；而"能不能用"由 `vk.wsi_device != NULL` 决定。`nm` 校验：ICD 里同时有 `wsi_CreateSwapchainKHR` 和 `wsi_GetSwapchainImagesKHR`（SYMTAB）。

### 2.2 通用 WSI 的路径（**非 Android**：X11 / headless）

```
vkCreateSwapchainKHR
 └─ wsi_CreateSwapchainKHR                       wsi_common.c:1425
     ├─ iface = wsi_device->force_headless_swapchain
     │            ? wsi[VK_ICD_WSI_PLATFORM_HEADLESS]
     │            : wsi[surface->platform]                       (:1434-1436)
     │   （force_headless_swapchain ← env MESA_VK_WSI_HEADLESS_SWAPCHAIN, wsi_common.c:297-298）
     └─ iface->create_swapchain(...)                             (:1466)
         ├─ X11: x11_surface_create_swapchain                     wsi_common_x11.c:3122
         │    ├─ 平台能力探测 wsi_x11_get_connection()            (:240-366)
         │    │    DRI3 / Present / MIT-SHM / explicit-sync / Xwayland / 私有 X server
         │    ├─ 选 image 类型                                    (:3233-3251)
         │    │    wsi_device->sw      → WSI_IMAGE_TYPE_CPU  (+ alloc_shm)
         │    │    else               → WSI_IMAGE_TYPE_DRM  (+ dri3 modifiers 若 use_modifiers())
         │    │    use_modifiers() = supports_modifiers && !x11.ignore_suboptimal  (:2938-2940)
         │    └─ wsi_swapchain_init(...)                          (:3274)
         └─ HEADLESS: wsi_headless_surface_create_swapchain       wsi_common_headless.c:445
              （!sw && supports_modifiers 时才查 modifier 列表，:471）

wsi_swapchain_init → wsi_configure_*_image  (dispatcher: wsi_common.c:460-480)
 ├─ WSI_IMAGE_TYPE_CPU  → wsi_configure_cpu_image                  wsi_common.c:3611
 │    ├─ NO_BLIT: create.tiling = VK_IMAGE_TILING_LINEAR
 │    │           create_mem = wsi_create_cpu_linear_image_mem    (:3514)
 │    │           → GetImageMemoryRequirements + GetImageSubresourceLayout
 │    │             + AllocateMemory(wsi_select_host_memory_type)
 │    │             + 可选 ImportMemoryHostPointer(HOST_ALLOCATION) ← 需 VK_EXT_external_memory_host
 │    │             + MapMemory
 │    └─ BLIT:    create_mem = wsi_create_cpu_buffer_image_mem    (:3579)
 └─ WSI_IMAGE_TYPE_DRM  → wsi_configure_native_image               wsi_common_drm.c:585
      ├─ num_modifier_lists == 0 → info->wsi.scanout = true  ←── 关键分支 (:600-603)
      │     （panvk_image.c 对 legacy scanout 直接取 **DRM_FORMAT_MOD_LINEAR**）
      └─ num_modifier_lists > 0  → assert(supports_modifiers)
            GetPhysicalDeviceFormatProperties2 拿 modifier 列表
            → 逐个 GetPhysicalDeviceImageFormatProperties2(tiling=DRM_FORMAT_MODIFIER_EXT) 过滤
            → VkImageDrmFormatModifierListCreateInfoEXT 挂到 pNext
            create_mem = wsi_create_native_image_mem               (:733)

每个 image: wsi_create_image()                                     wsi_common.c:882
 ├─ wsi->CreateImage(VkImageCreateInfo(+ WSI_IMAGE_CREATE_INFO_MESA / mod list))   (:896)
 ├─ info->create_mem()   ← 内存分配（见上；DRM 路径见 wsi_common_drm.c:758）
 │    └─ RM: AllocateMemory(VkExportMemoryAllocateInfo{DMA_BUF})
 │           + GetImageMemoryRequirements/SubresourceLayout
 │           + wsi->GetMemoryFdKHR(DMA_BUF) → image->dma_buf_fd   (wsi_common_drm.c:731-747)
 ├─ wsi->BindImageMemory()                                          (:905)
 └─ info->explicit_sync → wsi_create_image_explicit_sync_drm()       (:918)

vkGetSwapchainImagesKHR → wsi_GetSwapchainImagesKHR                wsi_common.c:2171
 └─ swapchain->get_wsi_image(chain, i)->image    ← 只是把创建期已建好的 VkImage 交出去
```

**各步的平台能力依赖**：
* XCB/XLIB WSI：`VK_USE_PLATFORM_XCB_KHR`/`XLIB_KHR` + libxcb(-dri3/present/shm/sync/xfixes/xrandr) + 一个 **DISPLAY**；DRI3 modifier 还要 X server 报 v1.2+ 且 `has_present_v1_2`。
* DRI3 直通（非 sw）：需要 driver 能 **export dma-buf**（`vkGetMemoryFdKHR(DMA_BUF)`）+ `supports_modifiers`，或走 fork 的 **raw-FD 1274** 私有通道（`x11_use_raw_fd_modifier`）。
* CPU/SHM 路径：`wsi_device->sw` 为真即可；MIT-SHM 快路**额外要求 `wsi_dev->has_import_memory_host`**（`wsi_common_x11.c:247` = `supported_extensions->EXT_external_memory_host`）。**panvk 没有这个扩展** → `has_mit_shm=false` → present 落到 `x11_present_to_x11_sw()`（`wsi_common_x11.c:1848`，纯 `xcb_put_image` 上传）。
* Headless：无条件可用（非 Win32），present 是空操作。

### 2.3 Android 的真实路径（**这才是本机该走的那条**）

```
vkCreateImage
 └─ panvk_CreateImage                                    panvk_image.c:724
     ├─ panvk_android_is_gralloc_image(pCreateInfo)?  → panvk_android_create_gralloc_image   (:731-734, panvk_android.c:139)
     │    ├─ 有 VkNativeBufferANDROID → panvk_android_anb_init (panvk_android.c:113)
     │    │    └─ vk_android_get_anb_layout()  → u_gralloc 查询 → VkImageDrmFormatModifierExplicitCreateInfoEXT
     │    │       + 强制 create_info.tiling = DRM_FORMAT_MODIFIER_EXT (panvk_android.c:164-166)
     │    │       + VkExternalMemoryImageCreateInfo{DMA_BUF} (panvk_android.c:127-131)
     │    │       → panvk_image_init()
     │    │    → vk_android_import_anb()  (vk_android.c:254)
     │    │         ├─ GetImageMemoryRequirements + GetMemoryFdPropertiesKHR(DMA_BUF)
     │    │         ├─ AllocateMemory(IMPORT_MEMORY_FD{DMA_BUF} + DEDICATED)
     │    │         └─ BindImageMemory2
     │    └─ 否则(deferred，来自 ImageSwapchainCreateInfoKHR / EXTERNAL_MEMORY_IMAGE_CREATE_INFO{AHB})
     │         → vk_android_init_deferred_image() (vk_android.c:299)，绑定推迟到 vkBindImageMemory2
     └─ wsi_common_is_swapchain_image()?  ← Android 上恒为 false，永不到此
     └─ 普通 vk_image_create()

vkAllocateMemory
 └─ panvk_AllocateMemory                                 panvk_device_memory.c:63
     └─ panvk_android_is_ahb_memory()? → panvk_android_allocate_ahb_memory()  (panvk_android.c:359)
          ├─ IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID → AHardwareBuffer_acquire
          │  否则 → vk_alloc_ahardware_buffer()（导出 AHB）
          └─ panvk_android_import_ahb_memory()  (panvk_android.c:250)
               ├─ AHardwareBuffer_getNativeHandle() → dma_buf_fd = handle->data[0]
               ├─ dedicated.image  → panvk_android_ahb_image_init()（u_gralloc layout → DRM_FORMAT_MODIFIER）
               │                     → panvk_android_get_image_mem_reqs()   ◄── 75 行
               ├─ dedicated.buffer → panvk_android_get_buffer_mem_reqs()    ◄── 101 行
               └─ 无 dedicated      → memoryTypeBits = GetMemoryFdPropertiesKHR  ◄── 298 行
               → dup(fd) → AllocateMemory(IMPORT_MEMORY_FD{DMA_BUF})
               → mem->ahardware_buffer = ahb

vkBindImageMemory2(image, VK_NULL_HANDLE)   ← VK_ANDROID_native_buffer 的用法
 └─ panvk_BindImageMemory2                              panvk_image.c:1330
     └─ mem == NULL → panvk_android_get_wsi_memory(dev, bind_info, &mem_handle)   (:1337)
          ├─ 把 VkNativeBufferANDROID 注入 img->vk.android_deferred_create_info 链
          ├─ panvk_android_anb_init()  → panvk_image_init()（此时才真正定 layout）
          ├─ vk_android_import_anb_memory() → AllocateMemory(IMPORT_MEMORY_FD{DMA_BUF})
          └─ *out_mem_handle = img->vk.anb_memory，随后正常 bind

vkGetSwapchainImagesKHR / vkAcquireNextImageKHR
 └─ 在 Android 上由 **AHardwareBuffer / SurfaceFlinger 一侧**完成；
    panvk 只提供 ANB/AHB 的 import，不提供 swapchain 对象
```

> 别忘了：`vkGetSwapchainImagesKHR` 只在 `android,x11` 构建（`wsi_common.c:2171`）里存在；纯 android 构建里 panvk 根本不暴露它。

---

## 3. Q2 · `panvk_android.c` / `.h` 提供的能力 与 每个失败点的 `VkResult`

`panvk_android.h` 导出 5 个能力；`!VK_USE_PLATFORM_ANDROID_KHR` 时全部退化为 `VK_ERROR_FEATURE_NOT_PRESENT`（`.h:36-72`），`is_*` 退化为 `false`。

| # | 能力 | 触发条件 | 失败返回 |
|---|---|---|---|
| 1 | `panvk_android_is_gralloc_image` `.c:19-44` | pNext 里有 `NATIVE_BUFFER_ANDROID`，或 `IMAGE_SWAPCHAIN_CREATE_INFO_KHR` 且 swapchain≠NULL，或 `EXTERNAL_MEMORY_IMAGE_CREATE_INFO` 带 `…ANDROID_HARDWARE_BUFFER_BIT_ANDROID` | bool（不返回 VkResult） |
| 2 | `panvk_android_create_gralloc_image` `.c:138-184` | 由 1 触发 | 透传：`vk_image_create` 失败→`VK_ERROR_OUT_OF_HOST_MEMORY`；`vk_android_init_deferred_image` / `panvk_image_init` / `vk_android_import_anb` 的返回值经 `panvk_error()` 透传；`vk_image_destroy` 清理 |
| 3 | `panvk_android_get_wsi_memory` `.c:186-219` | `vkBindImageMemory2(image, VK_NULL_HANDLE)` + `VkNativeBufferANDROID` | 透传 `panvk_android_anb_init` 与 `vk_android_import_anb_memory` 的结果；成功则输出 `img->vk.anb_memory` |
| 4 | `panvk_android_is_ahb_memory` `.c:342-357` | `IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID`，或 `EXPORT_MEMORY_ALLOCATE_INFO` 的 handleTypes **恰好等于** AHB 位 | bool |
| 5 | `panvk_android_allocate_ahb_memory` `.c:359-392` | 由 4 触发 | `AHardwareBuffer` 拿不到→`VK_ERROR_OUT_OF_HOST_MEMORY`；`panvk_android_import_ahb_memory` 的结果经 `panvk_error()` 透传；失败会 `AHardwareBuffer_release` |

### ★ `VK_ERROR_INVALID_EXTERNAL_HANDLE` 的三个点（任务点名的 75/101/298）

| 行 | 函数 | 条件 | 语义 |
|---|---|---|---|
| **`panvk_android.c:75`** | `panvk_android_get_image_mem_reqs()`（60-84） | `GetImageMemoryRequirements().memoryTypeBits & panvk_android_get_fd_mem_type_bits(fd)` == 0 | 图像要求的 memory type 与 dmabuf fd 允许的 memory type **交集为空** → 无法 import |
| **`panvk_android.c:101`** | `panvk_android_get_buffer_mem_reqs()`（86-110） | 同上，换成 `GetBufferMemoryRequirements()` | buffer 版的同类失败 |
| **`panvk_android.c:298`** | `panvk_android_import_ahb_memory()`（250-340），**无 dedicated info 分支** | `panvk_android_get_fd_mem_type_bits(device, dma_buf_fd)` 返回 0（即 `GetMemoryFdPropertiesKHR(DMA_BUF)` 失败或没给出任何 type） | 裸 AHB import 时拿不到可用 memory type |

三者都经 `panvk_errorf(..., VK_ERROR_INVALID_EXTERNAL_HANDLE, ...)` 打印原因。共同前置：`panvk_android_get_fd_mem_type_bits()`（`.c:46-58`）内部调 `vkGetMemoryFdPropertiesKHR(DMA_BUF)`，**失败时静默返回 0**，于是上层看起来就是"没有兼容 memory type"。所以 75/101/298 这三个点的根因往往是同一个：**panvk 的 `vkGetMemoryFdPropertiesKHR(DMA_BUF)` 没能为这个 fd 报出 type**（或该 fd 的 type 与图像/缓冲区要求无交集）。

### 同一链路上其他 `VK_ERROR_INVALID_EXTERNAL_HANDLE`（`src/vulkan/runtime/vk_android.c`，常被忽略但同样致命）

| 行 | 位置 | 条件 |
|---|---|---|
| 149 | `vk_gralloc_to_drm_explicit_layout()` | `u_gralloc_get_buffer_basic_info()` 失败（**u_gralloc 没起来 / HAL 拒绝**） |
| 152 | 同上 | `info.num_planes > max_planes`（panvk 传 `PANVK_MAX_PLANES`） |
| 164 | 同上 | planes 非连续（`info.offsets[i]==0`，disjoint） |
| 218 | `vk_android_import_anb_memory()` | `mem_reqs.memoryTypeBits & fd_props.memoryTypeBits` == 0 |
| 888 / 895 | AHB properties 相关 | 无已知 gralloc 能按该组合分配 |
| 942 / 962 / 969 | `vk_android_get_ahb_*_properties` 系列 | `u_gralloc_get_buffer_basic_info` / `_color_info` 失败，或后续校验失败 |

### panvk_android 暴露给 app 的扩展（`panvk_vX_physical_device.c`）

```c
bool has_gralloc = vk_android_get_ugralloc() != NULL;     // :42
...
.ANDROID_external_memory_android_hardware_buffer = has_gralloc,   // :231
.ANDROID_native_buffer = has_gralloc,                             // :232
/* VK_ANDROID_native_buffer */
.sharedImage = vk_android_get_front_buffer_usage() != 0,          // :1313-1314
```
* `vk_android_get_ugralloc()` = `u_gralloc_create(U_GRALLOC_TYPE_AUTO)`（`vk_android.c:58-68`）。**u_gralloc 起不来 ⇒ 这两个扩展直接被摘掉**（不是一个"能用但会失败"的状态）。
* **panvk 完全不支持 external format**：全树 `grep ExternalFormatANDROID src/panfrost/vulkan/` **零命中**；`vk_android.c` 里 `vk_android_rp_attachment_has_external_format()`（:1204-1211）是给别的驱动用的。也就是说 `VK_FORMAT_UNDEFINED` + `VkExternalFormatANDROID`（不透明/YCbCr AHB）这条 spec 必需路径 panvk 没实现 → 这类 AHB 会以 format 校验失败告终。
* 也**没有** `VK_EXT_external_memory_host`（panvk 源里零命中；.so 里的那个字符串来自通用扩展名表）→ 直接影响 X11 MIT-SHM 快路（见 §2.2）。

---

## 4. Q3 · 是否存在"不经过 WSI"的替代路径？

| 路径 | panvk/Mesa 是否支持 | 结论 |
|---|---|---|
| **`VK_EXT_headless_surface`** | **支持**：`panvk_instance.c:133`（`#ifndef VK_USE_PLATFORM_WIN32_KHR`，Android 上成立）；后端 `wsi_common_headless.c:553` 在非 Win32 构建里总会被 `wsi_device_init` 注册 | 可用，但**只能配 `VK_KHR_swapchain`**；纯 android 构建里 swapchain 被 `PANVK_USE_WSI_PLATFORM` 关掉 → **headless surface 是"暴露但没法用"**。`android,x11` 构建里两者都有 → 可当离屏/诊断路径（present 是空操作） |
| **`VK_KHR_display`** | **不支持（Android）**：`src/vulkan/meson.build:57` 明确 `not with_platform_android` | 且本机没有 render node、card0 是 0660；即便编译进去也无法工作 |
| `VK_EXT_acquire_drm_display` / `direct_mode_display` / `display_surface_counter` | 同 display 门 | 同上，不支持 |
| `VK_KHR_android_surface` | **panvk 完全没有实现**（`panvk_instance.c` 扩展表无此项；ICD 里连名字都没有） | Android 上屏的正统路径被 panvk 换成了 **ANB/AHB** |
| **`VkImageSwapchainCreateInfoKHR` + `wsi_common_create_swapchain_image()`**（"驱动给 swapchain 建 image"的通用机制，`wsi_common.c:2959`） | 代码在，但 **Android 上被 `wsi_common_is_swapchain_image()` 硬编码 return false 关掉**（`wsi_common.h:333-338`） | Android 上不生效；非 Android（X11/headless）上生效 |
| **自建 image + 上层拷贝**（完全不碰 WSI） | 纯 core Vulkan：panvk 能建 LINEAR/OPTIMAL image、能 copy（`panvk_vX_cmd_meta.c` 有 blit/copy 路径），**不依赖任何 WSI 扩展** | **最稳**。上屏需要上层自己解决（ANativeWindow+ANB，或 X11 窗口+SHM 上传）。代价：一次拷贝 |

**小结**：Android 上"不经过 WSI"不是备选，而是**唯一架构**——panvk 把 WSI 责任交给了 gralloc/SurfaceFlinger。真正可选的"绕过"只有三种：(a) headless（离屏，不上屏）；(b) 上层自建 image + 拷贝；(c) 走 X11 WSI（需要 X server）。

---

## 5. Q4 · `VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT` 与 u_gralloc modifier 的强制性

### 5.1 两条路径对 modifier 的态度完全不同

**(A) 通用 WSI（X11/headless）—— modifier 是"协商 + 过滤"，不碰 u_gralloc**

* `wsi_configure_native_image()`（`wsi_common_drm.c:585`）：`num_modifier_lists==0` → 走 legacy scanout（`info->wsi.scanout = true`，`:600-603`），panvk 侧 `panvk_image_get_mod()` 对 legacy scanout **直接取 `DRM_FORMAT_MOD_LINEAR`**（`panvk_image.c:373-376`）。有 modifier 列表时逐个过 `vkGetPhysicalDeviceImageFormatProperties2(tiling=DRM_FORMAT_MODIFIER_EXT)` 过滤。
* panvk 的过滤规则在 `panvk_physical_device.c:1910-1933`：
  ```c
  const bool can_use_afbc = PANVK_DEBUG(WSI_AFBC) && panvk_image_can_use_afbc(...);
  const bool supported = (drm_is_afbc(mod_info->drmFormatModifier) && can_use_afbc)
                        || mod_info->drmFormatModifier == DRM_FORMAT_MOD_LINEAR;   // :1921-1922
  ```
  而 `PANVK_DEBUG_WSI_AFBC = 1<<13`（`panvk_instance.h:31`）是**默认关闭的调试位**（要靠 `PANVK_DEBUG=wsi_afbc`）。同理 `vkGetPhysicalDeviceFormatProperties2` 里 AFBC modifier 只有在这个调试位打开时才被列出（`panvk_physical_device.c:1741-1748`）。
  → **默认情况下，panvk 对外只承认 `DRM_FORMAT_MOD_LINEAR` 这一个 DRM modifier。**
  → 于是 X11 DRI3 只用 LINEAR；X server 若连 LINEAR 都不给，`wsi_configure_native_image` 会走到 `assert(!"Failed to find a supported modifier!")` 后 `goto fail_oom`（release 下 assert 为空 → 返回 `VK_ERROR_OUT_OF_HOST_MEMORY`，`wsi_common_drm.c:735-740`）。
* 结论：**WSI 侧不强制 u_gralloc**，但用 modifier 时必须让 panvk 认（默认只有 LINEAR）。

**(B) Android ANB/AHB —— modifier 是强制的，且没有 LINEAR 回退**

`vk_gralloc_to_drm_explicit_layout()`（`vk_android.c:138-190`）核心只有 6 行：

```c
if (u_gralloc_get_buffer_basic_info(u_gralloc, in_hnd, &info) != 0)
   return VK_ERROR_INVALID_EXTERNAL_HANDLE;          // :148-149
...
out->drmFormatModifier = info.modifier;              // :174  ← 直接照抄，无任何判断
out->drmFormatModifierPlaneCount = info.num_planes;
for (i...) { out_layouts[i].offset = info.offsets[i]; out_layouts[i].rowPitch = info.strides[i]; }
```

* **没有任何 `DRM_FORMAT_MOD_INVALID` / 未知 modifier 的处理**，也不会在拿不到 modifier 时退化成 LINEAR。
* 调用者：`vk_android_get_anb_layout()`（`:280-297`，来自 `VkNativeBufferANDROID`）与 `vk_android_get_ahb_layout()`（`:619-638`，来自 `AHardwareBuffer`）。**两条都走同一函数**，即 ANB 与 AHB 一样强制。
* 消费端 `panvk_image_get_explicit_mod()`（`panvk_image.c:319-330`）只断言 `panvk_image_can_use_mod(image, iusage, mod, false)`；而 `panvk_image_can_use_mod()`（`panvk_image.c:197-...`）对**非 AFBC、非 LINEAR 的任意 modifier 直接 `return true`**（它只在 `drm_is_afbc(mod)` 为真时做那些限制检查，末尾无条件 `return true`）。
* 然后 `panvk_image_init()` 里：
  ```c
  const struct pan_mod_handler *mod_handler = pan_mod_get_handler(arch, image->vk.drm_format_mod);  // panvk_image.c:454
  ...
  image->planes[p].image.mod_handler = mod_handler;
  ...
  if (!pan_image_layout_init(arch, pan_img, pan_plane, &plane_layout)) → VK_ERROR_INITIALIZATION_FAILED
  ```
  `pan_mod_get_handler()`（`panfrost/lib/pan_mod.c:817-826`）**匹配不到就 `return NULL`**；而 `pan_image_layout_init()` 第一行就是 `assert(image->mod_handler);`（`panfrost/lib/pan_layout.c:69`），紧接着 `:98-99` 解引用 `mod_handler->init_plane_layout`。
  → **未知 modifier = 断言失败 / release 下 NULL 解引用 SIGSEGV，而不是一个 VkResult。**

panvk 认识的 modifier 全集 = `pan_mod_handlers[]`（`pan_mod.c:807-815`）：`afbc`、`u_tiled`(16x16 U-interleaved)、`linear`，以及 `PAN_ARCH>=10` 的 `interleaved_64k`、`afrc`。

> ⚠️ 特别提醒：`pan_format.h:24` 的注释写着 "Similarly **MTK 16L32** is only used if explicitly asked for"，但 `PAN_SUPPORTED_MODIFIERS`（`pan_format.h:26-74`）里**并没有** MTK 条目，`pan_mod_handlers[]` 里也**没有** MTK handler。`DRM_FORMAT_MOD_MTK_16L_32S_TILE` 的支持只存在于 **Gallium panfrost**（`gallium/drivers/panfrost/pan_mod_conv_cso.h:106` 的 `PANFROST_EMULATED_MODIFIERS`、`pan_resource.c:1713`、用 compute shader detile），**panvk 侧完全没有**。`drm_is_mtk_tiled(mod)` 在 panvk 里无人使用。
> 也就是说：**如果本机 gralloc 报 MTK 16L32，panvk 会崩，不会干净报错。**（本机实测 `ro.vendor.arm.gralloc.*` 齐全 + `libarm_gralloc_properties_sysprop.so`，指向 **ARM gralloc**，报的很可能是 ARM AFBC/AFRC/U-interleaved/LINEAR —— 这些 panvk **都认**。但"ARM gralloc 是否也会报 MTK 16L32"尚未实测确认，见 §8。）

### 5.2 "能不能用 LINEAR / 未知 modifier 绕过"

* **不能从 app 侧绕过**：`panvk_android_anb_init()` / `panvk_android_ahb_image_init()` 都是自己**构造** `VkImageDrmFormatModifierExplicitCreateInfoEXT` 并覆盖 `create_info->pNext`（`panvk_android.c:126-135`、`:229-241`）。上层塞进去的 `VkImageDrmFormatModifierExplicitCreateInfoEXT` 会被丢弃。
* **必须从 u_gralloc 侧或源码侧改**。三种改法：

**(改法 1，推荐，安全）在 `vk_gralloc_to_drm_explicit_layout()` 里对未知/INVALID modifier 显式拒绝**
```c
   out->drmFormatModifier = info.modifier;
+  /* panvk 只认 AFBC / U-interleaved / INTERLEAVED_64K / AFRC / LINEAR。
+   * 交给 pan_mod_get_handler() 判定：NULL 表示没有 handler，会 NULL 解引用。 */
+  if (info.modifier != DRM_FORMAT_MOD_LINEAR &&
+      GENX(pan_mod_get_handler)(PAN_ARCH, info.modifier) == NULL)
+     return VK_ERROR_INVALID_EXTERNAL_HANDLE;
```
效果：把 SIGSEGV 变成可诊断的 `VK_ERROR_INVALID_EXTERNAL_HANDLE`，上层可以退到别的方案。**不改变正确性。**

**(改法 2，仅在确认 buffer 真是线性时）把 INVALID/未知退化为 LINEAR**
```c
-   out->drmFormatModifier = info.modifier;
+   uint64_t mod = info.modifier;
+   if (mod == DRM_FORMAT_MOD_INVALID || GENX(pan_mod_get_handler)(PAN_ARCH, mod) == NULL) {
+      /* 只有当 gralloc 真的给的是线性/无压缩布局时才安全 */
+      mod = DRM_FORMAT_MOD_LINEAR;
+   }
+   out->drmFormatModifier = mod;
```
风险：如果 buffer 实际是 tiled 的，你会**静默渲染出错误的画面**（花屏/错位），而不是失败。**只应在能确认线性时用。**
（`u_gralloc_fallback.c:145` 确实会给出 `DRM_FORMAT_MOD_INVALID`——当 CROS/IMapper4/libdrm/QCOM 全都没起来时——所以这条退化对"gralloc 不可用"的设备有意义。）

**(改法 3，把 MTK 16L32 搬进 panvk）** 照 Gallium 的做法加 `pan_mod` handler + detile compute shader。工作量大（要写 shader 并接进 panvk 的 meta 路径），**不推荐**作为第一步。

**顺带一条更省事的杠杆**：ARM gralloc 尊重 usage 位 `ro.vendor.arm.gralloc.no_afbc_usage_flags/mask`（本机 = `0x200000000000000` / `0x1200000000000000`）。如果让上层在申请 buffer 时带上 "no AFBC" usage，gralloc 很可能直接给 **LINEAR**，那 panvk 就完全不用改。这条要在上层/分配侧做。

---

## 6. Q5 · 本机（MT6989 / Android 16 / kbase 可用 / 无可用 /dev/dri）让 panvk WSI 工作的最可行改法（排名）

### 先明确本机的真实约束（实测，非推断）
1. `/dev/mali0` **O_RDWR 可用** → kbase 后端能起来（`panvk_instance.c` 的 kbase 枚举分支）。
2. `/dev/dri` 只有 **card0（0660 system:system，非 render node）** → panvk 的 DRM 路径**不可用**，kbase 是唯一后端。
3. **`/dev/dma_heap/system` 是 0444**，`O_RDWR` 实测**失败**、`O_RDONLY` 成功。
   → `kbase_kmod.c:1265-1271` 的 `open(dev, O_RDWR|O_CLOEXEC)` 失败 → `dma_heap_fd = -1` → `kbase_kmod_supports_dmabuf()`（`kbase_kmod.c:152-158`）**返回 false**。
   → 代入 `panvk_wsi.c:68-88`：`kbase_dmabuf=false` ⟹ **`sw_device=true`**、`x11_use_raw_fd_modifier=false`、`supports_modifiers=false`。
   → **当前这份 ICD 上，panvk WSI 一定是 CPU/软件路径**，DRI3 直通（含 `PANVK_KBASE_DRI3=raw`）**一行代码都没走到**。这一条足以解释"WSI 起不来"的多数现象。

### 排名

**R1（最可行，零源码改动）· X11 + 当前已编译的 CPU/SW 路径**
* 用什么：`android,x11` 那一支的 ICD（`/root/drv/libvulkan_panfrost-android-aarch64.so`，已确认含 `wsi_x11_init_wsi`），配一个 X server（Termux:X11 / Winlator X server），`DISPLAY` 指过去。
* 为什么可行：`sw_device=true` 已经由 `panvk_wsi_init` 自动选中；X11 swapchain 走 `WSI_IMAGE_TYPE_CPU`→`wsi_create_cpu_linear_image_mem`（panvk 建 LINEAR、CPU 可映射的 image），present 走 `x11_present_to_x11_sw()`（`wsi_common_x11.c:1848`，`xcb_put_image`）。**不需要 dma-buf、不需要 modifier、不需要 u_gralloc、不需要 render node。**
* 注意：MIT-SHM 快路**用不了**——`wants_shm` 要求 `has_import_memory_host`（`wsi_common_x11.c:247`），而 panvk 没有 `VK_EXT_external_memory_host` → 只能走 `xcb_put_image`（每帧一次 CPU 上传，慢但正确）。
* 唯一前提：X server + `libvulkan` 能找到这份 ICD。**建议第一步就跑这个，它把"panvk 能不能渲染"和"WSI/上屏"彻底解耦。**

**R2（零改动，纯诊断/离屏）· headless surface + `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`**
* `panvk_instance.c:133` 在 Android 上暴露 `VK_EXT_headless_surface`；该环境变量让 `wsi_CreateSwapchainKHR` 把任何 surface 强制换成 headless 后端（`wsi_common.c:297-298, 1434-1436`）。
* 用途：验证 `vkCreateSwapchainKHR → vkGetSwapchainImagesKHR → 渲染 → submit` 这整条链路本身没问题，**把 X11/kbase/dma-heap 全部排除在外**。present 是空操作，拿不到画面，所以只是诊断。
* 注意：**只有 `android,x11` 构建**才有 `VK_KHR_swapchain`；纯 `android` 构建（v3/v4）里 headless surface 暴露但 swapchain 不存在，这条走不通。

**R3（最稳的上屏路径，零 WSI 依赖）· panvk 渲染到自建 LINEAR image，上层拷贝上屏**
* panvk 侧只需 core Vulkan（建 `VK_IMAGE_TILING_LINEAR` image + blit/copy），完全绕开 modifier 与 u_gralloc；上屏由上层用 ANativeWindow+ANB 或 X11+SHM 完成。
* MobileGL 这条链（`/root/HANDOFF.md`、`/root/doc09.md`）本来就在做"上层搬运"，所以这是**与现有工作最契合**的落点。
* 代价：一次额外拷贝；但**失败模式全是干净的 VkResult**，不会崩。

**R4（性能最好，但要先打补丁）· Android 原生 ANB/AHB 上屏（SurfaceFlinger）**
* 这是 Android 的"正统 WSI"，也是唯一能直接上屏（不经 X）的路。前提两个：
  (a) **必须**先打 §5.2 改法 1 的硬化补丁，否则 IMapper4 一旦返回 panvk 不认识的 modifier（如 MTK 16L32）就是 **SIGSEGV**；
  (b) 需要确认本机 IMapper4 实际返回的 modifier 落在 panvk 的 handler 集合内——本机是 **ARM gralloc**（`ro.vendor.arm.gralloc.*` + `libarm_gralloc_properties_sysprop.so`），报 AFBC/AFRC/U-interleaved/LINEAR 的概率高，**但这些 panvk 都认**；若报 MTK 16L32 则只能退到改法 3。
* 另外 `panvk_android.c:75/101/298` 那三个 `VK_ERROR_INVALID_EXTERNAL_HANDLE` 与 `vk_android.c:149`（u_gralloc 起不来）是这条路上的主要失败面，调试时优先看它们。

**R5（潜在性能最优，但依赖两个未验证假设）· 修 kbase dma_heap 打开方式，解锁 DRI3 raw-FD 直通**
* 补丁（`panfrost/lib/kmod/kbase_kmod.c:1265-1271`）：
  ```c
  -   kbase_dev->dma_heap_fd = open(dma_heap, O_RDWR | O_CLOEXEC);
  +   /* 本机 /dev/dma_heap/system 是 0444；dma-heap 的 ALLOC ioctl 不要求写权限 */
  +   kbase_dev->dma_heap_fd = open(dma_heap, O_RDONLY | O_CLOEXEC);
  +   if (kbase_dev->dma_heap_fd < 0)
  +      kbase_dev->dma_heap_fd = open(dma_heap, O_RDWR | O_CLOEXEC);
  ```
  （也可先用 `PANVK_KBASE_DMA_HEAP=<可写 heap>` 试；但本机 19 个节点里非 `system*` 的 `mtk_*` 也都是 0444，所以大概率只能靠 O_RDONLY。）
  成功后果：`kbase_kmod_supports_dmabuf()=true` → `kbase_dmabuf=true` → `sw_device=false`、`supports_modifiers = !kbase_raw_dri3`。
  再设 `PANVK_KBASE_DRI3=raw`（或 `WSI_X11_TERMUX=1`）→ `x11_use_raw_fd_modifier=true`、`supports_modifiers=false` → X11 走 `x11_image_init()` 的 **1274 raw-FD** 分支（`wsi_common_x11.c:2662-2689`），**完全跳过 DRM modifier 协商**；而 legacy-scanout 分支（`wsi_common_drm.c:600-603`）让 panvk 侧的 WSI image 默认就是 LINEAR——设计与 fork 的注释（`panvk_wsi.c:98-105`）完全自洽。
* **为什么排最后**：它依赖两个**尚未验证**的假设——(i) dma-heap 的 `DMA_HEAP_IOCTL_ALLOC` 在 `O_RDONLY` 下能成功；(ii) 本机 Mali kbase 内核允许 `KBASE_IOCTL_MEM_IMPORT` 导入 dma-buf（fork 用 `kbase_kmod_import_dmabuf`）。而且 0444 可能是 MTK/ROM 有意限制（连 `system` uid 都没有写位），存在硬性内核门禁的可能。**建议先写一个 ~30 行 C 探针在真机上验证 (i)(ii)，再投入。**

### 建议执行顺序
1. 先跑 **R2**（`MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`）→ 确认 panvk 渲染管线本身通。
2. 再跑 **R1**（Termux:X11，CPU/SW present）→ 拿到第一帧上屏。**这两步都不改一行代码。**
3. 并行做 **R4 的前置**：给 `vk_gralloc_to_drm_explicit_layout()` 打硬化补丁（防 SIGSEGV），并实测 IMapper4 返回的 modifier。
4. 写探针验证 **R5** 的两个假设；成立才动 dma_heap 那行。
5. 若 3/4 都不顺，落到 **R3**（上层拷贝）作为可交付方案。

---

## 7. 关键行号索引（便于复查）

```
src/panfrost/vulkan/panvk_wsi.c        :19-21, 36-48, 50-112(55,68-76,79-88,96,104-107,109), 114-122
src/panfrost/vulkan/panvk_wsi.h        :13-14
src/panfrost/vulkan/panvk_android.c    :19-44, 46-58, 60-84(75), 86-110(101), 112-136, 138-184,
                                       186-219, 221-248, 250-340(298), 342-357, 359-392
src/panfrost/vulkan/panvk_android.h    :16-32, 34-72
src/panfrost/vulkan/panvk_image.c      :197-..., 215-217, 319-330, 363-380, 373-376, 454-..., 724-735, 1330-1345(1337)
src/panfrost/vulkan/panvk_device_memory.c :63-71
src/panfrost/vulkan/panvk_instance.c   :86-88, 97-136(113,133), 160-(kbase 枚举)
src/panfrost/vulkan/panvk_instance.h   :31(WSI_AFBC), 43-48(PANVK_USE_WSI_PLATFORM)
src/panfrost/vulkan/panvk_vX_physical_device.c :42, 119-127, 231-232, 1313-1314,
                                       1741-1748, 1910-1933(1921-1922), 2108
src/panfrost/vulkan/meson.build        :66, 203-204
src/vulkan/meson.build                 :34-59(57 display 门), 65-66
src/vulkan/wsi/wsi_common.c            :69-(wsi_device_init), 247/253/265/277(后端注册), 297-298,
                                       460-480, 882-925(896,905,918), 1425-1436, 2171-2185,
                                       2959-2990, 3514-3560, 3611-3645
src/vulkan/wsi/wsi_common.h            :333-338(Android→false), 345
src/vulkan/wsi/wsi_common_x11.c        :240-255(247 wants_shm), 355-366, 1848(present sw),
                                       2068-2083, 2595, 2625-2689(2662-2689 raw fd 1274),
                                       2938-2949(use_modifiers), 3122, 3195-3308(3233-3251,3258),
                                       3459-3530(wsi_x11_init_wsi)
src/vulkan/wsi/wsi_common_drm.c        :585-745(600-603 scanout, 733 native mem), 758-830, 731-747(GetMemoryFdKHR)
src/vulkan/wsi/wsi_common_headless.c   :445, 471, 529, 553-580
src/vulkan/runtime/vk_android.c        :57-68(ugralloc), 138-190(149,152,164,174), 192-252(218),
                                       254-278, 280-297, 299-383, 619-638, 888/895, 942/962/969, 1204-1211
src/panfrost/lib/pan_mod.c             :660-664(linear match), 807-815(handlers), 817-826(返回 NULL)
src/panfrost/lib/pan_format.h          :24(MTK 注释), 26-74(PAN_SUPPORTED_MODIFIERS), 76-78(drm_is_afbc)
src/panfrost/lib/pan_layout.c          :69(assert(image->mod_handler)), 74, 98-99, 122
src/panfrost/lib/kmod/kbase_kmod.c     :152-158(supports_dmabuf), 1265-1271(dma_heap open),
                                       1510-1530(alloc_dmabuf), 1533-1546(bo_alloc)
src/util/u_gralloc/u_gralloc.c         :25-34(AUTO 顺序 CROS→GRALLOC4→LIBDRM→QCOM→FALLBACK)
src/util/u_gralloc/u_gralloc_fallback.c:145(modifier=INVALID), 155(LINEAR/QCOM)
src/util/u_gralloc/u_gralloc_imapper4_api.cpp :88-156(114-129 modifier 来源), meson.build:20-30
```

## 8. 未验证 / 待测（诚实标注）

1. **IMapper4 在本机实测返回的 modifier 具体值** —— 决定 R4 是"能直接用"还是"必须先加 MTK handler"。`/root/research/test-imapper4.sh` 目前只做了编译/链接验证，**没有在真机上跑过**。（本机是 ARM gralloc，倾向 ARM AFBC/AFRC/LINEAR，但未证实。）
2. **`DMA_HEAP_IOCTL_ALLOC` 在 `O_RDONLY` 下是否成功** —— R5 的假设 (i)。0444 的成因（ROM 有意限制 vs 内核默认）也未查证。
3. **本机 Mali kbase 内核是否允许 `KBASE_IOCTL_MEM_IMPORT`（导入 dma-buf）** —— R5 的假设 (ii)。
4. **`x11_present_to_x11_sw()` 在 Termux:X11/Winlator 上是否真能出画** —— R1 的落地验证。
5. 本报告未在本机实际构建/运行 panvk（源码服务器只读），行号基于 `/root/mesa` 当前 HEAD。

*（生成于任务 06，源码服务器 /root/mesa 只读，真机事实经 Shizuku shell 实测）*
