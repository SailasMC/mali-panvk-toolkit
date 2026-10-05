# 任务 05：设计"绕开 u_gralloc"的最小补丁（使 PanVK 能创建交换链图像）

> 只读探查完成。**未修改 `/root/mesa`、任何 build 目录、手机**；本文件是唯一产出。
> 探查时间：2026-10-05；被探查对象：`/root/mesa`（纯净上游 pin）与 `/root/zenithblue/work/mesa`（真正的构建源树，45 个文件已改）

## 摘要（≤14 行）

1. **推荐方案 A**：只改 `src/vulkan/runtime/vk_android.c` 一个文件，新增"AHB 自描述回退"，约 **90 行**，无需 meson 改动。
2. 触发条件严格**加性**：仅当 `u_gralloc_get_buffer_basic_info()` 已经失败时才生效，对正常设备零影响。
3. 关键校正：本树交换链走的是 **AHB 路径**（不是 ANB）——`work/mesa` 里 `panvk_wsi.c` 的 Android WSI 是**本项目新增**（上游 `/root/mesa` 同名文件 0 处 `android`），AHB 由我们自己 `AHardwareBuffer_allocate()`，所以"线性"可被证明而非猜测。
4. `AHardwareBuffer_describe()` 已经是 `.so` 的 UND 符号（由 `libnativewindow.so` 提供），**无链接/meson 改动**；`AHardwareBuffer_Desc` 经 `vndk/hardware_buffer.h` 传递包含 NDK 的 `/opt/android-ndk-r27c/.../usr/include/android/hardware_buffer.h`。
5. 编译目录：**`/root/zenithblue/build/android-v4`**——4 个 build 目录里**唯一**有产物，且产物与部署件 `dist/android-g720-v12-csf/libvulkan_panfrost.so` **md5 一致**。
6. 重编范围：1 个 `.c` → `libvulkan_lite_runtime.a.p/vk_android.c.o` → 重打 `.a` → 重链 20MB `.so`；预计 **30–60 s**（2 vCPU）。
7. 方案 B（新增 `u_gralloc_ahb.c` 后端）**拿不到 `AHardwareBuffer*`**（后端只收到 `native_handle_t`），无法实现题目设想的 `AHardwareBuffer_describe` 语义 → 退化为"按 hal_format 猜且不知道宽度"，还多 4 处改动 + meson 重配置 → **不选**。
8. 重要坑：新后端**必须插在 FALLBACK 之前**，因为 `u_gralloc_fallback_create()` 永远返回非 NULL，追加在末尾是死代码。
9. 重要坑：链接带 **`-Wl,--no-undefined`**，任何新外部符号（如 `AHardwareBuffer_lockPlanes`）必须同时在 `src/android_stub/nativewindow_stub.cpp` 补桩，否则构建失败。
10. 风险集中在 rowPitch（`desc.stride` 单位=像素）与 LINEAR 假设；全部走 **fail-closed**（拒绝而非猜），并提供 `PANVK_GRALLOC_NO_FALLBACK=1` 开关。
11. 回滚**不能**用 `git checkout --`（该文件本来就有未提交改动，会连带丢失）；用编辑前 `cp` 备份恢复。
12. 改动行数：A ≈ 90–105 行（1 文件，M1b 再 +2 文件/6 行）；B ≈ 130 行 + 4 文件 + meson。
13. 成功率评估：A **70–80%** 能让 `vkCreateImage`/`vkAllocateMemory` 通过并拿到交换链图像；剩余失败点已不属于本错误码（panvk 布局/AFBC/呈现链）。
14. 编译时长：A ≈ 30–60 s；B ≈ 60–90 s（含 meson regen）。**本任务未执行任何编译。**

---

## 1. 精读：调用者与调用链（只读 `/root/mesa`）

### 1.1 失败点本体

`/root/mesa/src/vulkan/runtime/vk_android.c`

```
139  vk_gralloc_to_drm_explicit_layout(struct u_gralloc_buffer_handle *in_hnd,
                                        VkImageDrmFormatModifierExplicitCreateInfoEXT *out,
                                        VkSubresourceLayout *out_layouts, int max_planes)
144     struct u_gralloc_buffer_basic_info info;
145     struct u_gralloc *u_gralloc = vk_android_get_ugralloc();
146     assert(u_gralloc);
148     if (u_gralloc_get_buffer_basic_info(u_gralloc, in_hnd, &info) != 0)
149        return VK_ERROR_INVALID_EXTERNAL_HANDLE;      // ← 观测到的失败
152     if (info.num_planes > max_planes)  return VK_ERROR_INVALID_EXTERNAL_HANDLE;
155-162 多平面 disjoint 判定 → 也是 VK_ERROR_INVALID_EXTERNAL_HANDLE
176-187 out->drmFormatModifier = info.modifier;
        out->drmFormatModifierPlaneCount = info.num_planes;
        out_layouts[i].offset   = info.offsets[i];
        out_layouts[i].rowPitch = info.strides[i];
        (work/mesa 追加：layer_count>1 时 arrayPitch = alloc_size/layer_count)
```

`u_gralloc_get_buffer_basic_info()` 本体在 `/root/mesa/src/util/u_gralloc/u_gralloc.c:139`，它把 `ops.get_buffer_basic_info` 的结果拷出；失败即返回非 0。

### 1.2 两条入口（谁要这个信息）

| 入口 | 位置 | 喂进去的字段 | 谁调用 |
|---|---|---|---|
| **AHB 路径**（本机交换链实际走这条） | `vk_android.c:620 vk_android_get_ahb_layout()` | `description.format`（→hal_format）、`description.stride`（→pixel_stride）、`AHardwareBuffer_getNativeHandle()`（→handle） | `panvk_android.c:232 panvk_android_ahb_image_init()` ← `panvk_android_import_ahb_memory()` ← `vkAllocateMemory(AHB)` |
| ANB 路径 | `vk_android.c:281 vk_android_get_anb_layout()` | `native_buffer->format`、`native_buffer->stride`、`native_buffer->handle` | `panvk_android.c:121 panvk_android_anb_init()`（`vkCreateImage` with ANB）、`panvk_android.c:201 panvk_android_get_wsi_memory()` |
| （旁路）AHB 属性查询 | `vk_android.c:932-941`（`vk_GetAndroidHardwareBufferPropertiesANDROID`） | 同上 | 走 `info.drm_fourcc` 判 external format；`color_info` 另有 `vk_android.c:965` |

**完整交换链链路（本树）**：

```
App(panvk-test) vkCreateSwapchainKHR(ANativeWindow surface)
  └─ libvulkan_panfrost.so: panvk_wsi.c:323 panvk_wsi_android_create_swapchain()
       ├─ ANativeWindow_setBuffersGeometry(RGBA_8888)
       ├─ loop: AHardwareBuffer_allocate(desc{ R8G8B8A8_UNORM, layers=1,
       │            usage = GPU_COLOR_OUTPUT|GPU_SAMPLED_IMAGE|CPU_READ_OFTEN })
       ├─ vkCreateImage(VkExternalMemoryImageCreateInfo(AHB bit))
       │     └─ panvk_android_create_gralloc_image() → vk_android_init_deferred_image()
       │        （deferred：此刻不查布局）
       └─ vkAllocateMemory(VkImportAndroidHardwareBufferInfoANDROID + AHB)
             └─ panvk_android_import_ahb_memory() → panvk_android_ahb_image_init()
                  └─ vk_android_get_ahb_layout()            ← 620
                       ├─ AHardwareBuffer_describe()        ← 626（已存在）
                       └─ vk_gralloc_to_drm_explicit_layout()  ← 139
                            └─ u_gralloc_get_buffer_basic_info() == -EINVAL
                                 → VK_ERROR_INVALID_EXTERNAL_HANDLE   ★失败★
```

**要哪些位、后续怎么用**（`vk_gralloc_to_drm_explicit_layout` 真正消费的）：

* `info.modifier` → `VkImageDrmFormatModifierExplicitCreateInfoEXT::drmFormatModifier`：panvk 用它选 tiling/压缩布局；`DRM_FORMAT_MOD_INVALID` 会让 panvk 拿到空 modifier handler（补丁 `android/013` 头注释原话：*"The pinned fallback sets DRM_FORMAT_MOD_INVALID, which leaves PanVK with a NULL modifier handler and crashes in pan_image_layout_init"*）。
* `info.num_planes` / `offsets[i]` / `strides[i]` → 逐平面 `VkSubresourceLayout{offset,rowPitch}`：**rowPitch 错 = 画面斜切/撕裂**。
* `info.drm_fourcc`：在 `vk_gralloc_to_drm_explicit_layout` 里只用于 YVU420 的 U/V 交换；真正用它判外部格式的是 `vk_android.c:945-961`（AHB 属性查询）。
* （work/mesa 追加）`info.alloc_size` / `info.layer_count` → `arrayPitch`。

`panvk_image_can_use_mod()`（`/root/mesa/src/panfrost/vulkan/panvk_image.c:200-215`）中 `forced_linear` 分支**只接受 `DRM_FORMAT_MOD_LINEAR`**；`panvk_host_copy.c` 也把 LINEAR 列为受支持布局 → 因此补丁给出 LINEAR 是 panvk 侧可接受的取值。

### 1.3 为什么 AUTO 链上每个后端在本机都失败（逐条实证）

`u_gralloc.c:24-39` 顺序：CROS → (IMAPPER4/5，编译期) → LIBDRM → QCOM → FALLBACK。

| 后端 | 本机结果 | 证据 |
|---|---|---|
| CROS | `create()` 返回 NULL | `u_gralloc_cros_api.c:219` 首步 `hw_get_module()`；而 `work/mesa/src/android_stub/hardware_stub.cpp` 里 `hw_get_module()` **恒返回 -ENOENT 且 \*module=nullptr**（`android-stub=True`） |
| IMAPPER4/5 | 未编译进来 | `src/util/u_gralloc/meson.build`：需 `dep_android_ui.found()` 或 `dep_android_mapper4.found()` 才加 `-DUSE_IMAPPER4_METADATA_API` |
| LIBDRM | 失败 | 需要 `/dev/dri/*`（本机全 EACCES） |
| QCOM | 失败 | 需要 handle int 区有 `'gmsm'` magic + UBWC 位 |
| **FALLBACK** | **`create()` 恒成功**（`u_gralloc_fallback.c:196`：即使 `hw_get_module` 失败、`gralloc_module==NULL`，也返回 `&gr->base`）→ 于是 `get_buffer_basic_info` 由它执行 | 见下 |

`fallback_gralloc_get_buffer_info()`（`u_gralloc_fallback.c:100-172`）两条出口：

* **非 YUV**：`num_planes=1`；`get_fourcc_from_hal_format()`；`stride = pixel_stride * bpp`；`modifier = DRM_FORMAT_MOD_INVALID`（→ 后续 panvk crash 风险，见 §1.2）。
* **YUV 类**（`is_hal_format_yuv()` 为真，`u_gralloc_internal.c` 的 `droid_yuv_formats[]` 含 `HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED=0x22`、`YCbCr_420_888=0x23`、`YV12`）：转 `fallback_gralloc_get_yuv_info()` → `if (!gr_mod || !gr_mod->lock_ycbcr) return -EINVAL;`（`u_gralloc_fallback.c:113-115`）→ **硬 -EINVAL**，且注意上游那条 "lock_ycbcr 失败返回 -EAGAIN 当作 RGB" 的 hack 在这里不会触发（没有 lock_ycbcr 符号）。

### 1.4 根因矩阵（三种候选，本补丁全都要覆盖）

| 候选 | 机制 | 日志特征 | 补丁是否覆盖 |
|---|---|---|---|
| **R1** `pixel_stride == 0` | `AHardwareBuffer_Desc::stride` 为 0 或未填 → `stride=0` → `mesa_loge("Failed to calcuulate stride")` → -EINVAL | "Failed to calcuulate stride" | ✅（用 `desc.width` 校验 + M1b `lockPlanes` 取真实 bytes-per-stride） |
| **R2** format ∈ YUV 集合 | `description.format == 0x22/0x23` → lock_ycbcr 不存在 → -EINVAL | 无（静默 -EINVAL） | ✅（自己做 format→fourcc 映射，**永不调用 lock_ycbcr**） |
| **R3** modifier = INVALID | basic_info 成功但 modifier 无效 → panvk 空 handler → `pan_image_layout_init` crash | 不在本错误码内 | ✅（直接给 `DRM_FORMAT_MOD_LINEAR`） |

> 无法在服务器上取到设备 logcat（任务禁止碰手机）。以上三者互相独立、可同时成立，补丁对三者都是"绕过"而非"绕过其中一个"。

**关键结构性事实（决定 A/B 选型）**：`/root/mesa/src/panfrost/vulkan/panvk_wsi.c` 只有 4332 B、`android` 出现 **0** 次；而 `work/mesa` 同名文件含 48 处 `android` → **Android WSI 是本项目新增代码**，交换链的 AHB 由我们自己 `AHardwareBuffer_allocate()` 创建（format/usage/尺寸全在源码里）。这让我们对"这块 buffer 是线性的"有**可证明**的依据（见 §2.2），而不是违反项目"never guess"设计规则。

---

## 2. 方案 A（推荐）：`vk_android.c` 内 AHB 自描述回退

### 2.1 前提校正（务必先读）

1. 题目设想的"在 `vk_android.c` 里用 `AHardwareBuffer_describe()` 直接填字段"**成立**——但只对 **AHB 路径**成立（`vk_android_get_ahb_layout()` 手里有 `struct AHardwareBuffer *`）。**ANB 路径没有任何 AHardwareBuffer 对象**，那里只能退化用 ANB 字段合成（见 §2.5，默认关闭）。本机交换链走的恰好是 AHB 路径。
2. `AHardwareBuffer_describe()` **已经是** `libvulkan_panfrost.so` 的 UND 符号，由 `libnativewindow.so` 提供（`readelf -d` 显示 `NEEDED libnativewindow.so`）→ **不需要任何链接改动**。
3. 头文件：`vk_android.c:44-51` 已经在 `#if ANDROID_API_LEVEL >= 26` 下 `#include <vndk/hardware_buffer.h>`（本构建 `ANDROID_API_LEVEL=35`）。该头在本树解析为 `work/mesa/include/android_stub/vndk/hardware_buffer.h`，而它第一行就是 `#include <android/hardware_buffer.h>`（NDK 公共头，位于 `/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/android/hardware_buffer.h`）。因此 `AHardwareBuffer_Desc` / `AHardwareBuffer_describe` / `AHardwareBuffer_lockPlanes` / `AHardwareBuffer_Planes` 全部可见，**无需新增 include**。
4. **可用的 format 常量**（决定草稿能写哪些 case，已逐个 grep 核实）：
   * NDK 公共头提供：`R8G8B8A8_UNORM(1)`、`R8G8B8X8_UNORM(2)`、`R8G8B8_UNORM(3)`、`R5G6B5_UNORM(4)`、`R16G16B16A16_FLOAT(0x16)`、`R10G10B10A2_UNORM(0x2b)`、`Y8Cb8Cr8_420(0x23)`、`YCbCr_P010(0x36)`、`BLOB(0x21)`、`D16/D24/D32/S8`…
   * 只有 in-tree VNDK 头额外提供：`B8G8R8A8_UNORM(5)`、**`IMPLEMENTATION_DEFINED(0x22)`**、`YV12`、`RAW*`…
   * ⇒ 在 `vk_android.c` 里两类都能用；但在新建的 `u_gralloc/*.c` 里若只 include NDK 头，`B8G8R8A8_UNORM`/`IMPLEMENTATION_DEFINED` 就**不可用**（方案 B 的又一坑）。
5. `android-stub=True` 的副作用与部署事实：`work/mesa/src/android_stub/nativewindow_stub.cpp` 里 `AHardwareBuffer_describe()` 是**空实现**、`AHardwareBuffer_getNativeHandle()` 返回 **NULL**。但 `/root/zenithblue/dist/android-g720-v12-csf/` 里**只有 `libvulkan_panfrost.so` 一个文件、没有 `android_stub/` 目录**，`.so` 的 `RUNPATH=$ORIGIN/../../android_stub` 落空 ⇒ 设备上解析到**系统真实 `libnativewindow.so`**（否则 `AHardwareBuffer_allocate()` 就会先失败，而不是失败在 u_gralloc）。⚠️ 若哪天把 stub 一起部署，`describe()` 变空实现 → 本补丁会**如实 fail-closed**（校验字段后返回错误），不会静默出错。

### 2.2 为什么可以给 LINEAR（与被引用的项目设计规则不冲突）

项目内 `patches/android/013-vendor-mapper-metadata.patch` 头注释立的规则是"**never map a missing modifier to LINEAR, never guess**"，那条规则针对的是**别人分配的、我们不了解的** buffer。本补丁给出 LINEAR 的依据是：

* 该 AHB **由本项目自己的 WSI 分配**（`panvk_wsi.c: panvk_wsi_android_create_swapchain()`），usage 里带 `AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN`；
* Android 上 **压缩布局（AFBC/UBWC）的 buffer 不可被 CPU 映射**；带 CPU 访问 usage 的分配，厂商 gralloc 只会给线性/可映射布局；
* 因此"CPU 可映射"⇒"线性"在这里是可验证的推论，而非猜测。**进一步把推论变成实测**：M1b 用 `AHardwareBuffer_lockPlanes()` 锁一次，成功才认 LINEAR（失败即拒绝）。
* 建议顺手把 WSI 的 usage 补上 `CPU_WRITE_OFTEN`（M1c，1 行，见下），让"必须可映射"成为我们主动声明的约束。
* 除该路径外，补丁**一律 fail-closed**（YUV/深度/多平面/多 layer/无 pitch → 返回错误，不猜）。

### 2.3 字段映射表（AHB/`HAL_PIXEL_FORMAT` → DRM；与 u_gralloc 自身映射保持一致）

`AHARDWAREBUFFER_FORMAT_*` 与 `HAL_PIXEL_FORMAT_*` 在这几个历史值上**故意同号**，这也是上游把 `description.format` 当 `hal_format` 传下去的原因。

| AHB/HAL 值 | 常量 | `DRM_FORMAT_*` | bpp | 备注 |
|---|---|---|---|---|
| 1 | `AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM` / `HAL_PIXEL_FORMAT_RGBA_8888` | `DRM_FORMAT_ABGR8888` | 4 | 本机 WSI 就是它 |
| 2 | `..._R8G8B8X8_UNORM` / `RGBX_8888` | `DRM_FORMAT_XBGR8888` | 4 | |
| 5 | `..._B8G8R8A8_UNORM` / `BGRA_8888` | `DRM_FORMAT_ARGB8888` | 4 | 与 `u_gralloc_internal.c` 的 `get_fourcc_from_hal_format()` 一致 |
| 4 | `..._R5G6B5_UNORM` / `RGB_565` | `DRM_FORMAT_RGB565` | 2 | |
| 0x16 | `..._R16G16B16A16_FLOAT` / `RGBA_FP16` | `DRM_FORMAT_ABGR16161616F` | 8 | |
| 0x2b | `..._R10G10B10A2_UNORM` / `RGBA_1010102` | `DRM_FORMAT_ABGR2101010` | 4 | |
| **0x22** | `..._IMPLEMENTATION_DEFINED` | `DRM_FORMAT_XBGR8888` | 4 | **R2 根因**：上游 fallback 会把它判成 YUV 去 `lock_ycbcr()`；这里按上游自己的 cros hack 当 RGBX |
| 3 | `..._R8G8B8_UNORM` | `DRM_FORMAT_BGR888` | 3 | 上游 `get_fourcc_from_hal_format()` 缺这条，本补丁补上 |
| 0x23 / 0x36 | `Y8Cb8Cr8_420` / `YCbCr_P010` | — | — | **拒绝**（YUV，需真 metadata） |
| 0x21 / 0x30-0x35 | `BLOB` / `D16..S8_UINT` | — | — | **拒绝**（深度/原始，需真 metadata） |

`stride` 单位：`AHardwareBuffer_Desc::stride` 是**像素**（NDK 头原文 *"Row stride in pixels"*），`AHardwareBuffer_describe()` 填的是 `GraphicBuffer::getStride()`（AOSP `libs/nativewindow/AHardwareBuffer.cpp:188`）⇒ `rowPitch = stride * bpp`。真实字节 pitch（可能含对齐填充）由 M1b 精确取得。

### 2.4 代码草稿 M1（可直接使用）

目标文件：`/root/zenithblue/work/mesa/src/vulkan/runtime/vk_android.c`
改动：① 顶部加一个 include；② 在 `vk_android_get_ahb_layout()` 前插入两个 static 函数；③ 改 `vk_android_get_ahb_layout()` 的 return。

```c
/* ---------- ① 顶部 include 区（`#include "util/log.h"` 附近）追加 ---------- */
#include "util/debug.h"        /* debug_get_bool_option() */

/* ---------- ② 插到 vk_android_get_ahb_layout() 之前 ---------- */

/* 把 AHardwareBuffer/HAL_PIXEL_FORMAT 映射到 DRM fourcc + 每像素字节数。
 * 只接受单平面、非压缩、非 YUV、非深度的格式；其余返回 false（拒绝猜）。 */
static bool
vk_android_ahb_format_to_drm(uint32_t ahb_format, uint32_t *out_fourcc,
                             uint32_t *out_bpp)
{
   /* 注意：AHARDWAREBUFFER_FORMAT_* 与 HAL_PIXEL_FORMAT_* 在这些值上同号，
    * 这里沿用 u_gralloc 的映射方向，避免与其它路径产生二义性。 */
   switch (ahb_format) {
   case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:            /* 1    RGBA_8888  */
      *out_fourcc = DRM_FORMAT_ABGR8888;      *out_bpp = 4; return true;
   case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:            /* 2    RGBX_8888  */
      *out_fourcc = DRM_FORMAT_XBGR8888;      *out_bpp = 4; return true;
   case AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM:            /* 5    BGRA_8888  */
      *out_fourcc = DRM_FORMAT_ARGB8888;      *out_bpp = 4; return true;
   case AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM:              /* 4    RGB_565    */
      *out_fourcc = DRM_FORMAT_RGB565;        *out_bpp = 2; return true;
   case AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT:        /* 0x16 RGBA_FP16  */
      *out_fourcc = DRM_FORMAT_ABGR16161616F; *out_bpp = 8; return true;
   case AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM:         /* 0x2b RGBA1010102*/
      *out_fourcc = DRM_FORMAT_ABGR2101010;   *out_bpp = 4; return true;
   case AHARDWAREBUFFER_FORMAT_IMPLEMENTATION_DEFINED:    /* 0x22 ← R2 根因   */
      /* 与 Mesa cros/fallback 的同一条 hack 保持一致：实现自定义的 RGB
       * buffer 在本项目目标机上就是 RGBX_8888。 */
      *out_fourcc = DRM_FORMAT_XBGR8888;      *out_bpp = 4; return true;
   case AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM:              /* 3              */
      *out_fourcc = DRM_FORMAT_BGR888;        *out_bpp = 3; return true;
   default:
      /* YUV / BLOB / D16..S8_UINT / RAW*：一律交回 u_gralloc，不猜。 */
      return false;
   }
}

/* 不用 gralloc，从 AHardwareBuffer 自身描述构造显式 DRM modifier 布局。
 * 仅在 u_gralloc 已经失败后被调用 ⇒ 对可用 gralloc 的设备零行为变化。 */
static VkResult
vk_android_ahb_layout_from_desc(const AHardwareBuffer_Desc *desc,
                                VkImageDrmFormatModifierExplicitCreateInfoEXT *out,
                                VkSubresourceLayout *out_layouts, int max_planes)
{
   uint32_t fourcc, bpp;

   if (max_planes < 1)
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;

   if (desc->layers > 1) {
      mesa_loge("AHB layout fallback: %u layers unsupported", desc->layers);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   if (!vk_android_ahb_format_to_drm(desc->format, &fourcc, &bpp)) {
      mesa_loge("AHB layout fallback: format 0x%x has no known linear mapping",
                desc->format);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   /* desc->stride 单位是「像素」(NDK 头原文)，rowPitch 需要「字节」。 */
   uint64_t row_pitch = (uint64_t)desc->stride * bpp;

   if (row_pitch == 0) {
      /* 见说明 M1b：只有在能实测出真实 pitch 时才继续，否则拒绝。 */
      uint64_t probed = 0;
      if (!vk_android_ahb_probe_row_pitch(desc, bpp, &probed)) {
         mesa_loge("AHB layout fallback: stride==0 and lockPlanes probe failed; "
                   "refusing to guess a row pitch");
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;
      }
      row_pitch = probed;
   }

   if (row_pitch < (uint64_t)desc->width * bpp) {
      mesa_loge("AHB layout fallback: pitch %llu < width(%u)*bpp(%u)",
                (unsigned long long)row_pitch, desc->width, bpp);
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }

   memset(out, 0, sizeof(*out));
   memset(out_layouts, 0, sizeof(*out_layouts) * max_planes);

   out->sType =
      VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
   out->pPlaneLayouts = out_layouts;
   out->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;   /* 由 CPU 可映射性支撑 */
   out->drmFormatModifierPlaneCount = 1;
   out_layouts[0].offset = 0;
   out_layouts[0].rowPitch = row_pitch;
   out_layouts[0].size = row_pitch * desc->height;   /* 仅供参考 */

   mesa_logi("AHB layout fallback: %ux%u fmt=0x%x fourcc=0x%08x pitch=%llu (LINEAR)",
             desc->width, desc->height, desc->format, fourcc,
             (unsigned long long)row_pitch);
   return VK_SUCCESS;
}

/* ---------- ③ 改 vk_android_get_ahb_layout()（原文件 620-640 行） ---------- */
VkResult
vk_android_get_ahb_layout(
   struct AHardwareBuffer *ahardware_buffer,
   VkImageDrmFormatModifierExplicitCreateInfoEXT *out,
   VkSubresourceLayout *out_layouts, int max_planes)
{
   AHardwareBuffer_Desc description;
   const native_handle_t *handle =
      AHardwareBuffer_getNativeHandle(ahardware_buffer);

   AHardwareBuffer_describe(ahardware_buffer, &description);

   struct u_gralloc_buffer_handle gr_handle = {
      .handle = handle,
      .pixel_stride = description.stride,
      .hal_format = description.format,
   };

   VkResult result = vk_gralloc_to_drm_explicit_layout(&gr_handle, out,
                                                       out_layouts, max_planes);
   if (result == VK_SUCCESS)
      return result;

   /* ---------------- task-05 方案 A：gralloc-free 回退 ---------------- */
   if (debug_get_bool_option("PANVK_GRALLOC_NO_FALLBACK", false))
      return result;                        /* 一键回到旧行为，便于 A/B */

   mesa_logw("u_gralloc cannot describe AHB (%ux%u fmt=0x%x stride=%u); "
             "using self-described LINEAR layout",
             description.width, description.height, description.format,
             description.stride);
   return vk_android_ahb_layout_from_desc(&description, out, out_layouts,
                                          max_planes);
}
```

### 2.5 M1b（精确 rowPitch）+ 必须的桩改动

`AHardwareBuffer_lockPlanes()` 是 **NDK 公共 API（API 29+）**，返回的 `AHardwareBuffer_Planes::planes[0].rowStride` 是**字节**，即真实行 pitch（含对齐填充）——这是唯一不靠推断的 pitch 来源，也能**实测**证明 buffer 可 CPU 映射（⇒ 非 AFBC）。

```c
/* 插到 vk_android_ahb_layout_from_desc() 之前 */

/* 用 NDK 公共 API 实测真实行 pitch（字节）。返回 false = 不能锁/不可映射 →
 * 调用方必须 fail-closed（这正是「不猜」的实现）。 */
static bool
vk_android_ahb_probe_row_pitch(const AHardwareBuffer_Desc *desc,
                               uint32_t bpp, uint64_t *out_pitch);

/* 需要 AHardwareBuffer* 本体，因此由 vk_android_get_ahb_layout() 通过一个
 * 文件级暂存指针传入（或把本函数改为收 AHardwareBuffer*，见下）。 */
static AHardwareBuffer *vk_android_probe_ahb;   /* 简单实现：进入时赋值 */

static bool
vk_android_ahb_probe_row_pitch(const AHardwareBuffer_Desc *desc, uint32_t bpp,
                               uint64_t *out_pitch)
{
   (void)desc; (void)bpp;
   if (!vk_android_probe_ahb)
      return false;

   AHardwareBuffer_Planes planes;
   memset(&planes, 0, sizeof(planes));
   if (AHardwareBuffer_lockPlanes(vk_android_probe_ahb,
                                  AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
                                  -1, NULL, &planes) != 0)
      return false;
   AHardwareBuffer_unlock(vk_android_probe_ahb, NULL);

   if (planes.planeCount < 1 || planes.planes[0].rowStride == 0)
      return false;
   *out_pitch = planes.planes[0].rowStride;      /* 字节，精确 */
   return true;
}
```

> 更干净的写法是让 `vk_android_ahb_layout_from_desc()` 直接收 `AHardwareBuffer *`（而不是只收 `desc`），把 `AHardwareBuffer_describe()` 也搬进去。草稿里用暂存指针是为了保持与 M1 的最小 diff；实现者按喜好二选一即可。

**必须同步修改（否则构建失败）**：链接命令带 `-Wl,--no-undefined`（`build/android-v4/build.ninja` 实测），而本构建链接的是 ASCII 桩 `src/android_stub/libnativewindow.so`，桩里**没有** `AHardwareBuffer_lockPlanes`。因此要在

`/root/zenithblue/work/mesa/src/android_stub/nativewindow_stub.cpp`

追加（文件已 include `<vndk/hardware_buffer.h>`，`AHardwareBuffer_Planes` 可见）：

```c
int
AHardwareBuffer_lockPlanes(AHardwareBuffer *buffer, uint64_t usage,
                           int32_t fence, const ARect *rect,
                           AHardwareBuffer_Planes *outPlanes)
{
   /* 离线/宿主桩：让调用方 fail-closed（设备上由系统真实
    * libnativewindow.so 提供真实实现）。 */
   return -EINVAL;
}
```

### 2.6 M1c（建议，1 行）：把"必须可映射"写进我们自己的分配

`work/mesa/src/panfrost/vulkan/panvk_wsi.c`（本项目新增文件）`panvk_wsi_android_create_swapchain()` 的 AHB usage：

```c
         .usage = AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                  AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                  AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,   /* 已有 */
+                 AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,  /* M1c: 强制可映射 ⇒ 线性 */
```

### 2.7 M2（备选，默认关闭）：ANB 路径的 handle 合成

若以后要覆盖"游戏走 BufferQueue/ANB 交换链"的场景（`vk_android_get_anb_layout()`，那里**没有** AHB 对象），可在 `vk_gralloc_to_drm_explicit_layout()` 失败后加一条 env-gated 分支：

```c
   if (u_gralloc_get_buffer_basic_info(u_gralloc, in_hnd, &info) != 0) {
      /* task-05 M2：仅当显式开启时才用 handle 上的 hal_format/stride 合成。
       * ⚠️ ANB 不携带 width，pixel_stride==0 时必须拒绝（无法推出 pitch）。 */
      if (debug_get_bool_option("PANVK_GRALLOC_GUESS", false) &&
          vk_android_layout_from_handle(in_hnd, &info) == VK_SUCCESS)
         goto have_info;
      mesa_loge("u_gralloc_get_buffer_basic_info failed");
      return VK_ERROR_INVALID_EXTERNAL_HANDLE;
   }
have_info:
```
`vk_android_layout_from_handle()` 就是"format→fourcc（用 §2.3 同表）+ num_planes=1 + offset 0 + `strides[0] = pixel_stride*bpp` + `modifier = DRM_FORMAT_MOD_LINEAR`"，`pixel_stride<=0` 时返回失败。**默认关闭**是为了尊重项目"never guess"规则——ANB 的 buffer 不是我们分配的，线性性无法证明。

---

## 3. 方案 B（评估后不选）：新增 `src/util/u_gralloc/u_gralloc_ahb.c` 后端

### 3.1 文件清单与 meson 改动

| 文件 | 改动 |
|---|---|
| `src/util/u_gralloc/u_gralloc_ahb.c` | **新增**（约 90 行） |
| `src/util/u_gralloc/meson.build` | `files_u_gralloc` 里加 `'u_gralloc_ahb.c'` |
| `src/util/u_gralloc/u_gralloc_internal.h` | 加 `extern struct u_gralloc *u_gralloc_ahb_create(void);` |
| `src/util/u_gralloc/u_gralloc.c` | `u_grallocs[]` 注册（**必须插在 FALLBACK 之前**） |
| `src/util/u_gralloc/u_gralloc.h` | 可选：`enum u_gralloc_type` 加 `U_GRALLOC_TYPE_AHB` |

```diff
--- a/src/util/u_gralloc/meson.build
+++ b/src/util/u_gralloc/meson.build
 files_u_gralloc = files(
   'u_gralloc.c',
   'u_gralloc_internal.c',
+  'u_gralloc_ahb.c',
   'u_gralloc_fallback.c',
```

```diff
--- a/src/util/u_gralloc/u_gralloc.c
+++ b/src/util/u_gralloc/u_gralloc.c
@@
    {.type = U_GRALLOC_TYPE_QCOM, .create = u_gralloc_qcom_create},
+   /* task-05 (b)：末位兜底的自描述后端。
+    * 必须放在 FALLBACK 之前——u_gralloc_fallback_create() 永远成功，
+    * 追加在它后面的后端是死代码。 */
+   {.type = U_GRALLOC_TYPE_AHB, .create = u_gralloc_ahb_create},
    {.type = U_GRALLOC_TYPE_FALLBACK, .create = u_gralloc_fallback_create},
```

> 若不想动 `u_gralloc.h`（该头被 5 个文件 include），可把 `.type` 暂时写成 `U_GRALLOC_TYPE_FALLBACK`——因为 `u_gralloc_cache[]` 是按**请求的** type（这里是 `AUTO`）索引的，entry 的 `.type` 只影响 `u_gralloc_get_type()` 的返回值。代价是类型上报不准。

### 3.2 代码草稿

```c
/* src/util/u_gralloc/u_gralloc_ahb.c —— task-05 方案 (b)
 * 末位兜底：不查 gralloc，直接用 handle + hal_format + pixel_stride 合成
 * 单平面 LINEAR 布局。 */
#include "u_gralloc_internal.h"

#include <errno.h>

#include <hardware/gralloc.h>          /* HAL_PIXEL_FORMAT_*（经 include/android_stub） */
#include <vndk/hardware_buffer.h>      /* AHARDWAREBUFFER_FORMAT_* 全集（含 0x22/0x5）
                                        * 只 include <android/hardware_buffer.h> 会缺
                                        * B8G8R8A8_UNORM / IMPLEMENTATION_DEFINED */
#include "drm-uapi/drm_fourcc.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/u_memory.h"

struct ahb_gralloc {
   struct u_gralloc base;
};

static bool
ahb_format_to_drm(int hal_format, uint32_t *fourcc, uint32_t *bpp)
{
   switch (hal_format) {
   case HAL_PIXEL_FORMAT_RGBA_8888:          *fourcc = DRM_FORMAT_ABGR8888;      *bpp = 4; return true;
   case HAL_PIXEL_FORMAT_RGBX_8888:
   case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED: /* 0x22 ← R2 根因 */
                                             *fourcc = DRM_FORMAT_XBGR8888;      *bpp = 4; return true;
   case HAL_PIXEL_FORMAT_BGRA_8888:          *fourcc = DRM_FORMAT_ARGB8888;      *bpp = 4; return true;
   case HAL_PIXEL_FORMAT_RGB_565:            *fourcc = DRM_FORMAT_RGB565;        *bpp = 2; return true;
   case HAL_PIXEL_FORMAT_RGBA_FP16:          *fourcc = DRM_FORMAT_ABGR16161616F; *bpp = 8; return true;
   case HAL_PIXEL_FORMAT_RGBA_1010102:       *fourcc = DRM_FORMAT_ABGR2101010;   *bpp = 4; return true;
   default:
      return false;   /* YUV/深度：交给 fallback，绝不猜 */
   }
}

static int
ahb_get_buffer_basic_info(struct u_gralloc *gralloc,
                          struct u_gralloc_buffer_handle *hnd,
                          struct u_gralloc_buffer_basic_info *out)
{
   uint32_t fourcc, bpp;

   if (!hnd || !hnd->handle || hnd->handle->numFds == 0)
      return -EINVAL;
   if (!ahb_format_to_drm(hnd->hal_format, &fourcc, &bpp))
      return -EINVAL;
   if (hnd->pixel_stride <= 0)
      return -EINVAL;             /* 拿不到 pitch —— 不猜（ANB/AHB 皆无 width） */

   out->drm_fourcc = fourcc;
   out->modifier = DRM_FORMAT_MOD_LINEAR;
   out->num_planes = 1;
   out->fds[0] = hnd->handle->data[0];
   out->offsets[0] = 0;
   out->strides[0] = hnd->pixel_stride * bpp;
   return 0;
}

static int
ahb_destroy(struct u_gralloc *gralloc)
{
   FREE((struct ahb_gralloc *)gralloc);
   return 0;
}

struct u_gralloc *
u_gralloc_ahb_create(void)
{
   struct ahb_gralloc *gr = CALLOC_STRUCT(ahb_gralloc);
   if (!gr)
      return NULL;
   gr->base.ops.get_buffer_basic_info = ahb_get_buffer_basic_info;
   gr->base.ops.destroy = ahb_destroy;
   mesa_logi("Using AHB self-describing gralloc backend");
   return &gr->base;
}
```

### 3.3 为什么方案 B 不选（三条硬理由）

1. **拿不到 `AHardwareBuffer*`**：后端接口只有 `struct u_gralloc_buffer_handle { const native_handle_t *handle; int hal_format; int pixel_stride; }`（`u_gralloc.h:24-28`）。题目设想的"后端里用 `AHardwareBuffer_describe()`"**在架构上不可能**——方案 B 只能按 `hal_format` 猜 fourcc、按 `pixel_stride` 算 pitch，**且永远不知道 width**（无法做 `rowPitch >= width*bpp` 健全性校验，也无法在 stride==0 时用宽度兜底）。它严格弱于方案 A。
2. **影响面更大**：改的是**全局 AUTO 后端选择**，会影响所有 profile/设备；方案 A 只在"u_gralloc 已经失败"之后才生效，天然加性。若把 B 放到 `u_grallocs[]` 最前（题目"优先尝试"），它会在所有设备上抢占 CROS/imapper（更糟）。
3. **构建更重**：4 个文件 + meson 重配置；`u_gralloc.h` 被 5 个文件 include（`egl_dri2.h`、`anv_private.h`、`vn_android.c`、`v3dv_device.c`、`vk_android.c`），改 enum 会牵动这些（本 profile 只编 panfrost，实际只多编 2 个对象，但仍是"能少改就少改"）。

**结论：B 作为"长期/可上游化架构"保留（与 `patches/android/013` 的 mapper 路线互补），落地选 A。**

---

## 4. 编译验证方案（**已只读探查，未执行任何编译**）

### 4.1 探查结论（路径与目标）

| 项 | 实测值 |
|---|---|
| **应改的 build 目录** | **`/root/zenithblue/build/android-v4`** |
| 判据 | `android-bionic`/`android-v2`/`android-v3`/`android-v4` 中**只有 v4** 产出 `src/panfrost/vulkan/libvulkan_panfrost.so`（20,003,136 B，2026-10-05 01:51:05） |
| 它就是要部署的那份 | `md5(build/android-v4/…/libvulkan_panfrost.so) == md5(dist/android-g720-v12-csf/libvulkan_panfrost.so) == 4417b369591fc2b3df27e22019ccf3a2` |
| **真正的源码树** | **`/root/zenithblue/work/mesa`**（所有 build.ninja 里都是 `../../work/mesa/...`）；`/root/mesa` 是**纯净上游**（`git status` 干净），**不要改它** |
| meson 选项（v4） | `buildtype=release`、`platforms=['android']`、`platform-sdk-version=35`（⇒ `-DANDROID_API_LEVEL=35`）、`vulkan-drivers=['panfrost']`、`gallium-drivers=[]`、`android-stub=True` |
| ninja 目标 | `src/panfrost/vulkan/libvulkan_panfrost.so` |
| `vk_android.c` 的编译单元 | `src/vulkan/runtime/libvulkan_lite_runtime.a.p/vk_android.c.o` → `libvulkan_lite_runtime.a` → 链入 `.so`（**只链 lite，不链 `libvulkan_runtime.a`**） |
| u_gralloc 的编译单元 | `src/util/u_gralloc/lib_mesa_u_gralloc.a.p/*.o` → `lib_mesa_u_gralloc.a` → 链入 `.so` |
| 冷构建规模 | 全量 1111 条 ninja edge（`android-v4.log` 末行 `[1111/1111]`） |
| 链接旗标风险 | 含 **`-Wl,--no-undefined`**；`NEEDED` 为 `liblog/libnativewindow/libsync/libm/libz/libdl/libc`，且链的是 `src/android_stub/libhardware.a` + stub 版 `libnativewindow.so` |

### 4.2 方案 A 的编译命令

```bash
# 第 0 步（只读，先看清会重编什么；-n 不写任何文件）
ninja -C /root/zenithblue/build/android-v4 -n \
      src/panfrost/vulkan/libvulkan_panfrost.so

# 第 1 步：真正构建（预计 3~5 条 edge）
ninja -C /root/zenithblue/build/android-v4 \
      src/panfrost/vulkan/libvulkan_panfrost.so
```

预期 edge：`vk_android.c.o`（重编）→ `libvulkan_lite_runtime.a`（重打包）→ `libvulkan_panfrost.so`（重链）→ 2 条 `panfrost_icd` custom command。**不会重编整个 Mesa**。

### 4.3 方案 B 的编译命令（同样只重链，但要先 meson 重生成）

```bash
ninja -C /root/zenithblue/build/android-v4 \
      src/panfrost/vulkan/libvulkan_panfrost.so
# meson.build 变了 → ninja 自动跑 regeneration 规则（meson --reconfigure）
# 之后多编 u_gralloc_ahb.c.o，并重打 lib_mesa_u_gralloc.a → 重链 .so
```

### 4.4 构建后验证（只读）

```bash
SO=/root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so
ls -l --time-style=full-iso "$SO"                       # mtime 变新、size 变化
readelf -d "$SO" | grep NEEDED                          # 依赖集合不变（M1 无新依赖）
readelf --dyn-syms -W "$SO" | grep -E 'AHAA|AHardwareBuffer'   # M1b 会新增 lockPlanes UND
llvm-nm -u "$SO" | grep -i '*gralloc*' || true          # 仍无 gralloc 强依赖
md5sum "$SO"                                            # 与构建前不同
```

设备侧烟测（由有权限的 agent 执行）：把 `.so` 推到启动器驱动目录 → 重启 App → logcat 里应出现
`AHB layout fallback: WxH fmt=0x… fourcc=0x… pitch=… (LINEAR)`，随后 `SWAPCHAIN=pass`；若仍失败，应看到 `P0A-V19-FULLPLANE`/`u_gralloc_get_buffer_basic_info failed` 的**新**组合，可据此判定落在 R1/R2/R3 的哪一支。

### 4.5 ⚠️ 不要新建 build 目录

仓库脚本 `repo-pushE/build/build_g720.sh` 用 `BDIR=…/build/android-v$(date +%s)`，并在构建前 `git -C work/mesa reset --hard <pin>` + `clean -fdq src/` —— 这会**抹掉当前 45 个未提交修改**并触发全量 1111-edge 重建（分钟级，且可能 PATCH-DRIFT）。**只允许在 `build/android-v4` 里增量 ninja。**

### 4.6 时长估算

| 步骤 | 时间（2 vCPU / 3 GB） |
|---|---|
| 单文件编译 `vk_android.c`（43 KB，-O2/-O3 release） | 2–5 s |
| 重打 `libvulkan_lite_runtime.a` | < 1 s |
| 重链 20 MB `.so`（lld） | 10–30 s |
| 2 条 ICD custom command | 1–3 s |
| **方案 A 合计** | **≈ 30–60 s** |
| 方案 B 额外（meson regen + 1 编译） | +20–30 s → **≈ 60–90 s** |

---

## 5. 正确性风险与回滚

### 5.1 风险表（每条都给出守卫）

| # | 风险 | 触发条件 | 症状 | 守卫 |
|---|---|---|---|---|
| 1 | **rowPitch 错**（最关键） | `desc.stride` 语义误用（像素 vs 字节）或真实 pitch 有对齐填充 | **画面斜切/对角撕裂**、按行错位 | 坚持 `rowPitch = stride*bpp`；`rowPitch >= width*bpp` 校验；`stride==0` 时用 M1b 的 `lockPlanes()` **实测字节 pitch**，拿不到就拒绝 |
| 2 | **LINEAR 假设不成立**（buffer 实为 AFBC/UBWC） | 分配时未声明 CPU 访问 / 厂商强制压缩 | 严重花屏、GPU fault | 我们自己的 WSI 带 `CPU_READ_OFTEN`（M1c 再加 `CPU_WRITE_OFTEN`）⇒ 必可映射 ⇒ 非压缩；M1b 的 `lockPlanes()` 成功即**实测证据**；失败则拒绝 |
| 3 | **RGBA/BGRA 映射反了** | fourcc 选错 | **红蓝互换** | 映射表与 `u_gralloc_internal.c:get_fourcc_from_hal_format()` 完全一致；用已知纯色帧验证 |
| 4 | `IMPLEMENTATION_DEFINED` 被当 RGBX，但实际是 YUV | App 真的分配 YUV AHB | 颜色垃圾/崩溃 | 仅本项目的 WSI 路径会走到（它显式分配 `R8G8B8A8_UNORM`）；ANB/通用路径默认关闭（M2 需显式 `PANVK_GRALLOC_GUESS=1`） |
| 5 | 多平面 / 多 layer | planar AHB、`desc.layers>1` | 平面偏移错 | `layers>1` 直接拒绝；YUV/深度格式直接拒绝（不生成 num_planes>1 的布局） |
| 6 | modifier 不被 panvk 接受 | LINEAR 不在支持列表 | release 下空 handler → 崩在 `pan_image_layout_init` | 已核实 `panvk_image.c:212` 的 `forced_linear` 分支只认 `DRM_FORMAT_MOD_LINEAR`，且 `panvk_host_copy.c:66` 把 LINEAR 列为支持布局 |
| 7 | 影响正常设备 | — | — | 补丁**严格加性**（只在 u_gralloc 返回错误后才跑）+ `PANVK_GRALLOC_NO_FALLBACK=1` 一键关闭 |
| 8 | M1b 用了 `lockPlanes` 但没补桩 | 忘记改 `nativewindow_stub.cpp` | **链接失败**（`--no-undefined`） | 见 §2.5，桩必须与调用同批提交 |

### 5.2 回滚方式（**重要陷阱**）

`work/mesa` 不是干净工作区（45 个文件已改、未提交），**`vk_android.c` 本身就在其中**。因此：

```bash
# ❌ 不要这样做：会把该文件回退到 pin 的 commit，连带丢掉项目既有改动
git -C /root/zenithblue/work/mesa checkout -- src/vulkan/runtime/vk_android.c

# ✅ 正确：编辑前做文件级备份，回滚时覆盖回去
cp /root/zenithblue/work/mesa/src/vulkan/runtime/vk_android.c \
   /root/research/05-vk_android.c.before
cp /root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so \
   /root/research/05-libvulkan_panfrost.before.so

# 回滚
cp /root/research/05-vk_android.c.before \
   /root/zenithblue/work/mesa/src/vulkan/runtime/vk_android.c
cp /root/research/05-libvulkan_panfrost.before.so \
   /root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so
ninja -C /root/zenithblue/build/android-v4 src/panfrost/vulkan/libvulkan_panfrost.so
```

若一并做了 M1b/M1c，备份/恢复要覆盖 `src/android_stub/nativewindow_stub.cpp` 与 `src/panfrost/vulkan/panvk_wsi.c`（同名 `.before` 副本）。另可用 `PANVK_GRALLOC_NO_FALLBACK=1`（env）做**零重编**的临时回退。

---

## 6. 结论

| 维度 | 方案 A（推荐） | 方案 B |
|---|---|---|
| 改动文件数 | **1**（M1b 再 +1 桩文件；M1c 再 +1） | 4–5 |
| 改动行数 | **≈ 90–105**（M1）／+ 约 30（M1b） | ≈ 130 + meson |
| 是否需要 meson | **否** | 是 |
| 能否用 `AHardwareBuffer_describe` | **能**（AHB 路径持有 AHB 对象） | **不能**（后端只有 native_handle） |
| 能否拿到 width 做健全性校验 | 能 | 不能 |
| 影响面 | 仅"u_gralloc 已失败"之后 | 全局 AUTO 后端顺序 |
| 编译时长 | **≈ 30–60 s** | ≈ 60–90 s |
| 估计成功率（拿到交换链图像） | **70–80%**（挡住的是错误码本身；后续若有 AFBC/呈现链问题属另外的补丁） | 50–60%（无 width 校验、无 AHB 信息） |

**推荐落地顺序**：M1（必须）→ M1b（若日志显示 `stride==0`，或想用实测 pitch 替代推断）→ M1c（建议，1 行）→ 之后再用 `patches/android/013` 的 mapper v5 路线把 LINEAR 替换成**真实 modifier**（那时本回退自然不再被触发，因为 u_gralloc 会成功）。

---

## 附录：本次探查的证据清单

| 结论 | 证据（文件:行 / 命令） |
|---|---|
| 失败调用点 | `/root/mesa/src/vulkan/runtime/vk_android.c:148`（`work/mesa` 同处已加 `mesa_loge`） |
| AHB 路径三个消费者 | `vk_android.c:620/281/932`；`panvk_android.c:121/201/232` |
| 消费的字段 | `vk_android.c:176-196`（modifier/num_planes/offset/rowPitch，work 树另用 alloc_size/layer_count） |
| 后端顺序与"FALLBACK 恒成功" | `u_gralloc.c:24-39`、`u_gralloc_fallback.c:196` |
| YUV 误判根因 | `u_gralloc_internal.c` `droid_yuv_formats[]` 含 `HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED`；`u_gralloc_fallback.c:113-115` 无 lock_ycbcr 即 `-EINVAL` |
| `hw_get_module` 被桩成恒失败 | `work/mesa/src/android_stub/hardware_stub.cpp`（`return -ENOENT; *module=nullptr`） |
| AHB 桩为空实现 | `work/mesa/src/android_stub/nativewindow_stub.cpp`（`AHardwareBuffer_describe(){}`、`getNativeHandle()→NULL`） |
| 部署件只有 `.so`（⇒ 设备用系统真 libnativewindow） | `ls /root/zenithblue/dist/android-g720-v12-csf/` → 仅 `libvulkan_panfrost.so` |
| `NEEDED libnativewindow.so` + `AHardwareBuffer_describe` 已 UND | `readelf -d` / `readelf --dyn-syms` 于 v4 的 `.so` |
| `-Wl,--no-undefined` | `grep -o 'Wl,--no-undefined' build/android-v4/build.ninja` |
| 唯一有产物的 build 目录 + 与 dist md5 相同 | `ls -d build/android-*`、`md5sum`（`4417b369591fc2b3df27e22019ccf3a2`） |
| 源码树是 `work/mesa` | `build.ninja` 内 `../../work/mesa/...`；`/root/mesa` git 干净 |
| Android WSI 是本项目新增 | `/root/mesa/.../panvk_wsi.c` 4332 B、`android` 0 次；`work/mesa` 同名文件 48 次 |
| 交换链自建 AHB（format/usage） | `work/mesa/src/panfrost/vulkan/panvk_wsi.c:323+`（`AHardwareBuffer_allocate`，`R8G8B8A8_UNORM` + `CPU_READ_OFTEN`） |
| LINEAR 被 panvk 接受 | `panvk_image.c:200-215`；`panvk_host_copy.c:66` |
| `describe()` 填 stride = getStride() | `/root/research/aosp-gralloc/AHardwareBuffer.cpp:188`（同文件 193 行 `AHardwareBuffer_lockAndGetInfo`） |
| NDK 头与常量可用性 | `/opt/android-ndk-r27c/.../usr/include/android/hardware_buffer.h`；`work/mesa/include/android_stub/vndk/hardware_buffer.h`（include 了前者并补 `IMPLEMENTATION_DEFINED`/`B8G8R8A8_UNORM`） |
| u_gralloc 编译的 include 路径 | `build.ninja` 该 edge 的 `-I../../work/mesa/include/android_stub` |
| "never guess" 规则原文 | `/root/zenithblue/patches/android/013-vendor-mapper-metadata.patch` 头部注释 |
