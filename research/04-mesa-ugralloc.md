# 任务 04：Mesa `u_gralloc` 精读（ops 契约 / 6 个后端 / vk_android.c 调用点 / MTK 可行性）

- 源码基准：`/root/mesa`（**只读，本次未修改任何文件**）
- 版本：`26.3.0-devel`，HEAD = `6598829`（= `funnymdzz/mesa@6598829019c`，与 `/root/panvk-mtk/build.sh` 里 `MESA_COMMIT` 一致）
- 涉及的构建事实（证据）：`/root/zenithblue/build/android-{v2,v3,v4,bionic}/meson-logs/meson-log.txt` 与 `meson-private/cmd_line.txt`
  ```
  -Dbuildtype=release -Dplatforms=android -Dandroid-stub=true -Dandroid-strict=false \
  -Dvulkan-drivers=panfrost -Dpanfrost-kmds=kbase -Dplatform-sdk-version=35
  ```
  NDK：`aarch64-linux-android35-clang`（r27c）。`-Dbuildtype=release` ⇒ `meson.build:12/62/83` 打开 `-DNDEBUG` ⇒ **所有 assert 被编译掉**（后文多处依赖这一点）。

---

## 0. 结论速览

1. `u_gralloc` 是一层"native_handle + hal_format + stride → DRM fourcc/modifier/planes(fd,offset,stride)"的**只读查询**抽象；`get_buffer_basic_info` 是唯一必需 op。
2. AUTO 顺序：`CROS → (imapper4 或 imapper5，二选一编入) → LIBDRM → QCOM → FALLBACK`（`u_gralloc.c:25-34`）。
3. 本机（Android 16/MTK、无 `/dev/dri`、gralloc 为 binderized AIDL HAL）**5 个既有后端没有一个可用**：CROS/QCOM/LIBDRM 都靠模块名精确匹配旧 gralloc 实现，imapper4/5 在当前 meson 配置下**根本没参与编译**，FALLBACK 虽能 create 成功，但对 YUV/`IMPLEMENTATION_DEFINED`/MTK 私有格式直接失败，且成功时只给 `modifier = DRM_FORMAT_MOD_INVALID`——这个值对 panvk 是**致命**的（`pan_mod_get_handler(INVALID) == NULL` ⇒ NULL 解引用，NDEBUG 下 assert 不救）。
4. **必须新增一个后端**。上游正解是 imapper5（libui `GraphicBufferMapper`），但需要 libui + 平台私有头，NDK-only 构建代价大；**最小解是运行期 `dlopen` + `dlsym("AIMapper_loadIMapper")` 直连 mapper AIDL 服务，用 `getStandardMetadata` 读 FOURCC/MODIFIER/PLANE_LAYOUTS**。
5. 这条路在本仓库里**已经做了一半**：`/root/zenithblue/work/mesa` 的 `u_gralloc_fallback.c` 里有 +275 行 `panvk_v19_*` 代码正是这个形态（寄居在 fallback 内）。任务 04 的落点建议因此是"提升为独立后端 + 修正插入顺序 + 对拍 parcel 格式"，而不是从零写。
6. 先决条件（地雷）：`/root/mesa/src/android_stub/hardware_stub.cpp` 的 `hw_get_module` 是 `return 0;` 且**不写 `*module`**，会让 CROS/LIBDRM/QCOM/FALLBACK 的 `create()` 在 `strcmp(module->name, …)` 处 NULL 解引用 → SIGSEGV。工作树已修成 `*module=nullptr; return -ENOENT;`。

---

## 1. `struct u_gralloc` 的 ops 契约

### 1.1 ops 表（`u_gralloc_internal.h:17-32`）

```c
struct u_gralloc_ops {
   int (*get_buffer_basic_info)(struct u_gralloc *g, struct u_gralloc_buffer_handle *hnd,
                                struct u_gralloc_buffer_basic_info *out);   // 必需
   int (*get_buffer_color_info)(struct u_gralloc *g, struct u_gralloc_buffer_handle *hnd,
                                struct u_gralloc_buffer_color_info *out);   // 可选
   int (*get_front_rendering_usage)(struct u_gralloc *g, uint64_t *out_usage);// 可选
   int (*destroy)(struct u_gralloc *g);                                     // 必需
};
struct u_gralloc { struct u_gralloc_ops ops; int type; };
```

| op | 语义 | 返回值约定 |
|---|---|---|
| `get_buffer_basic_info` | 由 native_handle 查询 DRM 布局；**唯一强制实现**（`u_gralloc.c:60` assert） | `0` 成功；**负 errno** 失败（各后端用 `-EINVAL/-ENOTSUP/-ENOMEM/-EAGAIN`）。`-EAGAIN` 在 legacy 路径里是"IMPLEMENTATION_DEFINED 的 RGBX hack"信号，会被调用者吞掉继续走 RGBX 分支 |
| `get_buffer_color_info` | 查 YUV 色彩空间/量化范围/色度位置 | `0` 成功。**若 ops 为空，包装函数自己填默认值**（REC601 / NARROW / 0.5 / 0.5，`u_gralloc.c:129-139`），所以它实际"永不失败"（除非后端实现了却失败） |
| `get_front_rendering_usage` | 查"前缓冲渲染"对应的 gralloc usage 位 | `0` 成功；ops 为空 → `-ENOTSUP`（`u_gralloc.c:146-147`）。调用者用它判断 `sharedImage` 是否支持 |
| `destroy` | 释放后端私有数据（并 dlclose 掉自己 dlopen 的 .so） | 忽略返回值 |

**公共包装函数的两个关键语义（`u_gralloc.c:106-122`）：**
- `u_gralloc_get_buffer_basic_info` 先建一个**零初始化**的本地 `info`，交给后端填，只有后端返回 0 时才 `*out = info` 整体拷贝 ⇒ ① 后端**不必自己清零**，没写的字段就是 0；② 后端失败时调用者的 `out` **不被触碰**。
- `u_gralloc_create(type)`（`u_gralloc.c:41-75`）：按 `type` 缓存 + 引用计数；`U_GRALLOC_TYPE_AUTO` 会按表顺序试用，第一个 `create()` 返回非 NULL 的后端被缓存，并把**实际类型回填**到 `gralloc->type`（`u_gralloc.c:63`），后续 `u_gralloc_create(AUTO)` 直接命中同一对象。全失败 → 返回 NULL。
- `u_gralloc_get_type()` 返回实际生效的后端类型。

### 1.2 输入：`struct u_gralloc_buffer_handle`（`u_gralloc.h:27-31`）

| 字段 | 含义 |
|---|---|
| `const native_handle_t *handle` | gralloc native handle。约定 `data[0..numFds-1]` 是 dma-buf fd，其后是各实现私有的 int 元数据。**多 plane 可能共用一个 fd，也可能一 plane 一 fd** |
| `int hal_format` | HAL 像素格式。`HAL_PIXEL_FORMAT_*` 与 `AHARDWAREBUFFER_FORMAT_*` 数值等价（如 `RGBA_8888 == R8G8B8A8_UNORM == 1`）。YUV 判断与 fourcc 映射全靠它 |
| `int pixel_stride` | 行跨距，**单位 = 像素**（`VkNativeBufferANDROID::stride` / `AHardwareBuffer_Desc::stride`）。legacy 后端要自己乘 bpp 得字节 stride |

### 1.3 输出：`struct u_gralloc_buffer_basic_info` 逐字段（`u_gralloc.h:33-41`）

| 字段 | 类型 | 语义 | 失败/陷阱 |
|---|---|---|---|
| `drm_fourcc` | `uint32_t` | `DRM_FORMAT_*` 平面格式，**必须非 0** | 0 表示后端没填；`vk_android.c` 的 `switch` 会走 default 返回错误 |
| `modifier` | `uint64_t` | `DRM_FORMAT_MOD_*`，描述平铺/压缩布局 | **`DRM_FORMAT_MOD_INVALID` 表示"未知"，不是合法 modifier**；Vulkan 的 `VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT` 路径会把它原样传给驱动（见 §3.1），panvk 会崩。`DRM_FORMAT_MOD_LINEAR == 0` |
| `num_planes` | `int` | 平面数，1..4 | 超过调用者 `max_planes`（本次调用者都给 4 / `PANVK_MAX_PLANES`）→ 上层的 `VK_ERROR_INVALID_EXTERNAL_HANDLE` |
| `fds[4]` | `int` | 每 plane 的 dma-buf fd，**可重复** | 单 fd 多 plane 时 `fds[i]` 全部相同 |
| `offsets[4]` | `int` | 每 plane 相对**其 fd 起点**的字节偏移 | **约定 `offsets[0] == 0`**；且 **`i>0 && offsets[i]==0` 表示该 plane 在"另一个 dma-buf"里**（disjoint）——`vk_android.c:154-165` 与 imapper4/5 的 fd 推断都用这条约定，所以后端绝不能把"合法的 0 偏移"留在 i>0 上 |
| `strides[4]` | `int` | 每 plane 行跨距，**字节** | 非 YUV 时 legacy 后端用 `pixel_stride * bpp` 算（`u_gralloc_internal.c:71-101` 的 bpp 表：RGBA_FP16=8，RGBA/RGBX/BGRA/1010102/IMPLEMENTATION_DEFINED=4，RGB_565=2，其余 0 ⇒ 失败） |

### 1.4 输入/输出全部由 `u_gralloc.h` 暴露的 4 个函数构成

`u_gralloc_create / _destroy / _get_buffer_basic_info / _get_buffer_color_info / _get_front_rendering_usage / _get_type`（`u_gralloc.h:60-77`）。`u_gralloc.h` 本身只 include `<cutils/native_handle.h>` + `gallium/include/mesa_interface.h`（为了 `__DRIYUVColorSpace` 等枚举）——**这就是它在纯 NDK 环境下也需要 android_stub 的原因**。

---

## 2. 六个后端：前提条件与失败点

### 2.1 后端选择表（`u_gralloc.c:22-34`）

| 槽位 | 类型枚举 | create 符号 | 编译条件 |
|---|---|---|---|
| 1 | `CROS` | `u_gralloc_cros_api_create` | 总是编译 |
| 2 | `GRALLOC4` | `u_gralloc_imapper_api_create` | 仅当定义 `USE_IMAPPER4_METADATA_API` |
| 3 | `LIBDRM` | `u_gralloc_libdrm_create` | 总是编译 |
| 4 | `QCOM` | `u_gralloc_qcom_create` | 总是编译 |
| 5 | `FALLBACK` | `u_gralloc_fallback_create` | 总是编译 |

> **易误判点**：`u_gralloc_imapper5_api.cpp` 在整个 `u_gralloc.c` 里"看不到引用"，但它和 imapper4 导出**同名符号** `u_gralloc_imapper_api_create`，由 meson 二选一（`u_gralloc/meson.build:19-29`）：
> - `dep_android_ui.found()`（libui，`platform-sdk-version>=35`）→ 编 **imapper5** + 定义 `USE_IMAPPER4_METADATA_API`
> - 否则 `dep_android_mapper4.found()`（`android.hardware.graphics.mapper>=4.0`，sdk>=30）→ 编 **imapper4** + 同名宏
> - 两者都没有 → 该槽位**整条消失**（`u_gralloc.c:28-30` 的 `#ifdef` 不成立）。

---

### 2.2 `u_gralloc_fallback.c`（legacy `gralloc_module_t`）

- **create 前提**：无。`hw_get_module("gralloc", …)` 失败只 `mesa_logw`，**永远返回非 NULL 对象**（`:181-197`）。→ 在 AUTO 里它总是兜底成功的那个。
- **`fallback_gralloc_get_yuv_info`（`:44-98`）前提**：
  1. `handle->numFds != 0`（`:57`）
  2. `gr_mod != NULL && gr_mod->lock_ycbcr != NULL`（`:60`）——否则 `-EINVAL`
  3. `lock_ycbcr(...)` 成功；**失败且 `hal_format == IMPLEMENTATION_DEFINED` 时返回 `-EAGAIN`**（`:69-70`，配合 `:112-123` 的 RGBX hack），其它失败 → `-EINVAL`
  4. `bufferinfo_from_ycbcr()`（`u_gralloc_internal.c:134-179`）按 `chroma_order`（由 `cr < cb` 推断）与 `chroma_step`（2→NV12/2 plane，1→3 plane，YCrCb 时 YVU420）在 `droid_yuv_formats[]` 查表 → 查不到 → `-EINVAL`
  5. 单 fd 时把 `fds[1]=fds[2]=fds[0]`；多 fd 时 `assert(num_fds == num_planes)`（`:86-95`）——**assert，NDEBUG 下等于没检查**
- **非 YUV 路径（`:100-160`）**：`num_planes=1`，`offsets[0]=0`（结构体零值，不显式写），`fds[0]=handle->data[0]`，`strides[0]=pixel_stride*bpp`，**`modifier = DRM_FORMAT_MOD_INVALID`（`:145`）**。
  - 唯一会给出真 modifier 的分支是 `#ifdef HAS_FREEDRENO`（`:150-157`，靠 handle ints 里的 `'gmsm'` magic + UBWC 位）——meson 只在 `with_freedreno_vk or with_gallium_freedreno` 时定义，**本工程（panfrost）没有**。
- **失败点汇总**：YUV/`IMPLEMENTATION_DEFINED` 且模块无 `lock_ycbcr` → `-EINVAL`；fourcc 表外格式（`u_gralloc_internal.c:104-129`，**不含任何 MTK 私有 format 如 `0x7FA30C00+`**）→ `-EINVAL`；bpp==0 → `-EINVAL`；即使全成功，也**永远没有真 modifier、永远没有 plane offset**。

### 2.3 `u_gralloc_libdrm.c`（要求 gbm 模块）

- **create 前提**：`hw_get_module("gralloc")` 成功 **且** `strcmp(module->common.name, "GBM Memory Allocator") == 0`（`:82-89`）。名字不符 → `fail:` → `destroy()` → **返回 NULL**。
  - 即：必须装的是 `gbm_gralloc`，而它要打开 `/dev/dri/renderD*` ⇒ **无 `/dev/dri` 的机器上该模块本身就不存在**（本机直接出局）。
  - 另一半提前条件：`libdrm` 必须以 android 支持构建（提供 `android/gralloc_handle.h` 布局）。
- **`get_buffer_info`（`:35-59`）**：
  1. `assert(handle->base.numFds == 1)` / `assert(numInts == GRALLOC_HANDLE_NUM_INTS)` / `assert(magic == 0x60585350)` / `assert(version == 4)`（`:42-49`）→ **任何非 libdrm 布局的 handle = abort**（NDEBUG 下变成按错误布局读内存，`handle->stride/modifier` 全是垃圾）
  2. `:52` 调 fallback 的 `u_gralloc_get_buffer_basic_info(...)` **但忽略返回值**（fallback 失败也继续）
  3. 用 `handle->modifier` / `handle->stride` 覆盖（`:55-56`）
- 结构体定义在 `u_gralloc_libdrm.h`（libdrm `android/gralloc_handle.h` 的副本，`GRALLOC_HANDLE_MAGIC 0x60585350`, `VERSION 4`, `NUM_FDS 1`）。

### 2.4 `u_gralloc_qcom.c`

- **create 前提（全部满足才行，`:155-222`）**：
  1. `hw_get_module` 成功
  2. 模块 `name`/`author` **精确匹配**两组之一：`"Graphics Memory Allocator Module"`/`"The Android Open Source Project"` 或 `"Graphics Memory Module"`/`"Code Aurora Forum"`（`:169-180`）→ 不匹配即 NULL
  3. `module_api_version > 0.3` → `gralloc1_open` + `getFunction(GRALLOC1_FUNCTION_PERFORM = 0x1000)`；`<= 0.3` → 用 `gralloc_module->perform`（`:182-196`）
  4. `perform` 非空，且**探测调用** `perform(GET_STRIDE=2, 1024, format=1, &out_stride)` 成功且 `out_stride != 0`（`:201-212`）
  - 最后 `:220` 再建一个 fallback 当 YUV 兜底。
- **`get_buffer_info`（`:82-135`）**：`perform(GET_UBWC_FLAG=9)` 成功且 flag 非 0 → `DRM_FORMAT_MOD_QCOM_COMPRESSED`，否则/失败 → **`DRM_FORMAT_MOD_LINEAR`**（`:94-102`，注意这里不是 INVALID）；YUV 走 `perform(GET_YUV_PLANE_INFO=7)`；非 YUV 与 fallback 同构（`num_planes=1`，`fds[0]=data[0]`，`strides[0]=pixel_stride*bpp`，**offsets 不填**）。
- **失败点**：MTK 的 name/author 不匹配 ⇒ create 直接 NULL；即使匹配，`GET_YUV_PLANE_INFO` 不支持时 YUV 仍失败。

### 2.5 `u_gralloc_cros_api.c`

- **create 前提（`:216-248`）**：`hw_get_module` 成功 + `name == "CrOS Gralloc"` + `module->perform != NULL`。
- **`get_buffer_basic_info`（`:83-107`）**：一次 `perform(CROS_GRALLOC_DRM_GET_BUFFER_INFO = 4, handle, &info)`，`info` 是 CrOS 私有 ABI `cros_gralloc0_buffer_info { drm_fourcc; int num_fds; int fds[4]; uint64_t modifier; int offset[4]; int stride[4]; }` ⇒ **这是唯一能直接给出真 modifier + 真 plane offset 的既有后端**。perform 返回非 0 → `-EINVAL`（无任何回退）。
- 还实现 `get_buffer_color_info`（perform `6`，dataspace/chroma_siting → `__DRI*`，失败时**返回默认值而非错误**，`:135-147`）与 `get_front_rendering_usage`（perform `5`，`:109-124`）。
- **失败点**：本机 gralloc 不叫 CrOS Gralloc；且该 ABI 是 CrOS 私有，不可移植。文件头注释还点明：**API 35 起这些 AIDL 生成的头不再随 VNDK 发布**（所以他们把常量手抄进来了）。

### 2.6 `u_gralloc_imapper4_api.cpp`（HIDL `IMapper@4.0`）

- **编译前提**：`dependency('android.hardware.graphics.mapper', '>= 4.0')` + `platform-sdk-version >= 30`（`meson.build:1072`）+ C++17；头文件 `aidl/.../common/*.h`、`gralloctypes/Gralloc4.h`、`system/window.h`，链 `libhidlbase` 一族。
- **运行前提**：`IMapper::getService()` 拿得到 HIDL `mapper@4.0` 服务（`:288`），否则 create **返回 NULL**（安全的失败方式）。
- **`mapper4_get_buffer_basic_info`（`:87-157`）**：分开查三次 metadata：`PixelFormatFourCC`、`PixelFormatModifier`、`PlaneLayouts`（`GetMetadata()` 内部把 `get()` 的 `!ret.isOk()` 归一成 `NO_RESOURCES`）。**任一项 error/解码失败 → `-EINVAL`**（`:104, 120, 133`）。plane 的 fd 用 offset 推断：`i>0 && offset==0 → fd_index++`，`fd_index >= handle->numFds → -EINVAL`。
- **color_info**：ChromaSiting/Dataspace 是**可选**属性，取不到就跳过（`:178, 193`），只有解码失败才 `-EINVAL`。
- **失败点**：Android 15/16 上 HIDL `mapper@4.0` 服务与 VNDK 头（`gralloctypes`）都已退役；没有服务 ⇒ create NULL ⇒ AUTO 落到 LIBDRM/QCOM/FALLBACK。

### 2.7 `u_gralloc_imapper5_api.cpp`（libui `GraphicBufferMapper`，mapper4/5 通吃）

- **编译前提**：`dependency('ui')` + `platform-sdk-version >= 35`（`meson.build:1075`）；头 `ui/GraphicBufferMapper.h`、`aidl/.../common/*`、`system/window.h`。
- **运行前提**：`GraphicBufferMapper::get().getMapperVersion() >= GraphicBufferMapper::GRALLOC_4`（`:203`），否则 create 返回 NULL（同样安全失败）。
- **`mapper5_get_buffer_basic_info`（`:50-98`）**：`getPixelFormatFourCC` → `getPixelFormatModifier` → `getPlaneLayouts`；**任一失败 → `-EINVAL`**；fd 推断同 imapper4。
- **color_info（`:100-174`）**：`getChromaSiting` / `getDataspace` **都是硬要求**，失败即 `-EINVAL`（与 imapper4 的"可选"不同）。
- **失败点**：需要 `libui.so` 在进程命名空间可达（app 进程不保证）、需要 API 35 已从 VNDK 移除的 AIDL graphics-common 头（要自带）、需要 mapper 服务真的支持 `getPlaneLayouts`（MTK 的 mapper5 支持）。

---

## 3. `src/vulkan/runtime/vk_android.c` 里的 u_gralloc 使用点

（行号均指 **pristine `/root/mesa`** 的该文件；工作树版本因本地补丁行号有偏移）

### 3.1 全部使用点

| 行 | 函数 | 对 u_gralloc 做什么 | 结果去哪 |
|---|---|---|---|
| 42 | — | `#include "util/u_gralloc/u_gralloc.h"` | — |
| 55 / 57-61 / 63-69 | `_gralloc` / `vk_android_init_ugralloc_once` / `vk_android_get_ugralloc` | `call_once` 里 `u_gralloc_create(U_GRALLOC_TYPE_AUTO)`；**全进程只建一次** | 所有调用点共用 |
| 138-190 | `vk_gralloc_to_drm_explicit_layout` | **核心转换**：`:148` `u_gralloc_get_buffer_basic_info` | → `VkImageDrmFormatModifierExplicitCreateInfoEXT` + `VkSubresourceLayout[]` |
| 289-296 | `vk_android_get_anb_layout` | 用 `VkNativeBufferANDROID{handle, format, stride}` 组 `u_gralloc_buffer_handle` | 转交 138 |
| 581-592 | `vk_android_get_front_buffer_usage` | `u_gralloc_get_front_rendering_usage` | `VkSwapchainImageUsageFlagsANDROID::sharedImage` 判定（`panvk_vX_physical_device.c:1314`） |
| 584-590 | `vk_common_GetSwapchainGrallocUsage2ANDROID` | 间接（`import` 前缓冲 usage） | gralloc1 producer usage |
| 619-639 | `vk_android_get_ahb_layout` | `AHardwareBuffer_describe`（`stride`/`format`）+ `AHardwareBuffer_getNativeHandle` | 转交 138 |
| 932-943 | `get_ahb_buffer_format_properties2` | `u_gralloc_get_buffer_basic_info`（**只在 `desc.format` 不在 AHB 等价表、即 `format == VK_FORMAT_UNDEFINED` 的私有/YUV 格式时才走这里**） | → `externalFormat` |
| 965-970 | 同上 | `u_gralloc_get_buffer_color_info` | → `suggestedYcbcrModel/Range/XChromaOffset/YChromaOffset` |
| 1202-1210 | `vk_android_rp_attachment_has_external_format` | 不调 u_gralloc，但读 `VkExternalFormatANDROID`（与上面的 `externalFormat` 配对） | renderpass attachment 合法性 |

> 另有 `panvk_vX_physical_device.c:42`：`bool has_gralloc = vk_android_get_ugralloc() != NULL;` → 决定是否对外声明 `ANDROID_external_memory_android_hardware_buffer` 与 `ANDROID_native_buffer`（`:231-232`）。**u_gralloc_create 返回 NULL 会让 panvk 干脆不声明 AHB/ANB 支持**。

### 3.2 数据流 A/B：→ `VkImageDrmFormatModifierExplicitCreateInfoEXT` + `VkSubresourceLayout`

`vk_gralloc_to_drm_explicit_layout`（`:138-190`）逐步：

```
:148  u_gralloc_get_buffer_basic_info() 失败            -> VK_ERROR_INVALID_EXTERNAL_HANDLE
:151  info.num_planes > max_planes                      -> VK_ERROR_INVALID_EXTERNAL_HANDLE
:154  i>0 且 offsets[i]==0（判 disjoint，尚不支持）      -> VK_ERROR_INVALID_EXTERNAL_HANDLE
:167  memset(out) / memset(out_layouts)
:170  out->sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT
:172  out->pPlaneLayouts = out_layouts
:174  out->drmFormatModifier = info.modifier            <-- INVALID 直接流出去
:175  out->drmFormatModifierPlaneCount = info.num_planes
:177  out_layouts[i].offset   = info.offsets[i]
:178  out_layouts[i].rowPitch = info.strides[i]
:181  drm_fourcc == DRM_FORMAT_YVU420 时交换 plane1/plane2（对齐 VK_FORMAT_G8_B8_R8_3PLANE_420）
```

两个入口：
- **ANB**：`vk_android_get_anb_layout`（`:280-297`）→ 被 `panvk_android_anb_init`（`panvk_android.c:113-135`）挂进 `create_info->pNext`（外加 `VkExternalMemoryImageCreateInfo{handleTypes = DMA_BUF}`）。
- **AHB**：`vk_android_get_ahb_layout`（`:619-639`）→ `panvk_android_ahb_image_init`（`panvk_android.c:435+`）用 `__vk_append_struct` 追加同一对结构。

### 3.3 数据流 C：→ `VkExternalFormatANDROID`

`get_ahb_buffer_format_properties2`（`:870-1015`）只在 `vk_ahb_format_to_image_format(desc.format) == VK_FORMAT_UNDEFINED`（即 AHB 用了等价表外的私有/YUV 格式，**正是 MTK 场景**）时才查 u_gralloc：

```
:939  u_gralloc_get_buffer_basic_info()                -> 失败 VK_ERROR_INVALID_EXTERNAL_HANDLE
:945  switch (info.drm_fourcc):
        DRM_FORMAT_YVU420 -> VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM
        DRM_FORMAT_NV12   -> VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
        DRM_FORMAT_P010   -> VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16
        DRM_FORMAT_XBGR8888 -> VK_FORMAT_R8G8B8A8_UNORM
        default           -> VK_ERROR_INVALID_EXTERNAL_HANDLE（:962）
:966  u_gralloc_get_buffer_color_info()                -> 失败 VK_ERROR_INVALID_EXTERNAL_HANDLE
:972  yuv_color_space  -> p->suggestedYcbcrModel（601/709/2020）
:986  sample_range     -> p->suggestedYcbcrRange（NARROW/FULL）
:989  horizontal/vertical_siting -> suggestedX/YChromaOffset（MIDPOINT / COSITED_EVEN）
:1004 p->externalFormat = external_format   <-- 应用把它填回 VkExternalFormatANDROID::externalFormat
:998  p->formatFeatures 由 vkGetPhysicalDeviceFormatProperties2 补齐，再 |MIDPOINT_CHROMA
```

同一函数前面还有一层"是否为等价表内格式"的短路：`p->format != VK_FORMAT_UNDEFINED` 时直接 `goto finish`（`:917-918`），**完全不碰 u_gralloc**。

### 3.4 `VK_ERROR_INVALID_EXTERNAL_HANDLE` 的**每一个**返回点（vk_android.c 内）

| 行 | 函数 | 触发条件 |
|---|---|---|
| 149 | `vk_gralloc_to_drm_explicit_layout` | `u_gralloc_get_buffer_basic_info` 返回非 0（**任何后端任何失败都汇到这里**） |
| 152 | 同上 | `info.num_planes > max_planes` |
| 164 | 同上 | `i>0 && offsets[i]==0` → 判定为 disjoint planes，暂不支持 |
| 218 | `vk_android_import_anb_memory` | `mem_reqs.memoryTypeBits & fd_props.memoryTypeBits == 0`（fd 的 memory type 与图像不兼容） |
| 888 | `get_ahb_buffer_format_properties2` | `desc.usage` 里没有任何 `AHARDWAREBUFFER_USAGE_GPU_*` |
| 895 | 同上 | `desc.layers > 1`（返回 `VK_ERROR_INVALID_EXTERNAL_HANDLE_KHR`，Vulkan 里是同一个值的别名） |
| 942 | 同上 | `u_gralloc_get_buffer_basic_info` 失败（前面会 `mesa_loge("Failed to get u_gralloc_buffer_basic_info")`） |
| 962 | 同上 | `info.drm_fourcc` 不在 NV12/YVU420/P010/XBGR8888 白名单 |
| 969 | 同上 | `u_gralloc_get_buffer_color_info` 失败 |

**此外还有两条"不是返回错误而是崩溃"的隐式路径（NDEBUG 下尤其危险）：**
- `:146 assert(u_gralloc)` —— buildtype=release 时 assert 被编译掉；若 `u_gralloc_create` 返回 NULL，`:148` 会执行 `u_gralloc_get_buffer_basic_info(NULL, …)` → 解引用 `gralloc->ops` → **SIGSEGV**。
- `:939` 更彻底：**连 assert 都没有**，直接 `u_gralloc_get_buffer_basic_info(vk_android_get_ugralloc(), …)` → 同上 SIGSEGV。而这条路径恰好是"私有/YUV 格式 AHB 属性查询"——本项目的核心场景。

---

## 4. 结论：Android 16/MTK 上哪条路可行

### 4.1 先看这套构建实际编进去了什么（决定性事实）

`-Dandroid-stub=true` 会**跳过**平台库探测（`meson.build:1055-1057`）：

```meson
if with_platform_android
  dep_android_ui = null_dep          # :1055
  dep_android_mapper4 = null_dep     # :1056
  if not with_android_stub           # :1057  <-- 本工程为 true，整块跳过
    ...
    dep_android_mapper4 = dependency('android.hardware.graphics.mapper', ...)  # :1072
    dep_android_ui = dependency('ui', ...)                                     # :1075
```

于是：
1. `u_gralloc/meson.build:19-29` 两个 `found()` 都是 false ⇒ **imapper4 与 imapper5 两个文件都不参与编译**，`USE_IMAPPER4_METADATA_API` 未定义 ⇒ `u_gralloc.c:28-30` 那个槽位整条消失。**"用 libui IMapper v5" 这条路在当前构建里连代码都没编进去。**
2. `android_stub` 取代了 `cutils/hardware/log/sync/nativewindow`（`src/android_stub/meson.build:1-19`，`dep_android = declare_dependency(link_with : stub_libs)`），而 stub 的 `hw_get_module`（`src/android_stub/hardware_stub.cpp`）是：
   ```c
   int hw_get_module(const char *id, const struct hw_module_t **module) { return 0; }
   ```
   **返回 0 却不写 `*module`** ⇒ CROS 的 `strcmp(gr->gralloc_module->common.name, …)`（`u_gralloc_cros_api.c:228`）在 `gralloc_module == NULL` 上解引用 → **AUTO 的第 1 个后端就 SIGSEGV**，根本轮不到 fallback。（`/root/zenithblue/work/mesa` 已把它改成 `*module = nullptr; return -ENOENT;`，见 §4.5。）

### 4.2 逐条淘汰（结合"无 /dev/dri、gralloc 是 binderized AIDL HAL、gralloc.default.so 是空壳"）

| 后端 | 结果 | 原因 |
|---|---|---|
| CROS | ✗ | 模块名必须是 `"CrOS Gralloc"`；MTK 不是 |
| imapper4 / imapper5 | ✗✗ | **压根没编译**（android-stub 下 meson 跳过探测）；即使编了，imapper4 需要 HIDL mapper@4.0 服务（15/16 已退役） |
| LIBDRM | ✗ | 模块名必须是 `"GBM Memory Allocator"`（gbm_gralloc），而它要 `/dev/dri` ⇒ 本机不可能 |
| QCOM | ✗ | name/author 必须精确匹配 QCOM/CAF；MTK 用自己的 name |
| FALLBACK | **create 成功**（若 stub 已修） | 但它① 对 `IMPLEMENTATION_DEFINED`/YUV/任何 MTK 私有 format 直接 `-EINVAL`（无 `lock_ycbcr` 的模块/空壳）；② 就算走通 RGBA 路径，也只给 **`modifier = DRM_FORMAT_MOD_INVALID`** |

而 `DRM_FORMAT_MOD_INVALID` 对 panvk 是**致命**的，链路完整如下：
```
u_gralloc_fallback.c:145  modifier = DRM_FORMAT_MOD_INVALID
  -> vk_android.c:174     out->drmFormatModifier = DRM_FORMAT_MOD_INVALID   （原样进 Vulkan 结构体）
  -> panvk_image.c:354    panvk_image_get_explicit_mod -> 直接返回该值
      （此前 :350 的 assert(panvk_image_can_use_mod(...)) 在 NDEBUG 下被编译掉）
  -> pan_mod.c:818-828    pan_mod_get_handler(INVALID) 四个 handler 都不 match -> 返回 NULL
      （linear 的 match 只认 DRM_FORMAT_MOD_LINEAR，:661-664）
  -> panvk_image.c:456    mod_handler = NULL 存进 pan_image
  -> pan_layout.c:69      assert(image->mod_handler) 被 NDEBUG 编译掉
  -> pan_layout.c:74+     解引用 mod_handler->init_plane_layout -> SIGSEGV
```
即：**"能 create 成功的 fallback" ≠ "可用"**；`DRM_FORMAT_MOD_INVALID` 既不是合法 modifier，也不能被 panvk 容忍。

> 结论：**在 Android 16/MTK 上，现有 5 个后端没有一个能给出可用的 `basic_info`；必须新增（或替换）一个后端。** 唯一能拿到真 modifier + 真 plane offset/stride 的东西是**平台的 mapper 服务**。

### 4.3 新增后端的两条路线

- **路线 A（上游正解，代价高）**：让 imapper5 真正编译进来（libui `GraphicBufferMapper`）。
  - 需要：改 `meson.build` 让 `-Dandroid-stub=true` 时仍探测 `ui`；提供 `ui/GraphicBufferMapper.h` 的可编译替代（libui 的 C++ ABI 直接 dlsym 也可）；自带 API 35 已从 VNDK 删除的 AIDL graphics-common 头（`Dataspace.h`/`ChromaSiting.h`/`BufferUsage.h`/`ExtendableType.h`/`PlaneLayout*.h`）；运行期还需要 `libui.so` 在 app 进程命名空间可见（**不保证**，需实测）。
- **路线 B（最小补丁，推荐）**：**在 u_gralloc 里新建"Android mapper"后端，运行期 `dlopen` + `dlsym("AIMapper_loadIMapper")` 直连 mapper AIDL 服务**，用 `getStandardMetadata` 读：
  | StandardMetadataType | 值 | 用途 |
  |---|---|---|
  | LAYER_COUNT | 5 | 校验 array layer |
  | PIXEL_FORMAT_FOURCC | 7 | → `drm_fourcc` |
  | PIXEL_FORMAT_MODIFIER | 8 | → `modifier`（**必须校验 != INVALID**） |
  | ALLOCATION_SIZE | 10 | plane offset/total 的越界校验 |
  | CHROMA_SITING | 14 | → color_info |
  | PLANE_LAYOUTS | 15 | → `strides[]/offsets[]`（planeLayout: components, offsetInBytes, sampleIncrementInBits, strideInBytes, widthInSamples, heightInSamples, totalSizeInBytes, horizontalSubsampling, verticalSubsampling） |
  | DATASPACE | 17 | → color_info |
  - 优点：**不依赖 libui / VNDK AIDL 头 / 平台私有头**，只需要 `libbinder_ndk.so` 或 `libvndksupport.so`（二者都在 `/system/lib64`，可用绝对路径 dlopen 兜底）+ `/vendor/lib64/hw/mapper.mediatek.so`。
  - 注意：**纯 `AHardwareBuffer` NDK API 单独不够**——`AHardwareBuffer_describe` 只给 width/height/layers/format/usage/**stride**，不给 modifier、不给 plane offset；而且 ops 的入参里**根本没有 `AHardwareBuffer*`**（只有 `native_handle_t* + hal_format + pixel_stride`），所以"用 NDK AHB API 填 basic_info"在不改接口的前提下拿不到比 fallback 更多的信息，若为此扩接口仍拿不到 modifier ⇒ 不推荐。

### 4.4 最小补丁落点清单（函数名 + 文件 + 改法）

> 目标树：`/root/mesa`；编号顺序即建议实施顺序。

| # | 文件 | 函数 / 位置 | 改法 |
|---|---|---|---|
| 0 | `src/android_stub/hardware_stub.cpp` | `hw_get_module()` | **先决条件**：改成 `if (module) *module = nullptr; return -ENOENT;`（pristine 的 `return 0;` 会让 CROS/LIBDRM/QCOM/FALLBACK 的 `create()` NULL 解引用） |
| 1 | `src/util/u_gralloc/meson.build` | `files_u_gralloc` | 加入 `'u_gralloc_android_mapper.c'`；**不要**把它挂在 `dep_android_ui` / `dep_android_mapper4` 条件下 |
| 2 | `src/util/u_gralloc/u_gralloc_android_mapper.c`（新建） | `u_gralloc_android_mapper_create()` | 只设 `ops.get_buffer_basic_info`（+ 可选 `get_buffer_color_info`/`destroy`），**不要**调 `hw_get_module`；`create` 里可先只做 dlopen 可用性探测 |
| 2' | 同上 | `android_mapper_get_buffer_basic_info()` | `dlopen("libbinder_ndk.so")`→`AServiceManager_openDeclaredPassthroughHal("mapper","mediatek",…)`；失败退 `dlopen("libvndksupport.so")`→`android_load_sphal_library("mapper.mediatek.so")`；再退直接 `dlopen("/vendor/lib64/hw/mapper.mediatek.so")`。`dlsym("AIMapper_loadIMapper")` 取 mapper，`v5.importBuffer` → 依次读 LAYER_COUNT/FOURCC/MODIFIER/ALLOCATION_SIZE/PLANE_LAYOUTS → 填 `out` → `v5.freeBuffer`。**任何一步失败返回负 errno，绝不猜布局**（宁可让上层拿到 `VK_ERROR_INVALID_EXTERNAL_HANDLE`） |
| 2'' | 同上 | `android_mapper_get_buffer_color_info()` | 由 CHROMA_SITING(14)/DATASPACE(17) 映射到 `__DRI_YUV_COLOR_SPACE_*` / `__DRI_YUV_*_RANGE` / `__DRI_YUV_CHROMA_SITING_0*`（映射表可直接抄 `u_gralloc_cros_api.c:149-199` / `u_gralloc_imapper5_api.cpp:119-171`） |
| 3 | `src/util/u_gralloc/u_gralloc_internal.h` | `extern` 声明区 | 加 `extern struct u_gralloc *u_gralloc_android_mapper_create(void);` |
| 4 | `src/util/u_gralloc/u_gralloc.c` | `u_grallocs[]`（`:25-34`） | 在 CROS 之后、**LIBDRM/QCOM/FALLBACK 之前**插入 `{.type = U_GRALLOC_TYPE_ANDROID_MAPPER, .create = u_gralloc_android_mapper_create}` —— **顺序是关键**，放最后就永远轮不到 |
| 5 | `src/util/u_gralloc/u_gralloc.h` | `enum u_gralloc_type`（`:50-58`） | 加 `U_GRALLOC_TYPE_ANDROID_MAPPER`（保持在 `U_GRALLOC_TYPE_COUNT` 之前；`u_gralloc_cache` 数组大小自动跟随） |
| 6 | `src/util/u_gralloc/u_gralloc_fallback.c` | `panvk_v19_*` 全部 | 若采纳 §4.5 的现状，把这 275 行**搬进 #2 的新文件**，fallback 恢复上游语义（能填就填、填不了返回错误） |
| 7 | `src/vulkan/runtime/vk_android.c` | `vk_gralloc_to_drm_explicit_layout()`（`:146`）、`get_ahb_buffer_format_properties2()`（`:939`） | NDEBUG 下 `assert` 不构成保护：两处先判 `vk_android_get_ugralloc() == NULL` → 直接 `return VK_ERROR_INVALID_EXTERNAL_HANDLE` |
| 8 | `src/panfrost/vulkan/panvk_image.c` | `panvk_image_get_explicit_mod()`（`:349-356`）/ `panvk_image_get_mod()`（`:352-354`） | 显式拒绝 `DRM_FORMAT_MOD_INVALID`（返回错误/退回 LINEAR），不要落到 `assert(!"Invalid modifier")`+返回 INVALID；`panvk_image_init_layouts()`（`:456`）在 `mod_handler == NULL` 时返回 `VK_ERROR_INITIALIZATION_FAILED` 而不是带着 NULL 往下走 |

### 4.5 仓库现状对照（**重要：避免重复劳动**）

`/root/zenithblue/work/mesa`（非只读工作树，`git status` 有 31 个改动文件）里**已经**：

1. `src/android_stub/hardware_stub.cpp` —— 已修成 `*module = nullptr; return -ENOENT;`（即 §4.4 的第 0 条）。
2. `src/util/u_gralloc/u_gralloc_fallback.c` —— **+275 行**，就是路线 B 的雏形（`panvk_v19_*`）：
   - 加载顺序：`libbinder_ndk.so` → `AServiceManager_openDeclaredPassthroughHal("mapper","mediatek")` → 退 `libvndksupport.so` → `android_load_sphal_library("mapper.mediatek.so")` → 退直接 `dlopen("/vendor/lib64/hw/mapper.mediatek.so")`；
   - `dlsym("AIMapper_loadIMapper")` 得到 `struct { uint32_t version; struct v5 {...8 个函数指针...} }`；
   - 手写 parcel 解码：`u64 长度 + 接口名 "android.hardware.graphics.common.StandardMetadataType" + i64 类型值`，再按 PlaneLayout 字段顺序读；
   - 读 5/7/8/10/15，`modifier != DRM_FORMAT_MOD_INVALID && allocation != 0` 才接受，失败一律 `-EINVAL` 且注释明写 "refusing guessed layout"。
   - 即：**"必须新增后端"这件事已经做了一半，只是寄生在 fallback 的 `fallback_gralloc_get_buffer_info()` 末尾**（`:422` 附近）。
3. `src/vulkan/runtime/vk_android.c`（+38）与 `src/panfrost/vulkan/panvk_android.c`（+66）—— 改的是 **AHB 的 dma-buf fd 选择 / allocationSize**（遍历 `handle->numFds` 找可 `lseek` 且 `GetMemoryFdPropertiesKHR` 成功的 fd，新增 `panvk_android_find_dma_buf_fd()` 并覆盖 `panvk_GetAndroidHardwareBufferPropertiesANDROID`），**与 u_gralloc 逻辑无关**，不要和 §3 混淆。

**因此任务 04 建议的下一步（不是从零写）**：
- (a) 把 `panvk_v19_*` 提升为独立文件/独立后端，并按 §4.4 第 3/4/5 条插到 FALLBACK **之前**（现在它只在 fallback 内生效，一旦 fallback 因 YUV/`IMPLEMENTATION_DEFINED` 提前 `return`，mapper 路径根本不会被执行——**这是当前代码的隐性死路**：`fallback_gralloc_get_buffer_info` 对 YUV 格式在 `:113` 就 `return ret` 了，`:422` 的 mapper 查询只在非 YUV 且格式表命中时才到达）。
- (b) 用 `/root/research/aosp-gralloc/IMapperMetadataTypes.h`（另一路研究已拉到本地）对拍 parcel 头与各 PlaneLayout 字段顺序，确认 `getStandardMetadata(h, type, NULL, 0)` 的"先探长度再取"两段式在 MTK 实现上成立（有的实现要求第一次调用就带缓冲）。
- (c) 确认 `AIMapper` versioned struct 的函数指针顺序（`importBuffer, freeBuffer, lock, unlock, flushLockedBuffer, rereadLockedBuffer, getMetadata, getStandardMetadata`）——顺序错一个就是 SIGSEGV。
- (d) 给 panvk 补 §4.4 第 8 条的防御，避免 mapper 不可用时以 NULL `mod_handler` 崩溃收场。

### 4.6 现场判定清单（可执行）

```bash
# 1) 哪个后端生效（每个 create 成功都有一句 mesa_logi）
adb logcat | grep -E 'Using .*gralloc|Using IMapper|Using QCOM|Using gralloc0 CrOS|Using fallback'
# 2) mapper 路径是否通（工作树补丁自带日志）
adb logcat | grep -E 'P0A-V19|mapper load failed|AIMapper_loadIMapper'
# 3) 关键硬条件
adb shell 'ls -l /dev/dri 2>&1; ls /vendor/lib64/hw/ | grep -i -E "gralloc|mapper"; getprop ro.hardware; dumpsys -l | grep -i mapper'
# 4) modifier 真假（0x0=LINEAR，0x00ffffffffffffff=INVALID，必须拒绝）
adb logcat | grep -E 'accepted fourcc|modifier=0x'
# 5) 后端是否真被编译进 .so
llvm-nm -D --defined-only libvulkan_panfrost.so | grep -i 'ugralloc'   # 看得到 *_create 符号即为编入
```

---

## 附：u_gralloc 相关文件索引（pristine `/root/mesa`）

| 文件 | 行数 | 关键内容 |
|---|---|---|
| `src/util/u_gralloc/u_gralloc.h` | 83 | 3 个 public 结构 + `enum u_gralloc_type` + 6 个 API |
| `src/util/u_gralloc/u_gralloc_internal.h` | 56 | `u_gralloc_ops`、`u_gralloc`、各 `*_create` 声明、legacy 辅助函数声明 |
| `src/util/u_gralloc/u_gralloc.c` | 156 | 后端表（AUTO 顺序）、按类型缓存 + refcount、包装函数（零初始化 + 可选 op 默认值） |
| `src/util/u_gralloc/u_gralloc_internal.c` | 179 | YUV fourcc 查表、bpp 表、`get_fourcc_from_hal_format`、`bufferinfo_from_ycbcr` |
| `src/util/u_gralloc/u_gralloc_fallback.c` | 198 | legacy `gralloc_module_t`（含 `lock_ycbcr`） |
| `src/util/u_gralloc/u_gralloc_libdrm.c/.h` | 106/62 | gbm_gralloc + libdrm handle 布局 |
| `src/util/u_gralloc/u_gralloc_qcom.c` | 228 | QCOM/CAF gralloc1 `perform` 探测 |
| `src/util/u_gralloc/u_gralloc_cros_api.c` | 249 | CrOS perform ABI（唯一直接给 offset 的既有后端） |
| `src/util/u_gralloc/u_gralloc_imapper4_api.cpp` | 304 | HIDL IMapper@4.0 元数据 |
| `src/util/u_gralloc/u_gralloc_imapper5_api.cpp` | 218 | libui `GraphicBufferMapper`（与 imapper4 同名 create 符号） |
| `src/util/u_gralloc/meson.build` | 49 | `dep_android_ui` / `dep_android_mapper4` 二选一 |
| `src/vulkan/runtime/vk_android.c` | 1212 | §3 的全部调用点 |
| `src/android_stub/hardware_stub.cpp` | 8 | `hw_get_module` 地雷（pristine 版） |
| `src/panfrost/vulkan/panvk_image.c:349-356, 456` / `panfrost/lib/pan_mod.c:818-828` / `panfrost/lib/pan_layout.c:69` | — | `DRM_FORMAT_MOD_INVALID` 崩溃链 |
