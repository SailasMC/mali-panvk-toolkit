# 任务 09：真实世界先例与已知解法

- 设备背景：OPPO PHZ110 / MT6989 / Immortalis-G720 / Android 16 / 无 root
- 现象：自编 Mesa PanVK 创建 Android 交换链时 `u_gralloc_get_buffer_basic_info()` 失败 → `VK_ERROR_INVALID_EXTERNAL_HANDLE (-1000072003)`
- 本文只做**外部先例与上游证据**（每条含 URL）；本机源码事实见 `04-mesa-ugralloc.md` / `05-bypass-patch.md`，本文与其交叉验证并补三点新结论。
- 只读操作：本文未修改 `/root/mesa`、`/root/MobileGL`、任何 build 目录或手机。

---

## 摘要（≤14 行）

1. **Mesa 上游没有"Android 无 gralloc 也能跑"的先例。** `u_gralloc` 是 Android WSI 的硬依赖；Turnip/panvk 的 AHB/ANB 导入**全都**走 `vk_android_get_ahb_layout()`/`vk_android_get_anb_layout()` → `u_gralloc_get_buffer_basic_info()`。
2. **upstream panvk 已有 Android 支持**（`panvk_android.c`，Copyright 2025 Google），但**没有**任何绕过 u_gralloc 的路径。
3. **最接近的真实先例是 Turnip + 自编驱动 + 无 root**：MR !43659 作者在 nubia NX809J（Adreno 840）上用 **Updatable Driver APK** 部署自编 Turnip，日志确认"Mesa selected the fallback gralloc"——证明"fallback gralloc + 应用态加载自编 Vulkan 驱动"是**真实可跑通**的配置。
4. **Mali 上"共享交换链图像"是全平台失败、全平台回退 blit 的既有事实**（leegao《WSI Woes on Mali》）：这解释了为什么 Mali 生态的可行解不是"修好 gralloc"，而是"自分配 + blit"。
5. **`-Dandroid-stub=true` 不是（本身）错误**：设计意图见 `src/android_stub/README.md`——stub **不安装**，运行时解析到真库。**但必须确认部署目录里没混进 `libhardware.so` / `libnativewindow.so`**，否则 `AHardwareBuffer_*` 全部是空实现（`getNativeHandle` 返回 NULL）。
6. **Android 16 上不存在 "mapper AIDL"**：`graphics/mapper/` 只有 HIDL `2.0/2.1/3.0/4.0` + **`stable-c`**（mapper5 是稳定 C API）。
7. 客户端取用 mapper5 的**唯一官方姿势**（`libs/ui/Gralloc5.cpp`）：AIDL `IAllocator/getIMapperLibrarySuffix()` → `AServiceManager_openDeclaredPassthroughHal("mapper", suffix, RTLD_LOCAL|RTLD_NOW)` → `dlsym("AIMapper_loadIMapper")`。
8. **`AServiceManager_openDeclaredPassthroughHal` 在 `__ANDROID_VENDOR__` 下被编译成 `return nullptr`**（`libs/binder/IServiceManager.cpp:553`），且需要 VINTF 声明 + 对 `hal_graphics_mapper_service` 的 `find` 权限 + sphal 命名空间 → **应用态自编驱动拿不到**。
9. **"直连 mapper AIDL 是否有人成功过"：公开渠道查无先例。** 唯一在树内做到一半的是本项目自己 `/root/zenithblue/work/mesa` 的 `panvk_v19_*`。上游 issue #11091 明确劝阻直接使用稳定 C 库。
10. **结论（按可靠性排序）**：① 自分配（公开格式 AHB / dma_buf）→ 无需 gralloc；② blit 模式（本地 primary + 外部 secondary）；③ 补丁 `u_gralloc` fallback `-EINVAL`→`-EAGAIN` **叠加** MR !43659 的 LINEAR 推断；④ imapper4/5 仅在 AOSP 内构建可行；⑤ 直连 stable-C mapper = 高风险未验证。
11. **新增可执行的最小改动点（本次发现）**：`u_gralloc_fallback.c` 中 `if (!gr_mod || !gr_mod->lock_ycbcr) return -EINVAL;` 应返回 **`-EAGAIN`**——注释与调用方都要求用 `-EAGAIN` 表达"这是 RGB，请继续走 fourcc 分支"。这一条**恰好**是本机 `IMPLEMENTATION_DEFINED` 失败的直接原因。
12. 但仅此不够：fallback 永远只给 `modifier = DRM_FORMAT_MOD_INVALID`，对 panvk 致命（`pan_mod_get_handler(INVALID)==NULL`）。**必须叠加 !43659 式的 LINEAR 推断**（条件：modifier 无效 + plane offset 0 + `fstat(fd).st_size == row_pitch*height`）。
13. `VK_EXT_headless_surface` 在 Mesa 存在（`wsi_common_headless.c`），但**只能离屏**，无法呈现到 ANativeWindow → 仅可作为"验证 PanVK 渲染正确性"的手段，不是落地方案。
14. 未解/需现场确认：设备是否存在 `/vendor/lib64/hw/mapper.<suffix>.so`、部署目录是否混入 stub `.so`、交换链 `hal_format` 究竟是 34 还是 1/2/5。

---

## 0. 一句话结论

**上游没有"Android 上不依赖 gralloc 的 Vulkan WSI"。** 所有先例分两类：(A) 让 gralloc 查询成功（imapper4/5，需 libui，NDK 不可得）；(B) **根本不查询**——自分配自己知道布局的缓冲（公开 AHB 格式或 dma_buf），必要时用 blit 把本地图像拷进可共享图像。本机条件下 **(B) 是唯一有真实先例支撑的路**。

---

## 1. 与任务 04/05 互证的源码级根因（本次新增 3 点）

### 1.1 新增点 A：`-EINVAL` vs `-EAGAIN` 的语义错位（直接死因）

`src/util/u_gralloc/u_gralloc_fallback.c`：

```c
static int
fallback_gralloc_get_yuv_info(...)
{
   struct fallback_gralloc *gr = ...;
   gralloc_module_t *gr_mod = gr->gralloc_module;
   ...
   if (!gr_mod || !gr_mod->lock_ycbcr) {
      return -EINVAL;          /* <=== 问题在这里 */
   }
   ...
}

static int
fallback_gralloc_get_buffer_info(...)
{
   if (is_hal_format_yuv(hnd->hal_format)) {
      int ret = fallback_gralloc_get_yuv_info(gralloc, hnd, out);
      /* HACK: https://issuetracker.google.com/32077885 ... */
      if (ret != -EAGAIN)
         return ret;           /* <=== -EINVAL 被原样上抛 */
   }
   ...  /* 走 RGB/fourcc 分支 */
}
```

而 `is_hal_format_yuv()` 的判定表（`u_gralloc_internal.c:29-43`）**包含** `HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED`（值 34 / 0x22）：

```c
{HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCbCr, 2, DRM_FORMAT_NV12},
{HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCbCr, 1, DRM_FORMAT_YUV420},
{HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCrCb, 1, DRM_FORMAT_YUV420},
{HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCrCb, 1, DRM_FORMAT_AYUV},
{HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, YCrCb, 1, DRM_FORMAT_XYUV8888},
```

同时 `get_fourcc_from_hal_format()` 又显式把 `IMPLEMENTATION_DEFINED` 映射成 `DRM_FORMAT_XBGR8888`（带 "HACK: Hardcode this to RGBX_8888" 注释）。

**即设计意图是**：对 `IMPLEMENTATION_DEFINED` 先试 `lock_ycbcr`，**失败就该被理解为"它其实是 RGB"**，靠返回 `-EAGAIN` 让调用方继续走 RGB 分支。
**但**当设备根本没有 gralloc0 模块时（`gr_mod == NULL`），代码返回的还是 `-EINVAL` → 被上抛 → `vk_android.c:148` 立即 `return VK_ERROR_INVALID_EXTERNAL_HANDLE`。

> 结论：本机链路的**直接死因**就是这一处 `-EINVAL`。修法是一行：`return -EAGAIN;`（现状下 `gr_mod==NULL` 意味着"无 gralloc0 可问"，与"问到了但不是 YUV"在下游处理上是等价的）。

### 1.2 新增点 B：`android_stub` 的真实语义（**不是**根因，但有部署风险）

`src/android_stub/README.md` 原文：

> The Android NDK doesn't come with enough of the platform libraries we need to build Mesa drivers out of tree, so android_stub has stub versions of those library that **aren't installed** which we link against, **relying on the real libraries to be present when the Mesa driver is deployed**.

`src/android_stub/meson.build` 把这 4 个 stub 建成 **shared_library**（`hardware` / `log` / `nativewindow` / `sync`），`install : false`，并通过 `dep_android` 传给 `u_gralloc`。

**风险点**：`src/android_stub/nativewindow_stub.cpp` 里 `AHardwareBuffer_*` 全是空壳：

| 符号 | stub 行为 | 后果 |
|---|---|---|
| `AHardwareBuffer_describe` | **空函数体**（不写 outDesc） | `desc` 全 0 → stride/format 全错 |
| `AHardwareBuffer_getNativeHandle` | `return NULL` | 直接崩 |
| `AHardwareBuffer_allocate` | `return 0`（不产生 buffer） | 后续用野指针 |
| `ANativeWindow_dequeueBuffer` 等 | 全部 `return 0` | 无缓冲 |

只要部署目录（`libvulkan_panfrost.so` 同目录 / `$ORIGIN` / app lib dir）里**同时存在** `libhardware.so` 或 `libnativewindow.so`，动态链接器就会优先解析到 stub，而不是 `/system/lib64/` 的真库 → `AHardwareBuffer_*` 与 `hw_get_module` 全部失效。

**必做现场检查**（只读）：
```bash
# 部署目录是否混进了 stub
ls -la <dist_dir>/ | grep -E 'libhardware|libnativewindow'
# 驱动实际解析到谁
readelf -d <dist_dir>/libvulkan_panfrost.so | grep -E 'NEEDED|RUNPATH'
# 运行期看映射了哪个 libnativewindow
grep -E 'libnativewindow|libhardware' /proc/<pid>/maps
```
另外注意：`src/android_stub/hardware_stub.cpp` 的
```c
int hw_get_module(const char *id, const struct hw_module_t **module) { return 0; }
```
**成功返回但不写 `*module`** → 若真用到 stub，`u_gralloc_cros_api_create()` 会在 `strcmp(gr->gralloc_module->common.name, ...)` 处 **NULL 解引用 SIGSEGV**（与任务 04 §6 一致）。

### 1.3 新增点 C：`-Dandroid-strict` 与本题无关（避免误入）

在 `/root/mesa` 全树 grep，`android-strict` **只出现在** `src/gfxstream/hermetic/android/aosp_mesa3d.toml:47: android-strict = false`（meson2hermetic/AOSP 生成选项）；`ANDROID_STRICT` 这个 C 宏只用于三处 driver 的 **API level 断言**：

- `src/amd/vulkan/radv_physical_device.c:1814`
- `src/intel/vulkan/anv_api_version.h:14`
- `src/intel/vulkan_hasvk/anv_device.c:114`

**与 gralloc/WSI 无任何关系**，不要把它当成缓解手段。

---

## 2. Mesa 历史问题单 / 提交（u_gralloc + Android gralloc）

### 2.1 Issue（`gitlab.freedesktop.org/mesa/mesa`）

| # | 标题 | 状态 / 标签 | 与本任务的关系 |
|---|---|---|---|
| [!12258](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/12258) | *There is neither mapper4 nor mapper5 function on currently the lastest android14 r75* | open / **Not Our Bug**, android | ★★★ **最直接的先例**：Android 14 r75 上"没有 mapper4 服务且 sdk<35"→ 运行期日志 `MESA: Using fallback gralloc implementation`，报告者明确说 **"but graphics can work and show"**。⇒ **纯 fallback 是可以工作的**，前提是格式能被解析 |
| [!13429](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/13429) | *Mesa doesn't provide android_stub for `android.hardware.graphics.mapper@4.0` and libui* | **open** / android | ★★★ 正是本题构建困境的官方记录：任何想用 **NDK** 构建 IMapper4/IMapper5 的目标都缺 stub。**到今天仍未修** |
| [!11091](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/11091) | *Android: IMapper4 / HIDL is being deprecated* | closed / android | ★★★ 关键引用（见 §8）：稳定 C 库 README **劝阻客户端直接使用**；libui **在 full-treble 设备上对 sphal 命名空间不可用**；建议改用 GraphicBufferMapper 或 **AHardwareBuffer** |
| [!11756](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/11756) | *QCOM Gralloc on android broken* | open / android, turnip | ★★ 证实 AUTO 下会自动落到 fallback："otherwise, it's using the fallback gralloc if we use `U_GRALLOC_TYPE_AUTO`" |
| [!11233](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/11233) | *Performance regression on a730 and lower since ANB/AHB support got merged (up to 40%)* | closed | ★ ANB/AHB 支持曾引入 40% 回退；提醒 AHB 路径有性能代价 |
| [!9691](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/9691) | *turnip: Support AHardwareBuffer* | closed | ★ 说明 Turnip 的 AHB 支持是应"用户想用开源驱动替代厂商驱动"而做的 |
| [!9874](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/9874) | *turnip: Request for change `tu_device_memory` base to `vk_device_memory`* | closed | ★ 同上，通用 AHB 化的前置 |
| [!10416](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/10416) | *util/u_gralloc: move GL specific DRI bits back to egl platform_android* | open | 低 |
| [!12096](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/12096) | *android/nouveau: eglCreateImageKHR fails for YV12 buffers generated by Android's gralloc* | open | ★ 证明"gralloc 格式/布局猜错"是跨厂商通病 |
| [!15690](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/15690) | *turnip: tu_image_init does not report to gralloc if ubwc is dropped* | open | ★ UBWC/modifier 往返问题 |
| [!7807](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/7807) | *hasvk: Incompatible with minigbm/gralloc4 on Android* | closed | ★ 另一种"新 gralloc 与旧假设不兼容" |
| [!16460](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/16460) | *[Turnip/A840] GPU hang ... with D3D12 titles under Winlator* | open / turnip | ★ 作者即在用 **自编 Turnip + fallback gralloc + 无 root**（见 !43659/!44464） |

**关键：搜遍 issue 库，`u_gralloc_get_buffer_basic_info` 失败被单独报为 bug 的条目：0 条。**
（`issues?search=u_gralloc` 共 8 条命中，无一提及该函数；`search="u_gralloc_get_buffer_basic_info"` → 0 条。）
⇒ 这不是一个"已知被反复踩"的上游 bug，而是**上游假设了 gralloc 一定存在**、根本没为"无 gralloc"设计过。

### 2.2 Merge Request / 补丁

| !MR | 标题 | 状态 | 意义 |
|---|---|---|---|
| [!25454](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/25454) | *tu: Support AHardwareBuffer* | **closed** | ★★★ "Tested with u_gralloc **IMapper4** API on Android 13"——**Turnip 的 AHB 支持是在 IMapper4 可用前提下验证的**，从未验证过纯 fallback。AHB 路线的唯一一次尝试被关闭 |
| [!44278](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/44278) | *u_gralloc: Enable building imapper5 backend with meson2hermetic and related fixes* | merged 2026-09-14 | ★★★ 原文："**The imapper5 u_gralloc backend requires Android's libui, which doesn't have a stub in Mesa. As a result, it can't be built with the NDK and is currently only buildable through the legacy Android.mk system**" |
| [!31766](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/31766) | *Android15 support gralloc IMapper5* | merged 2024-10-29 | ★★★ cheyang@bytedance："**in Android15 libui.so the vendor partition can access**"，用 GraphicBufferMapper 先 load mapper5、失败回退 mapper4。关闭 #11091 |
| [!43659](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/43659) | *vulkan/android: infer LINEAR modifier for tightly packed fallback buffers* | **open** | ★★★ **最贴合本机的补丁**，见 §7 方案③ |
| [!44464](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/44464) | *u_gralloc/fallback: detect UBWC on SnapAlloc buffer handles* | open | ★ 同作者；继续在 fallback 后端上做厂商布局探测 |
| [!29260](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/29260) | *u_gralloc/fallback: Extract modifier from QCOM native_handle* | merged | ★ fallback 里从 native_handle 额外字段抽 modifier 的先例 |
| [!29785](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/29785) | *u_gralloc/fallback: Set fd from handle directly* | merged | ★ fallback 正确性修补 |
| [!36277](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/36277) | *u_gralloc/mapper4: properly expose ChromaSiting types based on api level* | merged | 参考 |
| [!37185](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/37185) | *android/gralloc0: add CROS_GRALLOC_DRM_GET_BUFFER_COLOR_INFO* | merged | 参考 |
| [!6045](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/6045) | *egl: android: get BO info using IMapper@4 metadata API* | merged | ★ imapper4 的**起源**补丁（2021） |
| [!41330](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/41330) | *Draft: HACK: u_gralloc: always use ubwc detection path* | open (Draft) | ★ 已有先例在 fallback 里做"绕过"式 hack |
| [!35924](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/35924) | *RFC: vulkan/wsi/android: add common image ops* | closed | ★ Android WSI 公共化尝试未成 |

**"新增后端 / 降级到 AHardwareBuffer / 用 mapper AIDL"是否有上游补丁？逐条回答：**

1. **新增后端**：有，但只有 **mapper5 后端**（!31766，2024-10-29）；**没有**任何"AHB 自描述后端"或"无 gralloc 降级后端"的 MR。
2. **降级到 AHardwareBuffer**：**不存在**。AHB 在 Mesa 里不是"降级手段"，反而是**消费者**——`vk_android_get_ahb_layout()` 自己就要调 u_gralloc（`vk_android.c:620-637`）。`tu: Support AHardwareBuffer`（!25454）被 **closed**。
3. **用 mapper AIDL**：**不存在**，因为 Android 16 的 mapper 根本不是 AIDL（见 §5.1）。存在的是 mapper5 **stable-C** 后端（!31766 / !44278）。

---

## 3. Turnip / Freedreno / PanVK 在 Android 上的 WSI 现状

### 3.1 三个驱动都必须 gralloc —— 逐条证据

**panvk（本机目标）**：upstream **已有** Android 支持，文件 `src/panfrost/vulkan/panvk_android.c`（Copyright 2025 Google LLC），`src/panfrost/vulkan/meson.build:203-204` 仅在 `with_platform_android` 下编入。关键调用链：

```c
/* panvk_android.c */
panvk_android_anb_init()        -> vk_android_get_anb_layout()      /* :121 */
panvk_android_ahb_image_init()  -> vk_android_get_ahb_layout()      /* :222 附近 */
panvk_android_allocate_ahb_memory()                                 /* :360 */
     └─ 非导入情形: ahb = vk_alloc_ahardware_buffer(pAllocateInfo)  /* 自分配 AHB！*/
     └─ 然后仍然 import -> panvk_android_import_ahb_memory()
          └─ panvk_android_ahb_image_init() -> vk_android_get_ahb_layout()
```

⇒ **连 panvk 自己分配的 AHB 也要经过 u_gralloc**。`panvk` 里**没有**任何 `#if`/开关能跳过。`grep -rn "u_gralloc" src/panfrost/vulkan/` 无直接命中，但全部经由 `vk_android_*` 间接依赖。

**turnip**：`!25454 tu: Support AHardwareBuffer` **closed**；AHB 能力建立在 IMapper4 之上（作者原话）。ANB 路径同样经 `vk_android_get_anb_layout`（`src/broadcom/vulkan/v3dv_image.c:437` 是 v3dv 的同构用法）。

**freedreno/turnip 与 gralloc 的耦合**：`!29260`（从 QCOM native_handle 抽 modifier）、`!15690`（UBWC 往返）说明 turnip 需要 modifier 这类**只有 gralloc 才知道**的信息。

### 3.2 dmabuf-only / headless 的实践现状

- **`VK_EXT_headless_surface`：Mesa 有实现**——`src/vulkan/wsi/wsi_common_headless.c`，在 `wsi_common.c:277` `wsi_headless_init_wsi()` 初始化，`meson.build:34` 编入。
  但语义决定它**只能离屏**：headless surface 没有显示后端，present 是 no-op，**没有途径把图像交给 SurfaceFlinger/ANativeWindow**。
  ⇒ 用途只有：**验证 PanVK 在无 gralloc 环境下能否正确渲染**（把 WSI 从等式中消掉做二分）。**不是落地方案。**
  另注：`wsi_common.c:3702-3705` 把 `EXT_headless_surface` 与 `KHR_android_surface` 一起视为 `req_unsupported`，同时开两者有风险。
- **"dmabuf-only WSI"在上游不存在**；但**"dmabuf-only 内存"是真实实践**，见 §6.1 的 `VK_EXT_external_memory_dma_buf` 优先策略。
- **没有任何上游驱动**提供"跳过 u_gralloc 直接假设 LINEAR"的开关。

---

## 4. 关键词命中汇总

| 关键词 | 命中情况 | 结论 |
|---|---|---|
| `u_gralloc_get_buffer_basic_info` | Mesa GitLab issue/MR **搜索 0 命中**（函数名不在任何 issue/MR 标题或正文）；代码中仅 `u_gralloc.c` 定义 + `vk_android.c:148` / `u_gralloc_libdrm.c` 调用 | **上游从未把"该函数失败"当作可讨论的问题**；这不是已知 bug，是设计假设 |
| `VK_ERROR_INVALID_EXTERNAL_HANDLE panvk` | panvk 无专门 issue；相关命中在 v3dv !16462、radv !13163、hasvk !7807/!7630 | 该错误码在本机由 `vk_android.c:148/152/164/218` 四处产生；panvk 无特例处理 |
| `mesa android wsi gralloc.default.so` | 无直接命中；`gralloc.default.so` 是**厂商私有**空壳，上游无记录 | 上游不感知厂商 gralloc 空壳；等价问题被记在 !12258（无 mapper → fallback） |
| `USE_IMAPPER4_METADATA_API` | **Mesa 内部宏**，定义于 `src/util/u_gralloc/meson.build`：当 `dep_android_ui.found()`（libui）或 `dep_android_mapper4.found()` 时加上，并额外编入 `u_gralloc_imapper5_api.cpp` 或 `u_gralloc_imapper4_api.cpp`，强制 `cpp_std=c++17` | 本机因 **libui/mapper4 均 found()==false**（`-Dandroid-stub` 环境）而未定义 ⇒ **imapper4/5 后端根本没参与编译**（与任务 04 §4.1 一致） |
| `libgralloctypes ndk` | `frameworks/native/libs/gralloc/types/Android.bp`：`name: "libgralloctypes"`, `defaults: ["android.hardware.graphics.common-ndk_shared"]`, **`vendor_available: true`, `min_sdk_version: "29"`**，**无 `sdk_version`** | ★ **它不是 NDK 库**。`vendor_available` 只意味着可用 VNDK/vendor 工具链编译，**不提供 NDK 稳定 ABI** ⇒ 用 plain NDK 无法链接。这正是 !13429 的诉求 |
| `mesa -Dandroid-strict` | 仅 `src/gfxstream/hermetic/android/aosp_mesa3d.toml:47`；C 宏 `ANDROID_STRICT` 只在 radv/anv/hasvk 的 API-level 断言 | **与 gralloc/WSI 无关** |
| `android-stub mesa` | `meson.build:1043` `with_android_stub = get_option('android-stub')`；`:1044-1046` 要求 `platforms=android`；`src/android_stub/README.md` 说明"**不安装**，依赖部署时存在真库" | ★ 语义**不是**"无 Android 也能编"，而是"用假库头编译、运行期解析真库"。**有部署污染风险**（§1.2） |
| `AIMapper_loadIMapper` | ★★★ 命中且明确：AOSP `hardware/interfaces/graphics/mapper/stable-c/README.md` 规定 mapper5 的 stable-C 入口；客户端用法见 `frameworks/native/libs/ui/Gralloc5.cpp:48,118` | **mapper5 的官方入口**；gralloc5 就是"stable-C"，**不是 AIDL** |
| `AServiceManager_openDeclaredPassthroughHal` | ★★★ 命中：`frameworks/native/libs/binder/ndk/service_manager.cpp:227`（NDK 包装）+ `frameworks/native/libs/binder/IServiceManager.cpp:553-571`（真正实现） | 见 §5.2；**`__ANDROID_VENDOR__` 下被编译成 `return nullptr`** |

---

## 5. AOSP 侧（以 `android16-release` 分支为准）

### 5.1 Android 16 上 `hardware/interfaces/graphics/mapper` 各版本可用性

实测各分支 `graphics/mapper/` 目录内容（**14 / 15 / 16 / main 完全一致**）：

```
2.0   2.1   3.0   4.0   stable-c
```

- **HIDL**：`2.0 / 2.1 / 3.0 / 4.0`（`4.0/IMapper.hal`）。
- **`stable-c`**：gralloc5 的 mapper，**稳定 C API**（`mapper/stable-c/README.md`、`imapper.map.txt`、`vts/`）。
- **没有 `aidl/` 目录** —— `graphics/mapper/aidl/` 返回 `Object is not found`。
  只有 **allocator** 是 AIDL：`graphics/allocator/aidl`（另有 HIDL 2.0/3.0/4.0）。

`mapper/stable-c/README.md` 原文（关键）：

> Starting with gralloc version 5, IMapper is now exposed as a C API instead of through HIDL or AIDL.
> This is due to HIDL being deprecated, and **AIDL not wanting to support a pass-through mode & pointers for just a couple of clients such as IMapper.**

⇒ **题目中"直连 mapper AIDL"这个说法在 Android 16 上不成立**：mapper 侧没有 AIDL；AIDL 只出现在 **allocator**（用于问出 suffix）。"直连"的正确对象是 **stable-C `mapper.<suffix>.so`**。

stable-c 的部署契约（同 README）：
- 实现放在 **`/vendor/lib[64]/hw/mapper.<imapper_suffix>.so`**；
- VINTF 声明：`<hal format="native"><name>mapper</name><version>5.0</version><interface><instance><suffix></instance>...`；
- SELinux 必须给 **`mapper/<suffix> u:object_r:hal_graphics_mapper_service:s0`**（写在**设备自己的 `service_contexts`**，AOSP 主干里没有这条）；
- 必须导出 `ANDROID_HAL_STABLEC_VERSION`（=`AIMAPPER_VERSION_5`）与 `AIMapper_loadIMapper`。

### 5.2 `AServiceManager_openDeclaredPassthroughHal` 的"正确姿势"与三个陷阱

**AOSP 自己的用法**（`frameworks/native/libs/ui/Gralloc5.cpp`，这是唯一权威范例）：

```cpp
static const auto kIAllocatorServiceName =
        IAllocator::descriptor + std::string("/default");   // android.hardware.graphics.allocator.IAllocator/default

static std::shared_ptr<IAllocator> waitForAllocator() {
    if (!AServiceManager_isDeclared(kIAllocatorServiceName.c_str())) return nullptr;   // ← 陷阱1
    auto allocator = IAllocator::fromBinder(
            ndk::SpAIBinder(AServiceManager_waitForService(kIAllocatorServiceName.c_str())));
    int32_t version = 0; allocator->getInterfaceVersion(&version).isOk();
    if (version < 2) return nullptr;                                                  // 需要 IAllocator v2+
    return allocator;
}

static void *loadIMapperLibrary() {
    auto allocator = waitForAllocator();
    std::string mapperSuffix;
    allocator->getIMapperLibrarySuffix(&mapperSuffix);                                // ← suffix 从这里来
    void* so = nullptr;
    if (__builtin_available(android __ANDROID_API_V__, *)) {                           // API 35 = Android 15
        so = AServiceManager_openDeclaredPassthroughHal("mapper", mapperSuffix.c_str(),
                                                        RTLD_LOCAL | RTLD_NOW);
    } else {
        so = android_load_sphal_library(("mapper." + mapperSuffix + ".so").c_str(),
                                        RTLD_LOCAL | RTLD_NOW);
    }
    return so;
}
// 然后
auto loadIMapper = (AIMapper_loadIMapperFn)dlsym(so, "AIMapper_loadIMapper");
loadIMapper(&mapper);
```

**真正实现**（`frameworks/native/libs/binder/IServiceManager.cpp:553`）：

```cpp
void* openDeclaredPassthroughHal(const String16& interface, const String16& instance, int flag) {
#if defined(__ANDROID__) && !defined(__ANDROID_VENDOR__) && !defined(__ANDROID_RECOVERY__) && \
        !defined(__ANDROID_NATIVE_BRIDGE__)
    sp<IServiceManager> sm = defaultServiceManager();
    String16 name = interface + String16("/") + instance;      // "mapper/<suffix>"
    if (!sm->isDeclared(name)) return nullptr;                 // ← 必须 VINTF 声明过
    String16 libraryName = interface + String16(".") + instance + String16(".so");
    if (auto apex = sm->updatableViaApex(name); apex.has_value())
        return AApexSupport_loadLibrary(..., *apex, flag);
    return android_load_sphal_library(String8(libraryName).c_str(), flag);   // ← dlopen 进 sphal 命名空间
#else
    (void)interface; (void)instance; (void)flag;
    return nullptr;                                            // ← 陷阱2
#endif
}
```

**三个陷阱（逐条对应本机）**：

1. **`isDeclared()` 需要 binder `find` 权限。**
   `mapper/<suffix>` 被标为 `hal_graphics_mapper_service`（vendor HAL 域），**`untrusted_app` 域没有对该 service 的 `find` 权限**（app 只能 `find` `gpu_service`/`media_*`/`default_android_hwservice` 之类）。
   *AOSP 侧实测*：`system/sepolicy/private/service_contexts` 只有
   `android.hardware.graphics.allocator.IAllocator/default  u:object_r:hal_graphics_allocator_service:s0`；
   **`mapper/<suffix>` 不在 AOSP 主干任何 `service_contexts` 里**（由设备自行添加，见 stable-c README）。
   ⇒ 从 app 进程（或加载进 app 的自编驱动）走这条路，**第一步 `isDeclared` 就会失败**。
2. **`__ANDROID_VENDOR__` 下函数被编译成 `return nullptr`。**
   ⇒ 如果你的驱动是按 **vendor 模块**（Android.bp `vendor: true`）编的，调用它**永远静默返回 nullptr**，不会有任何错误信息。这是一个极隐蔽的坑（Mesa 走 Android.mk 时正是 vendor 侧）。
3. **`android_load_sphal_library()` 需要 sphal 命名空间访问权。**
   app 的 linker namespace（`classloader-namespace`）只包含 `/system/lib64` + APEX + 该 app lib 目录 + `public.libraries.txt` 里的库；**`/vendor/lib64/hw/mapper.<suffix>.so` 不在其中**，除非该库被列进 `vendor_public_libraries`。
   这正是 issue [#11091](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/11091) 那句话的机制来源：*"GraphicBufferMapper (part of libui, **which is not available to sphal namespace on a full-treble device**)"*。
   而 MR [!31766](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/31766) 说 *"in Android15 libui.so the vendor partition can access"* —— 说的是**vendor 分区里的进程**（AOSP 内构建的 vendor 模块），**不是 app**。

**`libui` / `libgralloctypes` 的可链接性（`frameworks/native/libs/ui/Android.bp`）**：
- `libui`：`vendor_available: true`、`double_loadable: true`、`min_sdk_version: "29"`、vendor 变体带 `-DLIBUI_IN_VNDK`；shared_libs 含 `android.hardware.graphics.mapper@4.0`、`libgralloctypes`、`libbinder_ndk`、`libvndksupport`；header_libs 含 `libimapper_stablec`、`libimapper_providerutils`。
- **二者都没有 `sdk_version`** ⇒ **plain NDK 无法链接**，只能在 AOSP 树内（Android.bp / meson2hermetic）用。
- `libgralloctypes`：`vendor_available: true`、`min_sdk_version: "29"`，同样非 NDK。

**mapper4（HIDL）的客户端姿势**（`libs/ui/Gralloc4.cpp:157-171`）：

```cpp
void Gralloc4Mapper::preload() { android::hardware::preloadPassthroughService<IMapper>(); }
Gralloc4Mapper::Gralloc4Mapper() {
    mMapper = IMapper::getService();
    if (mMapper == nullptr) { ALOGI("mapper 4.x is not supported"); return; }
    if (mMapper->isRemote()) LOG_ALWAYS_FATAL("gralloc-mapper must be in passthrough mode");
}
```

⇒ mapper4 **必须是 passthrough**，binder 化的（remote）会**直接 FATAL**。同时 `preloadPassthroughService` 需要 `libhidlbase` + sphal 命名空间（`libvndksupport`）。
⇒ 对 MTK 的检查点：`/vendor/lib64/hw/android.hardware.graphics.mapper@4.0-impl-<suffix>.so` 是否存在（有 **`-impl-`** 的才是 passthrough 实现）；只有 `/vendor/lib64/android.hardware.graphics.mapper@4.0.so`（接口库）**不够**。

---

## 6. 真实世界先例（★★★ 本节最重要）

### 6.1 leegao《WSI Woes on Mali》——Mali 上 WSI 的一手调查报告

URL: <https://leegao.github.io/winlator-internals/wrapper/2026/07/28/wsi-woes-mali.html>
配套项目：<https://github.com/leegao/bionic-vulkan-wrapper>（"Winlator Bionic/CMOD compatible (DX) Vk-on-Vk wrapper for **Mali, old Adreno drivers, and Xclipse**"）

**核心事实（原文引用）：**

> **On all Mali devices, the wrapper WSI implementation (the Mesa Vulkan WSI runtime with xMeM's patches for Android Hardware Buffers) fails to initialize an externally sharable swapchain image, causing every device to fallback to the slower blit mode using a local primary and shared external secondary buffer.**

> This would have failed anyways, as **importing BGRA AHBs is disallowed on the Mali driver**. Instead, we fall back to the blit path.

> To make blitting work for mobile drivers where certain image formats (like BGRA) cannot be exported, **xMeM came up with an ingenious hack. Just lie to the driver and say our image handle ... is always in R8B8G8A8 mode instead, even if physically, the data contained in these buffers are still in BGRA_8888 order.**

**内存分配的优先级（原文）** —— 这是"已验证可行做法"的清单：

> It first tries to use the generic **`VK_EXT_external_memory_dma_buf` / `VK_KHR_external_memory_fd`** path, which is **generically supported on Mali since r32p1**.
> ... Another caveat: on GKI 4.X, **dmabuf-heap is not available**, so this mechanism will fallback to using **ion-heap** ...
> Finally, it will try to use **AHB, which is generally supported on Android since almost forever (predates Vulkan) and is the most robust/well supported shared external memory API.**

**一个重要 erratum（原文）**：

> if `VkMemoryAllocateInfo` contains a pNext `VkMemoryDedicatedAllocateInfo` (`VK_KHR_dedicated_allocation`), then this [dma_buf] path **will fail** unless the image/buffer was created ... ⇒ on certain Mali driver versions, dedicated allocation is **mutually exclusive** with external memory allocation.

**对本任务的意义（为什么这是最重要的先例）**：
1. 有人**真的**在 Mali（就是你这类硬件）上跑 Mesa 的 Vulkan WSI 到 Android，并把它做成了开源项目。
2. 结论是**"共享交换链图像"在 Mali 上普遍失败**，生态里**公认的可行解是 blit 回退**（本地 primary + 外部 secondary）。这与你遇到的"无法创建 externally sharable 交换链图像"是同一类墙。
3. 关键前提：**缓冲区是他们自己分配的**（原文 `wrapper_allocate_memory_ahardware_buffer(...)` 自建 AHB），所以**布局是已知的**——不需要向 gralloc 询问布局。这正是绕开 `u_gralloc` 的本质。
4. 存在 **dmabuf 优先**的真实实践（`VK_EXT_external_memory_dma_buf`），对 Mali 自 r32p1 起通用。

### 6.2 mapmapbear / ValentinLiu —— Turnip + fallback gralloc + 无 root 自编驱动（最贴近本机形态）

- MR [!43659](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/43659)（open）+ [!44464](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/44464)（open）
- 设备：**nubia NX809J / Adreno 840 / Android 16 kernel**（issue !16460 给出 `6.12.23-android16-...`）
- 原文关键句：

> **The device log also confirms that Mesa selected the fallback gralloc.**
> Android's fallback gralloc reports `DRM_FORMAT_MOD_INVALID` when it cannot query vendor layout metadata. On a nubia NX809J with an Adreno 840, this causes Turnip to interpret a tightly packed RGBA8 Android native buffer as TILE6_3 with UBWC. The resulting layout does not match the allocation and produces corrupted output.
> ... **Final scoped binary installed through an Android Updatable Driver APK** after setting its required `DT_SONAME` to `vulkan.adreno.so`. ... no `updatable_driver_all_apps` override was used.

⇒ **这是"应用态部署自编 Mesa Vulkan 驱动 + fallback gralloc + 无 root"的实证**（不是推测）。它同时证明：
- fallback 后端在真机上**可以走到 get_buffer_basic_info 成功**（前提是格式是公开 RGB 格式，不是 `IMPLEMENTATION_DEFINED`）；
- 成功后唯一的缺陷是 **`modifier = DRM_FORMAT_MOD_INVALID`**，而他的修法就是**推断 LINEAR**（恰好是 §7 方案③）；
- **无 root 部署路线存在**：Updatable Driver APK + 设置 `DT_SONAME`（Android 的可更新驱动机制）。

### 6.3 PanVK on Mali stock Android（先例，但证据薄弱，需谨慎）

仓库：<https://github.com/martuniykmisha012-rgb/PanVK-Mali-Android-Stubs>
（"Experimental Mesa 26.2 (PanVK) Vulkan builds for ARM Mali. Features custom UMD stubs from Turnip to bypass Android `/dev/dri` initialization blocks"）

README 自述要点：
- 目标正是 **PanVK + Mali (Valhall G720) + stock Android 无 root**；
- 手法：把 **Turnip 的 Android 兼容 stub 移植进 PanVK 驱动结构**，绕过 `/dev/dri` 缺失导致的初始化失败（`vkEnumeratePhysicalDevices` 返回 0）；
- 战果：GL/Vulkan 游戏可跑、Mali-G720 合成测试 ~917 FPS；**VKD3D-Proton 卡在"final Swapchain presentation frame-delivery block"**。

**可信度警告（必须标注）**：仓库 `size: 3`（KB）、`language: null`、无源码，只有 README + Release 二进制；20 stars；创建于 2026-07-22。**极可能是 AI 生成的宣传性 README / 未经验证的声明**，不建议作为技术依据。

**但它仍有一个信息价值**：它自述的**失败点恰好也是交换链/呈现阶段**——与本机撞的是同一堵墙，侧面印证"PanVK 在 stock Android 上，交换链是公认终点"。

### 6.4 其他相关先例

- <https://github.com/ar37-rs/mesa-termux>（"Upstream Mesa build for Termux with some fixes"）——Termux/无 root 场景的 Mesa 构建。
- <https://github.com/27hectormanuel/Wine-Hangover-Patched-Android16LinuxTerminal>——Android 16 上的 Wine 栈。
- XDC 2025 PanVK 演讲：<https://indico.freedesktop.org/event/10/contributions/413/attachments/280/370/xdc2025-panfrost.pdf>
- `The412Banner/Banners-Turnip`：<https://github.com/The412Banner/Banners-Turnip>（"Automated bleeding-edge Mesa Turnip Vulkan driver builds for Qualcomm Adreno GPUs — packaged for **AdrenoTools**"）——AdrenoTools 是 Adreno 上应用态加载自编 Vulkan 驱动的成熟机制（Mali 无等价物，只能走 Updatable Driver APK）。

---

## 7. 结论：已被验证可行的做法清单（按可靠性 / 改动量排序）

### ✅ 方案① 不要把外部缓冲当渲染目标：自分配 + blit（**最高可靠性，已被 Mali 生态验证**）

- **依据**：§6.1《WSI Woes on Mali》——"on **all** Mali devices ... **every** device to fallback to the slower blit mode"。
- **做法**：渲染到**本地（非外部）图像**；另建一个**自己 `AHardwareBuffer_allocate()` 的、公开格式**（`AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM`=1 / `R8G8B8X8`=2 / `B8G8R8A8`=5）的外部图像；每帧 `vkCmdCopyImage` 一次。
- **为什么能绕开 u_gralloc**：公开 AHB 格式值**恰好等于** `HAL_PIXEL_FORMAT_RGBA_8888(1)/RGBX_8888(2)/BGRA_8888(5)`，所以**即使 FALLBACK 后端**也能命中 `get_fourcc_from_hal_format` + `get_hal_format_bpp` 的 RGB 分支而**成功返回**（不进入 `is_hal_format_yuv` 分支）。且因为是你自己分配的，你可以**确定它是 LINEAR**。
- **代价**：每帧一次全屏拷贝。
- **上游佐证**：`u_gralloc_internal.c` 的 bpp/fourcc 表对这些值都有定义（`RGBA_8888/RGBX_8888/BGRA_8888/IMPLEMENTATION_DEFINED → bpp=4`；`RGBA_8888→DRM_FORMAT_ABGR8888`、`RGBX_8888/IMPLEMENTATION_DEFINED→DRM_FORMAT_XBGR8888`）。

### ✅ 方案② dma_buf 优先（**有真实先例，改动中等**）

- **依据**：§6.1 的分配优先级 `VK_EXT_external_memory_dma_buf` / `VK_KHR_external_memory_fd` → dmabuf-heap/ion → AHB；Mali "generically supported since r32p1"。
- **做法**：自己申请 dma_buf（或 AHB 后取 `data[0]`），用 `VK_EXT_external_memory_dma_buf` 导入；**完全不需要 gralloc 元数据**（布局自定 LINEAR）。
- **注意**：避免 `VkMemoryDedicatedAllocateInfo` 与外部内存分配同时使用（§6.1 的 erratum）。

### ✅ 方案③ 补丁 `u_gralloc` fallback（**改动最小，需两处叠加**）

- **③a（本次新增发现，1 行）**：`src/util/u_gralloc/u_gralloc_fallback.c` 中
  `if (!gr_mod || !gr_mod->lock_ycbcr) return -EINVAL;` → **`return -EAGAIN;`**
  理由：调用方注释与 `-EAGAIN` 语义都明确表示"IMPLEMENTATION_DEFINED 其实是 RGBX，请继续走 fourcc 分支"。这是本机 `IMPLEMENTATION_DEFINED` 失败的**直接**原因。
- **③b（必须叠加）**：移植 MR [!43659](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/43659) 的 LINEAR 推断——当 `modifier == DRM_FORMAT_MOD_INVALID`、`offset == 0`、`fstat(fd).st_size == row_pitch * height` 时推断 `DRM_FORMAT_MOD_LINEAR`。
  **必须做**，因为 panvk 对 `MOD_INVALID` 无 handler（任务 04 §0.3：`pan_mod_get_handler(INVALID) == NULL` ⇒ NULL 解引用）。
- **上游接受度**：`!43659` 目前 **open**（未合并），`!44464` 也在 fallback 上做厂商探测 —— 说明"在 fallback 里补充布局推断"是**上游正在讨论的方向**，不是离经叛道。
- **风险**：`IMPLEMENTATION_DEFINED` 对真实 YUV 缓冲会误判为 RGB（但你只用于交换链，交换链必然可被 GPU 渲染 ⇒ 合理）。

### ⚠️ 方案④ imapper4/5 后端（上游正解，但本机构建条件不具备）

- **依据**：MR [!31766](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/31766)（merged）+ [!44278](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/44278)（merged）+ issue [!13429](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/13429)（open）。
- **硬约束**：需要 `libui`；**libui 没有 stub、plain NDK 不可链接**（!44278 原话），只能走 legacy `Android.mk` 或 meson2hermetic/Soong。
- **若你控制 AOSP 构建**：这是最正统的路。**若只是 NDK 编 .so**：需要自己补 libui/libgralloctypes 的桩与头（= 自己做 !13429 请求的东西），代价高。
- **MTK 现场前置条件**（只读检查）：
  - `/vendor/lib64/hw/mapper.<suffix>.so` 是否存在（stable-c，gralloc5）
  - `/vendor/lib64/hw/android.hardware.graphics.mapper@4.0-impl-<suffix>.so` 是否存在（HIDL passthrough impl）
  - AIDL allocator：`android.hardware.graphics.allocator.IAllocator/default` 是否 `isDeclared`

### ❌ 方案⑤ 直连 stable-C mapper / mapper AIDL（**高风险，公开渠道无成功先例**）

见 §8。**不推荐作为主路**。

### ❌ 方案⑥ `VK_EXT_headless_surface`（不是落地方案）

Mesa 有实现（`wsi_common_headless.c`），但只能离屏、无法呈现到 ANativeWindow。
**推荐用途**：作为**诊断工具**——用它跑通 PanVK 的渲染，把"gralloc/WSI 问题"与"panvk 布局/编译问题"分离。

---

## 8. 直连 mapper AIDL / stable-C：是否有人在 Android 上成功过？

### 8.1 结论

**没有找到任何公开的、在非 system/vendor 上下文（应用态自编驱动）中成功直连 mapper 的案例。**

具体地：
- 上游 Mesa 的 mapper5 后端（!31766）用的是 **`libui` 的 `GraphicBufferMapper`**，**不是**"直连 AIDL"。它是在 **AOSP 树内构建**（Android.mk / Soong），运行于 vendor 上下文。
- Mesa GitLab issue/MR 全文检索：无任何"直接 `dlopen("mapper.*.so")` + `AIMapper_loadIMapper`"的实现或成功报告。
- 唯一在树内做到"运行期 `dlopen` + `dlsym("AIMapper_loadIMapper")`"的，是**本项目自己**的工作树 `/root/zenithblue/work/mesa` 的 `u_gralloc_fallback.c` 里寄居的 `panvk_v19_*`（约 +275 行）——**属自研、尚未验证**（见任务 04 §0.5）。

### 8.2 上游为什么劝阻 + 三道硬门

issue [!11091](https://gitlab.freedesktop.org/mesa/mesa/-/work_items/11091) 原文：

> For the record, the README for the stable C library states that **clients should not use it directly** (:|). Clients should use **GraphicBufferMapper** (part of libui, **which is not available to sphal namespace on a full-treble device**) or **AHardwareBuffer**.

`mapper/stable-c/README.md` 亦规定实现方在 `/vendor/lib64/hw/`，客户端应经 ServiceManager `find`，并需设备侧 `service_contexts` 标注 `mapper/<suffix> u:object_r:hal_graphics_mapper_service:s0`。

⇒ 从应用态自编驱动直连，必须同时越过：

| 门 | 内容 | 应用态是否可越 |
|---|---|---|
| 1 | `AServiceManager_isDeclared("android.hardware.graphics.allocator.IAllocator/default")` —— 需对 `hal_graphics_allocator_service` 的 binder `find` 权限 | **否**（`untrusted_app` 无此授权） |
| 2 | `AServiceManager_openDeclaredPassthroughHal("mapper", suffix, ...)` —— 需 VINTF 声明 + `isDeclared` + `android_load_sphal_library`（sphal 命名空间） | **否**（app linker namespace 不含 `/vendor/lib64/hw`）；且若按 **vendor** 编译，该函数被 `#if !__ANDROID_VENDOR__` 编成 `return nullptr` |
| 3 | 拿 suffix 需先连上 AIDL `IAllocator`（同上第 1 门） | **否** |

**可行的绕法（也都要额外条件）**：
- 若能在**vendor 上下文**运行（例如随 ROM 或经 Magisk 类机制放到 `/vendor`），第 1/2 门可能通过（MR !31766 说的 "in Android15 libui.so the vendor partition can access"）——但本机**无 root**，不可行。
- 若把 `mapper.<suffix>.so` 加入 `vendor_public_libraries` 使 app 可 dlopen——需要改系统配置，**无 root 不可行**。

---

## 9. 未解问题 + 建议的现场验证清单（全部只读）

1. **交换链缓冲的 `hal_format` 到底是什么？**
   这是决定性事实。在 `vk_android.c` 的 `vk_gralloc_to_drm_explicit_layout()` 入口加一行日志打印
   `in_hnd->hal_format` / `in_hnd->pixel_stride` / `in_hnd->handle->numFds`。
   - 若 `== 34`（`IMPLEMENTATION_DEFINED`）→ 证实 §1.1 的死因，方案③a 直接命中。
   - 若 `== 1/2/5` → fallback 本应成功，问题在别处（`numFds==0` / `pixel_stride==0` / `get_fourcc==-1`）。
2. **部署目录是否混入 stub 库？**（§1.2）`ls` dist 目录查 `libhardware.so` / `libnativewindow.so`；`readelf -d` 查 NEEDED/RUNPATH；`/proc/<pid>/maps` 查实际解析。
3. **`u_gralloc_get_type()` 是哪个后端？** 每个成功 create 都有一句 `mesa_logi`（"Using gralloc0 CrOS API" / "Using fallback gralloc implementation" / libdrm 的 logw）。也可直接调 `u_gralloc_get_type()`。
4. **设备上 mapper 的实际形态**（只读）：
   - `/vendor/lib64/hw/mapper.*.so`（stable-c / gralloc5）
   - `/vendor/lib64/hw/android.hardware.graphics.mapper@4.0-impl-*.so`（HIDL passthrough）
   - `/vendor/lib64/android.hardware.graphics.mapper@4.0.so`（**接口库，存在≠passthrough 实现存在**）
   - `libgralloctypes.so` 所在位置（`/system/lib64` 还是 `/vendor/lib64`）→ 决定能否被 app 命名空间解析
5. **该设备 mapper 是 passthrough 还是 binderized？** 若是 binderized，AOSP 的 `Gralloc4Mapper` 会 `LOG_ALWAYS_FATAL("gralloc-mapper must be in passthrough mode")`（§5.2）。
6. **`VK_EXT_headless_surface` 二分**：用它验证 PanVK 渲染本身是否正常（§7 方案⑥），把 WSI/gralloc 从等式中剔除。
7. **未解**：MediaTek MT6989 的 stable-c mapper suffix 具体是什么（`mediatek`? `mtk_common`?）——只能现场从 `IAllocator::getIMapperLibrarySuffix()` 或 VINTF manifest 读。

---

## 附录 A：URL 全表

### Mesa GitLab（issue / MR）
- https://gitlab.freedesktop.org/mesa/mesa/-/work_items/12258 — 无 mapper4/5 → fallback，**graphics 可正常工作**
- https://gitlab.freedesktop.org/mesa/mesa/-/work_items/13429 — **open**：android_stub 缺 mapper@4.0 与 libui
- https://gitlab.freedesktop.org/mesa/mesa/-/work_items/11091 — IMapper4/HIDL 弃用；**劝阻直接使用稳定 C 库**；libui 对 sphal 不可用
- https://gitlab.freedesktop.org/mesa/mesa/-/work_items/11756 — QCOM Gralloc on android broken（AUTO→fallback）
- https://gitlab.freedesktop.org/mesa/mesa/-/work_items/11233 — ANB/AHB 支持引入性能回退
- https://gitlab.freedesktop.org/mesa/mesa/-/work_items/9691 — turnip: Support AHardwareBuffer
- https://gitlab.freedesktop.org/mesa/mesa/-/work_items/9874 — turnip: vk_device_memory 化（AHB 前置）
- https://gitlab.freedesktop.org/mesa/mesa/-/work_items/15690 — turnip: UBWC/modifier 往返
- https://gitlab.freedesktop.org/mesa/mesa/-/work_items/12096 — nouveau: gralloc YV12 eglCreateImageKHR 失败
- https://gitlab.freedesktop.org/mesa/mesa/-/work_items/7807 — hasvk 与 minigbm/gralloc4 不兼容
- https://gitlab.freedesktop.org/mesa/mesa/-/work_items/10416 — u_gralloc: move GL specific DRI bits
- https://gitlab.freedesktop.org/mesa/mesa/-/work_items/16460 — **Turnip/A840 + fallback gralloc + 自编驱动（无 root）** 实证
- https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/25454 — tu: Support AHardwareBuffer（**closed**，IMapper4 前提）
- https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/31766 — **Android15 support gralloc IMapper5**（merged）
- https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/44278 — imapper5 需 libui、**NDK 不可构建**（merged）
- https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/43659 — **infer LINEAR modifier for fallback buffers**（open，最贴合）
- https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/44464 — fallback: detect UBWC on SnapAlloc handles（open）
- https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/41330 — Draft HACK: always use ubwc detection path
- https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/29260 — fallback: Extract modifier from QCOM native_handle
- https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/29785 — fallback: Set fd from handle directly
- https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/6045 — egl: android: IMapper@4 metadata API（imapper4 起源）
- https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/36277 — u_gralloc/mapper4: ChromaSiting
- https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/37185 — android/gralloc0: CROS_GRALLOC_DRM_GET_BUFFER_COLOR_INFO
- https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/35924 — RFC: vulkan/wsi/android common image ops（closed）
- https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/30353 — u_gralloc: include dep_android_mapper4 as needed

### AOSP（android16-release，除注明外）
- https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/android16-release/graphics/mapper/ — `2.0, 2.1, 3.0, 4.0, stable-c`（14/15/16/main 一致）
- https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/android16-release/graphics/mapper/stable-c/README.md — **mapper5 = stable-C**；`AIMapper_loadIMapper`；`/vendor/lib[64]/hw/mapper.<suffix>.so`；`hal_graphics_mapper_service`
- https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/graphics/mapper/stable-c/README.md — 同上（main）
- https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android16-release/libs/ui/Gralloc5.cpp — **客户端直连范例**
- https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android16-release/libs/ui/Gralloc4.cpp — `preloadPassthroughService` / `LOG_ALWAYS_FATAL("gralloc-mapper must be in passthrough mode")`
- https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android16-release/libs/binder/IServiceManager.cpp — `openDeclaredPassthroughHal` 实现（`__ANDROID_VENDOR__` → `return nullptr`）
- https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android16-release/libs/binder/ndk/service_manager.cpp — NDK 包装
- https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android16-release/libs/ui/Android.bp — `libui` `vendor_available`、`min_sdk_version: "29"`、无 `sdk_version`
- https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android16-release/libs/gralloc/types/Android.bp — `libgralloctypes` `vendor_available`、非 NDK
- https://android.googlesource.com/platform/system/sepolicy/+/refs/heads/android16-release/private/service_contexts — `allocator.IAllocator/default → hal_graphics_allocator_service`（**无 mapper 条目**）
- https://android.googlesource.com/platform/frameworks/native/+/refs/heads/android16-release/libs/nativewindow/AHardwareBuffer.cpp
- https://issuetracker.google.com/32077885 — Mesa 注释反复引用的 IMPLEMENTATION_DEFINED 无 API 查询问题

### 第三方项目 / 文章
- https://leegao.github.io/winlator-internals/wrapper/2026/07/28/wsi-woes-mali.html — **《WSI Woes on Mali》**
- https://github.com/leegao/bionic-vulkan-wrapper — Winlator Bionic Vk-on-Vk wrapper（Mali/Adreno/Xclipse）
- https://github.com/martuniykmisha012-rgb/PanVK-Mali-Android-Stubs — PanVK on Mali stock（**证据薄弱，勿作依据**）
- https://github.com/The412Banner/Banners-Turnip — AdrenoTools 打包的 Turnip 自编驱动
- https://github.com/ar37-rs/mesa-termux — Upstream Mesa for Termux
- https://indico.freedesktop.org/event/10/contributions/413/attachments/280/370/xdc2025-panfrost.pdf — XDC 2025 PanVK
- https://github.com/27hectormanuel/Wine-Hangover-Patched-Android16LinuxTerminal

### 本机（只读交叉引用）
- `/root/mesa/src/util/u_gralloc/u_gralloc.c`（AUTO 顺序 `CROS→IMAPPER4/5→LIBDRM→QCOM→FALLBACK`）
- `/root/mesa/src/util/u_gralloc/u_gralloc_fallback.c`（**§1.1 的 `-EINVAL`**）
- `/root/mesa/src/util/u_gralloc/u_gralloc_internal.c`（`is_hal_format_yuv` 含 `IMPLEMENTATION_DEFINED`）
- `/root/mesa/src/util/u_gralloc/meson.build`（`USE_IMAPPER4_METADATA_API`、依赖 `dep_android_ui`/`dep_android_mapper4`）
- `/root/mesa/src/android_stub/README.md` + `nativewindow_stub.cpp` + `hardware_stub.cpp`（§1.2）
- `/root/mesa/src/vulkan/runtime/vk_android.c`（`:148` 等四处 `VK_ERROR_INVALID_EXTERNAL_HANDLE`；`:620` AHB layout；`:820` `vk_alloc_ahardware_buffer`）
- `/root/mesa/src/panfrost/vulkan/panvk_android.c`（upstream panvk Android 支持，全部经 u_gralloc）
- `/root/mesa/src/vulkan/wsi/wsi_common_headless.c`（`VK_EXT_headless_surface`）
- `/root/research/04-mesa-ugralloc.md`、`/root/research/05-bypass-patch.md`（本仓已有分析，本文与其互证）
