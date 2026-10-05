# 任务 10：验证方案与独立探针设计（panvk WSI / u_gralloc）

**目标设备**：OPPO PHZ110（MT6989 / Immortalis-G720 / Android 16 / 无 root）
**被测产物**：`/root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so`
（md5 `4417b369591fc2b3df27e22019ccf3a2`，与 `/root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so` 逐字节相同，本次实测）
**本轮新增（本报告的可交付部分）**：
`/root/research/probe10/panvk_wsi_probe.c`（1394 行，**已在本服务器用 NDK r27c 编译通过**）、
`/root/research/probe10/build.sh`、`tri.vert`、`tri.frag`；本文件含完整源码草稿。
**未修改**：`/root/mesa`、`/root/MobileGL`、任何 build 目录、手机（本轮对手机只做了**只读**读取，见 §1.4）。

---

## 1. 本轮实测事实（每条都可复现）

### 1.1 产物与构建配置（决定后面一切）

| 事实 | 证据（命令 / 文件:行） |
|---|---|
| 产物 20,003,136 B，md5 `4417b369…` | `md5sum build/android-v4/…/libvulkan_panfrost.so dist/android-g720-v12-csf/libvulkan_panfrost.so`（两个值相同） |
| `NEEDED` 只有 `liblog/libnativewindow/libsync/libm/libz/libdl/libc`；`RUNPATH=$ORIGIN/../../android_stub` | `readelf -d` |
| **只导出 3 个符号**：`vk_icdGetInstanceProcAddr` / `vk_icdGetPhysicalDeviceProcAddr` / `vk_icdNegotiateLoaderICDInterfaceVersion` | `readelf --dyn-syms -W`（共 244 条 dynsym，其余全 UND） |
| 构建选项 `-Dplatforms=android -Dandroid-stub=true -Dandroid-strict=false -Dvulkan-drivers=panfrost -Dpanfrost-kmds=kbase -DANDROID_API_LEVEL=35` | `build/android-v4/meson-logs/meson-log.txt:3`、`compile_commands.json` |
| `MESA_LOG_TAG` 默认 `"MESA"`；`MESA_DEBUG=0` ⇒ `MESA_DEFAULT_LOG_LEVEL = MESA_LOG_INFO` | `work/mesa/src/util/log.h:36-37,48-53` |
| 因此 `mesa_logi/w/e` **默认全部进 logcat（tag `MESA`）** | `util/log.c:114-124`（Android 默认开 `MESA_LOG_CONTROL_ANDROID`）、`util/log.c:377-400`（`__android_log_write`） |
| `.so` 里确有本 fork 的探针字符串 | `strings -a …so \| grep -Fc 'P0A-V19-FULLPLANE'` = 8；`'Using fallback gralloc implementation'`/`'No gralloc hwmodule detected'`/`'Failed to get u_gralloc_buffer_basic_info'`/`'u_gralloc_get_buffer_basic_info failed'` 各 1 |
| 驱动**声明**支持 `VK_EXT_headless_surface` | `panvk_instance.c:140` `.EXT_headless_surface = true`；`.so` 内含 `VK_EXT_headless_surface`/`vkCreateHeadlessSurfaceEXT`/`wsi_common_headless.c` 字符串；`wsi_common.c:276` 无条件 `wsi_headless_init_wsi()` |

> 直接结论：**探针不能链接 ICD**（只导出 3 个符号），必须 `dlopen` + `vk_icdGetInstanceProcAddr`。
> 已确认 mesa 对 `instance==NULL` 会返回全局入口（`vkCreateInstance`/`vkEnumerateInstanceExtensionProperties`/`vkEnumerateInstanceVersion`/`vkGetInstanceProcAddr`）：`work/mesa/src/vulkan/runtime/vk_instance.c:300-325`。
> 这是本仓 `tests/vulkan-smoke/enumerate.c` 已有的约定，本探针沿用。

### 1.2 u_gralloc 后端：**这个 .so 里只有 4 个后端，imapper4/5 压根没编**

* `u_gralloc/meson.build:11-30`：`u_gralloc_imapper5_api.cpp`（`dep_android_ui`）与 `u_gralloc_imapper4_api.cpp`（`dep_android_mapper4`）**只在能探测到平台库时才加入源码**；`android-stub=true` 时二者都探测不到。
* 实测编译产物印证：`build/android-v4/src/util/u_gralloc/lib_mesa_u_gralloc.a.p/` 里只有
  `u_gralloc.c.o / u_gralloc_cros_api.c.o / u_gralloc_fallback.c.o / u_gralloc_internal.c.o / u_gralloc_libdrm.c.o / u_gralloc_qcom.c.o`
  ——**没有** `u_gralloc_imapper4_api.cpp.o` / `u_gralloc_imapper5_api.cpp.o`。
* 运行期 `u_gralloc_create(U_GRALLOC_TYPE_AUTO)` 的顺序（`u_gralloc.c:25-34`）：
  **CROS → [GRALLOC4 未编译，跳过] → LIBDRM → QCOM → FALLBACK**。
* 前三个 `create()` 的第一步都是 `hw_get_module(GRALLOC_HARDWARE_MODULE_ID, …)`，而本构建链的是桩
  `work/mesa/src/android_stub/hardware_stub.cpp:6-11`：
  ```c
  int hw_get_module(const char *id, const struct hw_module_t **module) {
     if (module) *module = nullptr;
     return -ENOENT;                      /* 恒失败 */
  }
  ```
  ⇒ CROS/LIBDRM/QCOM **必然返回 NULL**，**唯一能返回非 NULL 的是 FALLBACK**
  （`u_gralloc_fallback.c:451-473`，无条件 `return &gr->base`，且 `gralloc_module == NULL`）。
* 本 fork 把 FALLBACK 改写成了「binder-NDK 直取 AIDL IMapper V5」：
  `u_gralloc_fallback.c:72-131`（三条加载路由）、`:277-...`（`panvk_v19_query_mapper`）、
  `:367-448`（`fallback_gralloc_get_buffer_info`，末尾 `:427-431`）：
  ```c
  int stable_ret = panvk_v19_query_mapper(hnd->handle, out);
  if (stable_ret != 0) {
     mesa_logw("[P0A-V19-FULLPLANE] complete metadata unavailable rc=%d; refusing guessed layout", stable_ret);
     return stable_ret;      /* 即使 fourcc/stride 已经猜出来了也不再返回 */
  }
  ```

### 1.3 手机实测（**只读**，uid 2000/shell，经 Shizuku；未改任何设置、未装任何东西）

| 事实 | 原始结果 |
|---|---|
| 就是目标机 | `ro.product.model=PHZ110`、`ro.board.platform=mt6989`、`ro.hardware=mt6989`、`ro.build.version.sdk=36`、`getenforce=Enforcing`、`context=u:r:shell:s0` |
| **`/dev/mali0` 世界可读写** → 无 root 就能建 instance/device | `crw-rw-rw- 1 system graphics u:object_r:gpu_device:s0 10,97 /dev/mali0`（kbase 探针路径 `/dev/mali%d`，`panvk_instance.c:203`） |
| mapper 实现库在 | `/vendor/lib64/hw/mapper.mediatek.so`（→ `mt6989/mapper.mediatek.so`，154,920 B，`u:object_r:same_process_hal_file:s0`） |
| **VINTF 里接口名就是 `mapper`、实例名就是 `mediatek`** | `/vendor/etc/vintf/manifest/mapper.mediatek.xml`：`<hal format="native"><name>mapper</name><version>5.0</version><interface><instance>mediatek</instance></interface></hal>` ⇒ fork 里写死的 `("mapper","mediatek")` **与 manifest 一致，不是猜的** |
| `mapper.mediatek.so` 导出 `AIMapper_loadIMapper` | `grep -ac AIMapper_loadIMapper /vendor/lib64/hw/mapper.mediatek.so` = 1 |
| `libbinder_ndk.so` 有 `AServiceManager_openDeclaredPassthroughHal` | `grep -ac … /system/lib64/libbinder_ndk.so` = 1 |
| **`libui.so` 同时含 `openDeclaredPassthroughHal` + `AIMapper_loadIMapper`**（且不含 `getDeclaredInstances`、不含字面量 `mediatek`） | ⇒ 框架自己用的就是「openDeclaredPassthroughHal 拿句柄 → `dlsym(AIMapper_loadIMapper)`」这一套，**说明该 API 返回的是 dlopen 句柄**，fork 的用法与 libui 同构 |
| `libnativewindow.so` 导出 `AHardwareBuffer_getNativeHandle`（NDK 公共头里没有它） | `grep -ac` = 1 ⇒ 探针里用 `dlsym` 弱引用取（`mesa` 自己也依赖它） |
| HIDL mapper@4.0 是 binderized 服务（`DM,FC`，几百个 client） | `lshal \| grep mapper`；另有 `/vendor/lib64/hw/android.hardware.graphics.mapper@4.0-impl-mediatek.so`、`…allocator-V2-mediatek.so`、`gralloc.default.so` |

> 尚未证实（只能靠探针在机上回答）：shell 域（`u:r:shell:s0`）能否真的 `dlopen` 那个 `same_process_hal_file`，以及 IMapper V5 对本机 GPU AHB 返回的 metadata 究竟长什么样。

### 1.4 仓库里已有、可直接复用的资产

* `tests/vulkan-smoke/enumerate.c`：dlopen ICD + negotiate + CreateInstance 的既有写法。
* `tests/ahb/ahb*.c`、`tests/ahb/fdimport*.c`：AHB 分配/导入/GPU 渲染/CPU 回读。
* `tests/offscreen/triangle.c` + `tri.vert/frag(.spv.h)`：离屏三角形。
* **`tests/android-loader-app/`（已能出 APK）**：`MainActivity` 起 `SurfaceView` → JNI `ANativeWindow_fromSurface` → `vkCreateAndroidSurfaceKHR` → caps/formats/presentModes → **`vkCreateSwapchainKHR` → acquire → present**（`jni/panvk_loader_test.c:452-620`，另有 Gate G 的 AHB 像素校验）。
  → 这是**现成的 App 上下文 WSI 探针**：如果 shell 域被 SELinux 挡住 mapper，直接用它对比，可一步区分「域限制」与「代码缺陷」。

---

## 2. 卡点精确定位：u_gralloc 到底在哪一步被调用

按 `panvk_wsi_android_create_swapchain()`（`panvk_wsi.c:323-455`）逐句对应：

| # | WSI 里的动作 | 内部落点 | 是否碰 u_gralloc |
|---|---|---|---|
| 1 | `AHardwareBuffer_allocate(R8G8B8A8_UNORM, GPU_COLOR_OUTPUT\|GPU_SAMPLED_IMAGE\|CPU_READ_OFTEN)` `panvk_wsi.c:366-376` —— **自己分配**，不是从 ANativeWindow 拿 | 框架 gralloc（libui/AIDL allocator） | 否 |
| 2 | `CreateImage` + `VkExternalMemoryImageCreateInfo{ANDROID_HARDWARE_BUFFER}` | `panvk_android_is_gralloc_image`（`panvk_android.c:23-44`）命中 → 无 `NATIVE_BUFFER_ANDROID` → `vk_android_init_deferred_image()` **延迟建图** | **否**（此处不解析 layout） |
| 3 | `vkGetAndroidHardwareBufferPropertiesANDROID` `panvk_wsi.c:396-401` | `panvk_android.c:440-458`：先 `vk_common_…`，再 `panvk_android_find_dma_buf_fd()`（对 native handle 的每个 fd 试 `GetMemoryFdPropertiesKHR(DMA_BUF)`）；**任一失败→`VK_ERROR_INVALID_EXTERNAL_HANDLE`** | **仅当 AHB 格式不在等价表**（如 IMPLEMENTATION_DEFINED）才在 `vk_android.c:1066-1076` 走 u_gralloc。R8G8B8A8_UNORM 是已知格式 ⇒ 常规路径**不**碰 |
| 4 | `AllocateMemory(VkImportAndroidHardwareBufferInfoANDROID + dedicated image)` `panvk_wsi.c:412-431` | `panvk_android.c:405-437`（`panvk_android_allocate_ahb_memory`）→ `:293-…`（`panvk_android_import_ahb_memory`）→ `:226-…`（`panvk_android_ahb_image_init`）→ **`vk_android.c:670-687 vk_android_get_ahb_layout()` → `:141-252 vk_gralloc_to_drm_explicit_layout()` → `u_gralloc_get_buffer_basic_info()`** | ★**是**。失败即 `mesa_loge("Failed to get u_gralloc_buffer_basic_info")`（`vk_android.c:1075`）或 `"u_gralloc_get_buffer_basic_info failed"`（`:151`）→ `VK_ERROR_INVALID_EXTERNAL_HANDLE` |
| 5 | `vkCreateSwapchainKHR` 返回 | —— | 结果：**VK_ERROR_INVALID_EXTERNAL_HANDLE** |

**所以要纠正一句表述**：「Mesa 的 u_gralloc 在 `vk_android.c` 里把 AHB 转 DRM 描述失败」——准确说法是：
**失败发生在第 4 步 `AllocateMemory` 内部的 `vk_android_get_ahb_layout()`，经 `u_gralloc_get_buffer_basic_info()` 落到唯一可用的 FALLBACK，再落到 `panvk_v19_query_mapper()`；它在 `u_gralloc_fallback.c:427-431` 直接 fail-closed。**
这条链**完全不需要 Surface**，所以探针可以用「自己 `AHardwareBuffer_allocate` + 导入」把这一环单独复现（`--mode=ahb`），并用 `--mode=ahbimpdef` 把它提前到第 3 步。

---

## 3. 探针设计（为什么这样设计）

| mode | 覆盖的环节 | 需要 window? | 判据 |
|---|---|---|---|
| `render` | instance→device→image→内存→`vkCmdClearColorImage`→`vkCmdCopyImageToBuffer`→CPU 回读 | 否 | 6 个采样像素 = 清屏色（±2），见 §7 |
| `tri` | 真·图形管线（内嵌 SPIR-V）渲染红三角→回读 | 否 | 中心红(或 BGR)、角落=清屏色 |
| `ahb` | **只走 WSI 的那条 AHB 导入链**（u_gralloc 环） | 否 | 第 4 步 `vkAllocateMemory` 的 `VkResult` + logcat |
| `ahbimpdef` | 同上但格式用 `IMPLEMENTATION_DEFINED(0x22)`，把 u_gralloc 提前到 properties 查询 | 否 | 第 3 步 `VkResult` + logcat |
| `headless` | `vkCreateHeadlessSurfaceEXT` → caps/formats/modes → swapchain → images → acquire → present | 否（纯 headless surface） | 各步 `VkResult` |
| `win` | `AImageReader` 的 window → `vkCreateAndroidSurfaceKHR` → caps/formats/modes → **`vkCreateSwapchainKHR`** → images → acquire → clear → present → 读回窗口像素 | 是（ImageReader 的 Surface，**不需要 Java/Activity**） | 各步 `VkResult` + 窗口像素 |
| `mapper` | **完全不碰 Vulkan**：复现 FALLBACK 的 AIDL IMapper V5 取 metadata 流程并 hexdump | 否 | 三条加载路由哪条成功 / `version` / 5 个 metadata 的 size+原始字节 |

设计要点：

1. **不经 Vulkan loader**：Android 的 `libvulkan.so` 走 HAL 白名单发现（`vulkan.<ro.hardware>.so` 需在 `/vendor/lib64/hw`，要 root），
   而 ICD 只导出 3 个符号 → 本探针 `dlopen` 直连 ICD（无 root 可行）。这也意味着 **`VK_LOADER_DEBUG` 对本探针无效**；
   真正的证据在 **logcat tag `MESA`**（§6）。
2. **从 Java 侧拿 `ANativeWindow` 不方便** → 用 `AImageReader` 的 window（`libmediandk`，公有 NDK API；`AImageReader_new` + `AImageReader_getWindow`）；
   纯 headless 用 `vkCreateHeadlessSurfaceEXT`；两者都不要时用 `--mode=render/ahb`（**完全不要 surface**）。
3. `--mode=ahb` 把 u_gralloc 单独拎出来：它复制的就是 `panvk_wsi_android_create_swapchain()` 的第 1/2/3/4 步，**一行不多一行不少**。
4. `--mode=mapper` 是**独立于驱动**的判据：直接调用厂商 IMapper V5，把 5 个 standard metadata 的**原始字节**打印出来。
   这是唯一能回答「IMapper 到底给不给 fourcc/modifier/plane layout、以及 fork 里 `panvk_v19_get_scalar()` 的解析假设对不对」的手段。
5. 每一步都打印 `VkResult`（含 `VK_ERROR_*` 人名），并且**每个 mode 都可在失败处立即返回**，不会挂死（fence 等待 5s 超时；`acquire` 不传同步对象，因为 panvk 的 `acquire_next_image` 是立即返回的轮转实现，但 `wsi_common.c:2288-2310` 会替它 signal fence/semaphore，故两者都安全）。

---

## 4. 完整源码草稿（`/root/research/probe10/panvk_wsi_probe.c`，已编译通过）

编译与运行见 §5；源码：

~~~~~~~~ (源码开始) ~~~~~~~~

```c
/* ============================================================================
 * panvk_wsi_probe.c  --  task 10: independent WSI / u_gralloc probe
 *
 *  Loads OUR ICD directly (no Vulkan loader, no root needed):
 *      dlopen(libvulkan_panfrost.so) -> vk_icdGetInstanceProcAddr -> everything
 *  (same convention as /root/zenithblue/tests/vulkan-smoke/enumerate.c)
 *
 *  Modes (every VkResult is printed; also to logcat tag P10PROBE):
 *    render    no surface at all: image + clear + CopyImageToBuffer + readback
 *              -> proves the driver renders (fallback proof, no WSI involved)
 *    tri       graphics pipeline (embedded SPIR-V) draws a red triangle into an
 *              image, then CopyImageToBuffer + readback: centre pixel red,
 *              corner pixel = clear colour.  Strongest "driver rasterises" proof.
 *    ahb       AHardwareBuffer_allocate (exactly like panvk_wsi.c does) ->
 *              external VkImage -> GetAndroidHardwareBufferPropertiesANDROID ->
 *              AllocateMemory(import AHB + dedicated image) -> BindImageMemory
 *              -> render + readback.  The AllocateMemory step is the one that
 *              calls u_gralloc (vk_android_get_ahb_layout).  Isolates u_gralloc
 *              with NO Surface and NO window at all.
 *    ahbimpdef same, but the AHB is AHARDWAREBUFFER_FORMAT_IMPLEMENTATION_DEFINED
 *              (0x22): u_gralloc is then hit one call earlier, inside
 *              vkGetAndroidHardwareBufferPropertiesANDROID.
 *    headless  vkCreateHeadlessSurfaceEXT -> caps/formats/presentModes ->
 *              swapchain -> images -> acquire -> present (no AHB, no window)
 *    win       AImageReader -> ANativeWindow -> vkCreateAndroidSurfaceKHR ->
 *              caps/formats/presentModes -> vkCreateSwapchainKHR ->
 *              vkGetSwapchainImagesKHR -> acquire -> clear -> present ->
 *              drain the ImageReader and check the pixels that reached the
 *              window.  THIS is the step that currently dies with
 *              VK_ERROR_INVALID_EXTERNAL_HANDLE.
 *    mapper    no Vulkan at all: reproduce the driver's patched u_gralloc
 *              fallback (AServiceManager_openDeclaredPassthroughHal ->
 *              dlsym AIMapper_loadIMapper -> importBuffer ->
 *              getStandardMetadata) and hexdump the raw metadata blobs.
 *
 *  build: build.sh (NDK r27c, aarch64-linux-android35)
 *  run  : /data/local/tmp/panvk_wsi_probe --icd=/data/local/tmp/libvulkan_panfrost.so
 * ==========================================================================*/
#define VK_USE_PLATFORM_ANDROID_KHR 1
#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>

#include <android/hardware_buffer.h>
#include <android/log.h>
#include <android/native_window.h>
#include <media/NdkImageReader.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef AHARDWAREBUFFER_FORMAT_IMPLEMENTATION_DEFINED
#define AHARDWAREBUFFER_FORMAT_IMPLEMENTATION_DEFINED 0x22
#endif

#define W 64
#define H 64
#define TAG "P10PROBE"

#define LOGI(...) do { printf(__VA_ARGS__); printf("\n"); fflush(stdout); \
   __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__); } while (0)
#define LOGE(...) do { printf(__VA_ARGS__); printf("\n"); fflush(stdout); \
   __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__); } while (0)

/* ------------------------------------------------------------------ helpers */
static const char *vkres(VkResult r)
{
   switch (r) {
   case VK_SUCCESS: return "VK_SUCCESS";
   case VK_NOT_READY: return "VK_NOT_READY";
   case VK_TIMEOUT: return "VK_TIMEOUT";
   case VK_INCOMPLETE: return "VK_INCOMPLETE";
   case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
   case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
   case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
   case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
   case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
   case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
   case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
   case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
   case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
   case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
   case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
   case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
   case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
   case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
   default: return "VK_ERROR_<other>";
   }
}

static const char *sfmt(VkFormat f)
{
   switch (f) {
   case VK_FORMAT_UNDEFINED: return "UNDEFINED";
   case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
   case VK_FORMAT_R8G8B8A8_SRGB: return "R8G8B8A8_SRGB";
   case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
   default: return "?";
   }
}

static const char *spm(VkPresentModeKHR m)
{
   switch (m) {
   case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
   case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
   case VK_PRESENT_MODE_FIFO_KHR: return "FIFO";
   case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
   default: return "?";
   }
}

/* native_handle_t: private libnativewindow export (mesa uses it too) -------- */
typedef struct p10_native_handle {
   int version;
   int numFds;
   int numInts;
   int data[0];
} p10_native_handle_t;
typedef const p10_native_handle_t *(*get_native_handle_fn)(const AHardwareBuffer *);
static get_native_handle_fn g_get_native_handle;

static void dump_native_handle(const AHardwareBuffer *ahb)
{
   if (!g_get_native_handle) {
      LOGI("  native handle: AHardwareBuffer_getNativeHandle NOT resolvable");
      return;
   }
   const p10_native_handle_t *h = g_get_native_handle(ahb);
   if (!h) { LOGI("  native handle: NULL"); return; }
   LOGI("  native handle: version=%d numFds=%d numInts=%d", h->version, h->numFds, h->numInts);
   for (int i = 0; i < h->numFds; i++) {
      int fd = h->data[i];
      struct stat st;
      long sz = -1;
      if (fd >= 0 && fstat(fd, &st) == 0) sz = (long)st.st_size;
      LOGI("    fd[%d]=%d size=%ld", i, fd, sz);
   }
}

/* loader-less entrypoint plumbing ----------------------------------------- */
typedef PFN_vkVoidFunction (*icd_gipa_fn)(VkInstance, const char *);
static icd_gipa_fn g_gipa;

#define INST_FNS(X)                                    \
   X(vkEnumeratePhysicalDevices)                       \
   X(vkGetPhysicalDeviceProperties)                    \
   X(vkGetPhysicalDeviceMemoryProperties)              \
   X(vkGetPhysicalDeviceQueueFamilyProperties)         \
   X(vkEnumerateDeviceExtensionProperties)             \
   X(vkCreateDevice)                                   \
   X(vkGetDeviceProcAddr)                              \
   X(vkCreateAndroidSurfaceKHR)                        \
   X(vkCreateHeadlessSurfaceEXT)                       \
   X(vkDestroySurfaceKHR)                              \
   X(vkGetPhysicalDeviceSurfaceSupportKHR)             \
   X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR)        \
   X(vkGetPhysicalDeviceSurfaceFormatsKHR)             \
   X(vkGetPhysicalDeviceSurfacePresentModesKHR)

struct inst_fns {
#define X(n) PFN_##n n;
   INST_FNS(X)
#undef X
};

#define DEV_FNS(X)                                     \
   X(vkGetDeviceQueue)                                 \
   X(vkGetAndroidHardwareBufferPropertiesANDROID)      \
   X(vkCreateImage)                                    \
   X(vkDestroyImage)                                   \
   X(vkGetImageMemoryRequirements)                     \
   X(vkBindImageMemory)                                \
   X(vkAllocateMemory)                                 \
   X(vkFreeMemory)                                     \
   X(vkCreateBuffer)                                   \
   X(vkDestroyBuffer)                                  \
   X(vkGetBufferMemoryRequirements)                    \
   X(vkBindBufferMemory)                               \
   X(vkMapMemory)                                      \
   X(vkUnmapMemory)                                    \
   X(vkCreateImageView)                                \
   X(vkDestroyImageView)                               \
   X(vkCreateCommandPool)                              \
   X(vkDestroyCommandPool)                             \
   X(vkAllocateCommandBuffers)                         \
   X(vkBeginCommandBuffer)                             \
   X(vkEndCommandBuffer)                               \
   X(vkCmdPipelineBarrier)                             \
   X(vkCmdClearColorImage)                             \
   X(vkCmdCopyImageToBuffer)                           \
   X(vkCmdBeginRenderPass)                             \
   X(vkCmdEndRenderPass)                               \
   X(vkCmdBindPipeline)                                \
   X(vkCmdDraw)                                        \
   X(vkCreateRenderPass)                               \
   X(vkDestroyRenderPass)                              \
   X(vkCreateFramebuffer)                              \
   X(vkDestroyFramebuffer)                             \
   X(vkCreateShaderModule)                             \
   X(vkDestroyShaderModule)                            \
   X(vkCreatePipelineLayout)                           \
   X(vkDestroyPipelineLayout)                          \
   X(vkCreateGraphicsPipelines)                        \
   X(vkDestroyPipeline)                                \
   X(vkQueueSubmit)                                    \
   X(vkQueueWaitIdle)                                  \
   X(vkDeviceWaitIdle)                                 \
   X(vkCreateFence)                                    \
   X(vkDestroyFence)                                   \
   X(vkWaitForFences)                                  \
   X(vkResetFences)                                    \
   X(vkCreateSwapchainKHR)                             \
   X(vkDestroySwapchainKHR)                            \
   X(vkGetSwapchainImagesKHR)                          \
   X(vkAcquireNextImageKHR)                            \
   X(vkQueuePresentKHR)

struct dev_fns {
#define X(n) PFN_##n n;
   DEV_FNS(X)
#undef X
};

struct ctx {
   VkInstance instance;
   VkPhysicalDevice phys;
   VkPhysicalDeviceProperties props;
   uint32_t qfam;
   VkDevice dev;
   VkQueue queue;
   struct inst_fns i;
   struct dev_fns d;
};

struct rrb {
   VkBuffer buf;
   VkDeviceMemory mem;
   void *mapped;
};

static int inst_ext_available(const char *name)
{
   PFN_vkEnumerateInstanceExtensionProperties e =
      (PFN_vkEnumerateInstanceExtensionProperties)g_gipa(NULL, "vkEnumerateInstanceExtensionProperties");
   if (!e) return 0;
   uint32_t n = 0;
   if (e(NULL, &n, NULL) != VK_SUCCESS || n == 0) return 0;
   VkExtensionProperties *p = calloc(n, sizeof(*p));
   if (!p) return 0;
   int found = 0;
   if (e(NULL, &n, p) == VK_SUCCESS)
      for (uint32_t k = 0; k < n; k++)
         if (!strcmp(p[k].extensionName, name)) found = 1;
   free(p);
   return found;
}

static void list_extensions(VkPhysicalDevice pd, PFN_vkEnumerateDeviceExtensionProperties e)
{
   uint32_t n = 0;
   if (!e || e(pd, NULL, &n, NULL) != VK_SUCCESS || n == 0) { LOGI("  (no device ext list)"); return; }
   VkExtensionProperties *p = calloc(n, sizeof(*p));
   if (!p) return;
   if (e(pd, NULL, &n, p) == VK_SUCCESS) {
      char line[1024] = {0};
      size_t off = 0;
      for (uint32_t k = 0; k < n; k++) {
         if (off + strlen(p[k].extensionName) + 2 > sizeof(line)) { LOGI("  ext: %s", line); off = 0; line[0] = 0; }
         off += (size_t)snprintf(line + off, sizeof(line) - off, "%s ", p[k].extensionName);
      }
      if (off) LOGI("  ext: %s", line);
   }
   free(p);
}

static int find_mem_type(PFN_vkGetPhysicalDeviceMemoryProperties gmp, VkPhysicalDevice pd,
                         uint32_t bits, VkMemoryPropertyFlags want)
{
   VkPhysicalDeviceMemoryProperties mp;
   gmp(pd, &mp);
   for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
      if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return (int)i;
   for (uint32_t i = 0; i < 32; i++)
      if (bits & (1u << i)) return (int)i;
   return -1;
}

/* ------------------------------------------------- instance / device setup */
static VkResult create_instance(int want_android, int want_headless, VkInstance *out)
{
   const char *ext[8];
   uint32_t ne = 0;
   ext[ne++] = VK_KHR_SURFACE_EXTENSION_NAME;
   if (want_android && inst_ext_available(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME))
      ext[ne++] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
   else if (want_android)
      LOGI("  !! %s not advertised", VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
   if (want_headless && inst_ext_available(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME))
      ext[ne++] = VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME;
   else if (want_headless)
      LOGI("  !! %s not advertised", VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);

   LOGI("  instance extensions requested (%u):", ne);
   for (uint32_t k = 0; k < ne; k++) LOGI("    %s", ext[k]);

   VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "panvk-wsi-probe",
      .apiVersion = VK_API_VERSION_1_3,
   };
   VkInstanceCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
      .enabledExtensionCount = ne,
      .ppEnabledExtensionNames = ext,
   };
   VkResult r = ((PFN_vkCreateInstance)g_gipa(NULL, "vkCreateInstance"))(&ici, NULL, out);
   LOGI("  vkCreateInstance -> %d (%s)", (int)r, vkres(r));
   return r;
}

static VkResult create_device(struct ctx *c, int want_swapchain)
{
   uint32_t n = 0;
   c->i.vkGetPhysicalDeviceQueueFamilyProperties(c->phys, &n, NULL);
   VkQueueFamilyProperties *qf = calloc(n ? n : 1, sizeof(*qf));
   c->i.vkGetPhysicalDeviceQueueFamilyProperties(c->phys, &n, qf);
   int pick = -1;
   for (uint32_t k = 0; k < n; k++) {
      LOGI("  queue family %u: flags=0x%x count=%u", k, qf[k].queueFlags, qf[k].queueCount);
      if ((qf[k].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
          (pick < 0 || !(qf[pick].queueFlags & VK_QUEUE_GRAPHICS_BIT)))
         pick = (int)k;
   }
   if (pick < 0 && n) pick = 0;
   c->qfam = (uint32_t)(pick < 0 ? 0 : pick);
   free(qf);

   float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = c->qfam,
      .queueCount = 1,
      .pQueuePriorities = &prio,
   };
   const char *dext[1];
   uint32_t nde = 0;
   if (want_swapchain) {
      uint32_t en = 0;
      c->i.vkEnumerateDeviceExtensionProperties(c->phys, NULL, &en, NULL);
      VkExtensionProperties *ep = calloc(en ? en : 1, sizeof(*ep));
      int have = 0;
      if (ep && c->i.vkEnumerateDeviceExtensionProperties(c->phys, NULL, &en, ep) == VK_SUCCESS)
         for (uint32_t k = 0; k < en; k++)
            if (!strcmp(ep[k].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) have = 1;
      free(ep);
      if (have) dext[nde++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
      else LOGI("  !! %s not advertised, device will not do swapchains", VK_KHR_SWAPCHAIN_EXTENSION_NAME);
   }
   VkDeviceCreateInfo dci = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &qci,
      .enabledExtensionCount = nde,
      .ppEnabledExtensionNames = dext,
   };
   VkResult r = c->i.vkCreateDevice(c->phys, &dci, NULL, &c->dev);
   LOGI("  vkCreateDevice(qfam=%u) -> %d (%s)", c->qfam, (int)r, vkres(r));
   if (r != VK_SUCCESS) return r;
   return VK_SUCCESS;
}

static int load_dev_fns(struct ctx *c)
{
   int ok = 1;
#define LOAD_DEV(name) do { c->d.name = (PFN_##name)c->i.vkGetDeviceProcAddr(c->dev, #name); \
   if (!c->d.name) { LOGE("  !! missing device entrypoint %s", #name); ok = 0; } } while (0);
#define X(n) LOAD_DEV(n)
   DEV_FNS(X)
#undef X
#undef LOAD_DEV
   if (ok) c->d.vkGetDeviceQueue(c->dev, c->qfam, 0, &c->queue);
   if (!c->queue) { LOGE("  !! vkGetDeviceQueue gave NULL"); ok = 0; }
   return ok;
}

/* ------------------------------------------------------- render + readback */
static VkResult make_host_buffer(struct ctx *c, VkDeviceSize size, struct rrb *o)
{
   VkBufferCreateInfo bci = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
   };
   VkResult r = c->d.vkCreateBuffer(c->dev, &bci, NULL, &o->buf);
   if (r != VK_SUCCESS) { LOGI("  vkCreateBuffer(readback) -> %d (%s)", (int)r, vkres(r)); return r; }
   VkMemoryRequirements mr;
   c->d.vkGetBufferMemoryRequirements(c->dev, o->buf, &mr);
   int mt = find_mem_type(c->i.vkGetPhysicalDeviceMemoryProperties, c->phys, mr.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   if (mt < 0) { LOGI("  no host-visible memory type for readback buffer"); return VK_ERROR_FEATURE_NOT_PRESENT; }
   VkMemoryAllocateInfo mai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size,
      .memoryTypeIndex = (uint32_t)mt,
   };
   r = c->d.vkAllocateMemory(c->dev, &mai, NULL, &o->mem);
   if (r != VK_SUCCESS) { LOGI("  vkAllocateMemory(readback) -> %d (%s)", (int)r, vkres(r)); return r; }
   r = c->d.vkBindBufferMemory(c->dev, o->buf, o->mem, 0);
   if (r != VK_SUCCESS) { LOGI("  vkBindBufferMemory(readback) -> %d (%s)", (int)r, vkres(r)); return r; }
   r = c->d.vkMapMemory(c->dev, o->mem, 0, VK_WHOLE_SIZE, 0, &o->mapped);
   if (r != VK_SUCCESS) { LOGI("  vkMapMemory(readback) -> %d (%s)", (int)r, vkres(r)); return r; }
   memset(o->mapped, 0xAA, (size_t)size);
   return VK_SUCCESS;
}

static void free_host_buffer(struct ctx *c, struct rrb *o)
{
   if (o->mapped) c->d.vkUnmapMemory(c->dev, o->mem);
   if (o->buf) c->d.vkDestroyBuffer(c->dev, o->buf, NULL);
   if (o->mem) c->d.vkFreeMemory(c->dev, o->mem, NULL);
   memset(o, 0, sizeof(*o));
}

/* verify the 3 sampled pixels against the clear colour */
static int check_clear_pixels(const uint8_t *p, const char *tag, int *out_swapped)
{
   static const uint8_t exp[4] = { 64, 128, 191, 255 }; /* 0.25/0.5/0.75 * 255 */
   const int px[3][2] = { {0,0}, {W/2,H/2}, {W-1,H-1} };
   int good = 1, rbsp = 1, nonzero = 0;
   for (int k = 0; k < 3; k++) {
      const uint8_t *q = p + ((size_t)px[k][1] * W + px[k][0]) * 4;
      LOGI("  [%s] pixel(%d,%d) = %3u %3u %3u %3u  (want %3u %3u %3u %3u)",
           tag, px[k][0], px[k][1], q[0], q[1], q[2], q[3], exp[0], exp[1], exp[2], exp[3]);
      if (q[0] || q[1] || q[2] || q[3]) nonzero = 1;
      for (int j = 0; j < 4; j++) {
         int d = (int)q[j] - (int)exp[j];
         if (d < -2 || d > 2) good = 0;
      }
      int d0 = (int)q[0] - (int)exp[2], d2 = (int)q[2] - (int)exp[0];
      if (d0 < 0) d0 = -d0;
      if (d2 < 0) d2 = -d2;
      if (d0 > 2 || d2 > 2) rbsp = 0;
   }
   if (out_swapped) *out_swapped = rbsp;
   if (good) { LOGI("  [%s] PIXEL PASS (clear colour read back exactly)", tag); return 0; }
   if (rbsp) { LOGI("  [%s] PIXEL PASS (R/B swapped: gralloc ABGR vs RGBA - GPU rendered)", tag); return 0; }
   if (nonzero) { LOGI("  [%s] PIXEL UNEXPECTED (non-zero but not the clear colour)", tag); return 0; }
   LOGE("  [%s] PIXEL FAIL (all zero: nothing rendered or readback broken)", tag);
   return 1;
}

static int clear_copy_readback(struct ctx *c, VkImage img, const char *tag)
{
   int fails = 0;
   struct rrb rb = {0};
   VkCommandPool pool = VK_NULL_HANDLE;
   VkCommandBuffer cmd = VK_NULL_HANDLE;
   VkFence fence = VK_NULL_HANDLE;

   if (make_host_buffer(c, (VkDeviceSize)W * H * 4, &rb) != VK_SUCCESS) return 1;

   VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                   .queueFamilyIndex = c->qfam };
   VkResult r = c->d.vkCreateCommandPool(c->dev, &pci, NULL, &pool);
   if (r != VK_SUCCESS) { LOGI("  vkCreateCommandPool -> %d", (int)r); free_host_buffer(c, &rb); return 1; }
   VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
   c->d.vkAllocateCommandBuffers(c->dev, &cbai, &cmd);
   VkCommandBufferBeginInfo cbbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
   c->d.vkBeginCommandBuffer(cmd, &cbbi);

   VkImageSubresourceRange rng = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0,
      .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 };
   VkImageMemoryBarrier b = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = img, .subresourceRange = rng,
   };
   c->d.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
   VkClearColorValue cc;
   memset(&cc, 0, sizeof(cc));
   cc.float32[0] = 0.25f; cc.float32[1] = 0.5f; cc.float32[2] = 0.75f; cc.float32[3] = 1.0f;
   c->d.vkCmdClearColorImage(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cc, 1, &rng);
   b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
   b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
   c->d.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
   VkBufferImageCopy region = {
      .bufferOffset = 0, .bufferRowLength = 0, .bufferImageHeight = 0,
      .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
      .imageOffset = { 0, 0, 0 }, .imageExtent = { W, H, 1 },
   };
   c->d.vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.buf, 1, &region);
   VkBufferMemoryBarrier bb = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = rb.buf, .offset = 0, .size = VK_WHOLE_SIZE,
   };
   c->d.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &bb, 0, NULL);
   r = c->d.vkEndCommandBuffer(cmd);
   if (r != VK_SUCCESS) { LOGI("  vkEndCommandBuffer -> %d", (int)r); fails++; }

   VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
   c->d.vkCreateFence(c->dev, &fci, NULL, &fence);
   VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1, .pCommandBuffers = &cmd };
   r = c->d.vkQueueSubmit(c->queue, 1, &si, fence);
   LOGI("  [%s] vkQueueSubmit(clear+copy) -> %d (%s)", tag, (int)r, vkres(r));
   if (r != VK_SUCCESS) fails++;
   VkResult w = c->d.vkWaitForFences(c->dev, 1, &fence, VK_TRUE, 5000000000ULL);
   LOGI("  [%s] vkWaitForFences(5s) -> %d (%s)", tag, (int)w, vkres(w));
   if (w != VK_SUCCESS) fails++;

   if (!fails)
      fails += check_clear_pixels((const uint8_t *)rb.mapped, tag, NULL);

   if (fence) c->d.vkDestroyFence(c->dev, fence, NULL);
   if (pool) c->d.vkDestroyCommandPool(c->dev, pool, NULL);
   free_host_buffer(c, &rb);
   return fails;
}

/* ---------------------------------------------------------------- mode: render */
static int make_image(struct ctx *c, VkImage *out, VkDeviceMemory *mem_out,
                      VkImageUsageFlags usage, const void *pnext, const char *tag)
{
   VkImageCreateInfo ici = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = pnext,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   };
   VkResult r = c->d.vkCreateImage(c->dev, &ici, NULL, out);
   LOGI("  vkCreateImage(%s) -> %d (%s)", tag, (int)r, vkres(r));
   if (r != VK_SUCCESS) return 1;
   VkMemoryRequirements mr;
   c->d.vkGetImageMemoryRequirements(c->dev, *out, &mr);
   LOGI("  image mem req: size=%llu typeBits=0x%x align=%llu",
        (unsigned long long)mr.size, mr.memoryTypeBits, (unsigned long long)mr.alignment);
   int mt = find_mem_type(c->i.vkGetPhysicalDeviceMemoryProperties, c->phys, mr.memoryTypeBits,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   LOGI("  chosen memoryTypeIndex=%d", mt);
   VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size, .memoryTypeIndex = (uint32_t)(mt < 0 ? 0 : mt) };
   r = c->d.vkAllocateMemory(c->dev, &mai, NULL, mem_out);
   LOGI("  vkAllocateMemory(image) -> %d (%s)", (int)r, vkres(r));
   if (r != VK_SUCCESS) return 1;
   r = c->d.vkBindImageMemory(c->dev, *out, *mem_out, 0);
   LOGI("  vkBindImageMemory -> %d (%s)", (int)r, vkres(r));
   if (r != VK_SUCCESS) return 1;
   return 0;
}

static int mode_render(struct ctx *c)
{
   int fails = 0;
   LOGI("== mode render: image -> clear -> CopyImageToBuffer -> CPU readback");
   VkImage img = VK_NULL_HANDLE;
   VkDeviceMemory mem = VK_NULL_HANDLE;
   if (make_image(c, &img, &mem,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT, NULL, "own, optimal, 64x64 RGBA8, color|src|dst"))
      fails++;
   else
      fails += clear_copy_readback(c, img, "render");
   if (mem) c->d.vkFreeMemory(c->dev, mem, NULL);
   if (img) c->d.vkDestroyImage(c->dev, img, NULL);
   return fails;
}

#ifdef HAVE_TRIANGLE
#include "tri.vert.spv.h"
#include "tri.frag.spv.h"

/* real graphics pipeline: clear + draw a red triangle, read the pixels back */
static int mode_triangle(struct ctx *c)
{
   int fails = 0;
   const uint8_t clear_exp[4] = { 64, 128, 191, 255 };
   LOGI("== mode tri: render pass + graphics pipeline + draw(3) -> readback");
   VkImage img = VK_NULL_HANDLE;
   VkDeviceMemory mem = VK_NULL_HANDLE;
   if (make_image(c, &img, &mem,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                  NULL, "triangle target"))
      return 1;

   VkImageView view = VK_NULL_HANDLE;
   VkRenderPass rp = VK_NULL_HANDLE;
   VkFramebuffer fb = VK_NULL_HANDLE;
   VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
   VkPipelineLayout pl = VK_NULL_HANDLE;
   VkPipeline pipe = VK_NULL_HANDLE;
   VkCommandPool pool = VK_NULL_HANDLE;
   VkCommandBuffer cmd = VK_NULL_HANDLE;
   VkFence fence = VK_NULL_HANDLE;
   struct rrb rb = {0};
   VkResult r;

   VkImageViewCreateInfo ivci = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = img, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
      .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
   };
   r = c->d.vkCreateImageView(c->dev, &ivci, NULL, &view);
   LOGI("  vkCreateImageView -> %d (%s)", (int)r, vkres(r));
   if (r != VK_SUCCESS) { fails++; goto out; }

   VkAttachmentDescription att = {
      .format = VK_FORMAT_R8G8B8A8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
   };
   VkAttachmentReference ar = { .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
   VkSubpassDescription sp = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .colorAttachmentCount = 1, .pColorAttachments = &ar,
   };
   VkSubpassDependency dep = {
      .srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
      .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
   };
   VkRenderPassCreateInfo rpci = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1, .pAttachments = &att,
      .subpassCount = 1, .pSubpasses = &sp,
      .dependencyCount = 1, .pDependencies = &dep,
   };
   r = c->d.vkCreateRenderPass(c->dev, &rpci, NULL, &rp);
   LOGI("  vkCreateRenderPass -> %d (%s)", (int)r, vkres(r));
   if (r != VK_SUCCESS) { fails++; goto out; }

   VkFramebufferCreateInfo fbci = {
      .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .renderPass = rp, .attachmentCount = 1, .pAttachments = &view,
      .width = W, .height = H, .layers = 1,
   };
   r = c->d.vkCreateFramebuffer(c->dev, &fbci, NULL, &fb);
   LOGI("  vkCreateFramebuffer -> %d (%s)", (int)r, vkres(r));
   if (r != VK_SUCCESS) { fails++; goto out; }

   VkShaderModuleCreateInfo svci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = TRI_VERT_SPV_LEN, .pCode = TRI_VERT_SPV };
   VkShaderModuleCreateInfo sfci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = TRI_FRAG_SPV_LEN, .pCode = TRI_FRAG_SPV };
   r = c->d.vkCreateShaderModule(c->dev, &svci, NULL, &vs);
   LOGI("  vkCreateShaderModule(vert) -> %d (%s)", (int)r, vkres(r));
   if (r != VK_SUCCESS) { fails++; goto out; }
   r = c->d.vkCreateShaderModule(c->dev, &sfci, NULL, &fs);
   LOGI("  vkCreateShaderModule(frag) -> %d (%s)", (int)r, vkres(r));
   if (r != VK_SUCCESS) { fails++; goto out; }

   VkPipelineShaderStageCreateInfo stages[2] = {
      { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main" },
      { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main" },
   };
   VkPipelineVertexInputStateCreateInfo vi = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
   VkPipelineInputAssemblyStateCreateInfo ia = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
   VkViewport vp = { 0.f, 0.f, (float)W, (float)H, 0.f, 1.f };
   VkRect2D scissor = { { 0, 0 }, { W, H } };
   VkPipelineViewportStateCreateInfo vps = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &scissor };
   VkPipelineRasterizationStateCreateInfo rs = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
   VkPipelineMultisampleStateCreateInfo ms = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
   VkPipelineColorBlendAttachmentState cba = {
      .blendEnable = VK_FALSE,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
   VkPipelineColorBlendStateCreateInfo cb = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1, .pAttachments = &cba };
   VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
   r = c->d.vkCreatePipelineLayout(c->dev, &plci, NULL, &pl);
   LOGI("  vkCreatePipelineLayout -> %d (%s)", (int)r, vkres(r));
   if (r != VK_SUCCESS) { fails++; goto out; }

   VkGraphicsPipelineCreateInfo gpci = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2, .pStages = stages,
      .pVertexInputState = &vi, .pInputAssemblyState = &ia,
      .pViewportState = &vps, .pRasterizationState = &rs,
      .pMultisampleState = &ms, .pColorBlendState = &cb,
      .layout = pl, .renderPass = rp, .subpass = 0,
   };
   r = c->d.vkCreateGraphicsPipelines(c->dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe);
   LOGI("  vkCreateGraphicsPipelines -> %d (%s)", (int)r, vkres(r));
   if (r != VK_SUCCESS) { fails++; goto out; }

   if (make_host_buffer(c, (VkDeviceSize)W * H * 4, &rb) != VK_SUCCESS) { fails++; goto out; }
   VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                   .queueFamilyIndex = c->qfam };
   c->d.vkCreateCommandPool(c->dev, &pci, NULL, &pool);
   VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
   c->d.vkAllocateCommandBuffers(c->dev, &cbai, &cmd);
   VkCommandBufferBeginInfo cbbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
   c->d.vkBeginCommandBuffer(cmd, &cbbi);
   VkClearValue cv;
   memset(&cv, 0, sizeof(cv));
   cv.color.float32[0] = 0.25f; cv.color.float32[1] = 0.5f;
   cv.color.float32[2] = 0.75f; cv.color.float32[3] = 1.0f;
   VkRenderPassBeginInfo rpbi = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .renderPass = rp, .framebuffer = fb,
      .renderArea = { { 0, 0 }, { W, H } },
      .clearValueCount = 1, .pClearValues = &cv,
   };
   c->d.vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
   c->d.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
   c->d.vkCmdDraw(cmd, 3, 1, 0, 0);
   c->d.vkCmdEndRenderPass(cmd);
   VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT };
   c->d.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
   VkBufferImageCopy region = {
      .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
      .imageExtent = { W, H, 1 },
   };
   c->d.vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.buf, 1, &region);
   VkBufferMemoryBarrier bb = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = rb.buf, .offset = 0, .size = VK_WHOLE_SIZE,
   };
   c->d.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &bb, 0, NULL);
   c->d.vkEndCommandBuffer(cmd);
   VkFenceCreateInfo fci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
   c->d.vkCreateFence(c->dev, &fci, NULL, &fence);
   VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1, .pCommandBuffers = &cmd };
   r = c->d.vkQueueSubmit(c->queue, 1, &si, fence);
   LOGI("  [tri] vkQueueSubmit(draw) -> %d (%s)", (int)r, vkres(r));
   if (r != VK_SUCCESS) fails++;
   VkResult w = c->d.vkWaitForFences(c->dev, 1, &fence, VK_TRUE, 5000000000ULL);
   LOGI("  [tri] vkWaitForFences(5s) -> %d (%s)", (int)w, vkres(w));
   if (w != VK_SUCCESS) fails++;

   if (!fails) {
      const uint8_t *p = (const uint8_t *)rb.mapped;
      const uint8_t *corner = p;
      const uint8_t *centre = p + ((size_t)(H / 2) * W + (W / 2)) * 4;
      LOGI("  [tri] corner(0,0)    = %3u %3u %3u %3u  (want clear %3u %3u %3u %3u)",
           corner[0], corner[1], corner[2], corner[3],
           clear_exp[0], clear_exp[1], clear_exp[2], clear_exp[3]);
      LOGI("  [tri] centre(%d,%d) = %3u %3u %3u %3u  (want red 255 0 0 255, or BGR 0 0 255)",
           W / 2, H / 2, centre[0], centre[1], centre[2], centre[3]);
      int red = centre[0] > 200 && centre[1] < 40 && centre[2] < 40;
      int bgr = centre[2] > 200 && centre[1] < 40 && centre[0] < 40;
      int clr = corner[0] >= 62 && corner[0] <= 66 && corner[2] >= 189 && corner[2] <= 193;
      int clr_sw = corner[2] >= 62 && corner[2] <= 66 && corner[0] >= 189 && corner[0] <= 193;
      if ((red || bgr) && (clr || clr_sw))
         LOGI("  [tri] PIXEL PASS (GPU rasterised the triangle and the clear survived)");
      else {
         LOGE("  [tri] PIXEL FAIL (centre red=%d bgr=%d, corner clear=%d sw=%d)", red, bgr, clr, clr_sw);
         fails++;
      }
   }
out:
   if (rb.buf || rb.mem) free_host_buffer(c, &rb);
   if (fence) c->d.vkDestroyFence(c->dev, fence, NULL);
   if (pool) c->d.vkDestroyCommandPool(c->dev, pool, NULL);
   if (pipe) c->d.vkDestroyPipeline(c->dev, pipe, NULL);
   if (pl) c->d.vkDestroyPipelineLayout(c->dev, pl, NULL);
   if (vs) c->d.vkDestroyShaderModule(c->dev, vs, NULL);
   if (fs) c->d.vkDestroyShaderModule(c->dev, fs, NULL);
   if (fb) c->d.vkDestroyFramebuffer(c->dev, fb, NULL);
   if (rp) c->d.vkDestroyRenderPass(c->dev, rp, NULL);
   if (view) c->d.vkDestroyImageView(c->dev, view, NULL);
   if (mem) c->d.vkFreeMemory(c->dev, mem, NULL);
   if (img) c->d.vkDestroyImage(c->dev, img, NULL);
   return fails;
}
#endif /* HAVE_TRIANGLE */

/* ------------------------------------------------------------------- mode: ahb */
static int mode_ahb(struct ctx *c, int impdef)
{
   int fails = 0;
   const uint32_t fmt = impdef ? AHARDWAREBUFFER_FORMAT_IMPLEMENTATION_DEFINED
                               : AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
   LOGI("== mode ahb%s: AHB -> external image -> import as memory (u_gralloc step)",
        impdef ? "impdef" : "");
   AHardwareBuffer_Desc desc = {
      .width = W, .height = H, .layers = 1, .format = fmt,
      .usage = AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
               AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
   };
   AHardwareBuffer *ahb = NULL;
   int rc = AHardwareBuffer_allocate(&desc, &ahb);
   LOGI("  AHardwareBuffer_allocate(fmt=0x%x usage=0x%llx) -> rc=%d ahb=%p",
        fmt, (unsigned long long)desc.usage, rc, (void *)ahb);
   if (rc != 0 || !ahb) return 1;
   AHardwareBuffer_Desc d2;
   memset(&d2, 0, sizeof(d2));
   AHardwareBuffer_describe(ahb, &d2);
   LOGI("  AHardwareBuffer_describe: w=%u h=%u stride=%u fmt=0x%x usage=0x%llx layers=%u",
        d2.width, d2.height, d2.stride, d2.format, (unsigned long long)d2.usage, d2.layers);
   dump_native_handle(ahb);

   VkExternalMemoryImageCreateInfo emici = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
   };
   VkImage img = VK_NULL_HANDLE;
   VkResult r = c->d.vkCreateImage(c->dev, &(VkImageCreateInfo){
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext = &emici,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_UNORM,
      .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
   }, NULL, &img);
   LOGI("  vkCreateImage(EXTERNAL AHB handleType) -> %d (%s)", (int)r, vkres(r));
   if (r != VK_SUCCESS) { fails++; goto out; }

   VkAndroidHardwareBufferFormatPropertiesANDROID fmtp = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
   };
   VkAndroidHardwareBufferPropertiesANDROID props = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
      .pNext = &fmtp,
   };
   r = c->d.vkGetAndroidHardwareBufferPropertiesANDROID(c->dev, ahb, &props);
   LOGI("  vkGetAndroidHardwareBufferPropertiesANDROID -> %d (%s) allocSize=%llu typeBits=0x%x"
        " format=%s externalFormat=%llu",
        (int)r, vkres(r), (unsigned long long)props.allocationSize,
        props.memoryTypeBits, sfmt(fmtp.format), (unsigned long long)fmtp.externalFormat);
   if (r != VK_SUCCESS) { fails++; goto out; }

   uint32_t mt = 0;
   for (uint32_t k = 0; k < 32; k++)
      if (props.memoryTypeBits & (1u << k)) { mt = k; break; }

   VkImportAndroidHardwareBufferInfoANDROID imp = {
      .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
      .buffer = ahb,
   };
   VkMemoryDedicatedAllocateInfo dai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = &imp, .image = img,
   };
   VkMemoryAllocateInfo mai = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &dai,
      .allocationSize = props.allocationSize,
      .memoryTypeIndex = mt,
   };
   VkDeviceMemory mem = VK_NULL_HANDLE;
   r = c->d.vkAllocateMemory(c->dev, &mai, NULL, &mem);
   LOGI("  >>> vkAllocateMemory(import AHB, dedicated image) -> %d (%s)"
        "   <== the u_gralloc step (vk_android_get_ahb_layout)", (int)r, vkres(r));
   if (r != VK_SUCCESS) { fails++; goto out; }
   r = c->d.vkBindImageMemory(c->dev, img, mem, 0);
   LOGI("  vkBindImageMemory(ahb) -> %d (%s)", (int)r, vkres(r));
   if (r != VK_SUCCESS) { fails++; c->d.vkFreeMemory(c->dev, mem, NULL); goto out; }

   fails += clear_copy_readback(c, img, "ahb");
   c->d.vkFreeMemory(c->dev, mem, NULL);
out:
   if (img) c->d.vkDestroyImage(c->dev, img, NULL);
   AHardwareBuffer_release(ahb);
   return fails;
}

/* --------------------------------------------- surface caps/formats/modes */
static int surface_queries(struct ctx *c, VkSurfaceKHR s, const char *tag,
                           VkSurfaceCapabilitiesKHR *caps_out)
{
   int fails = 0;
   VkBool32 sup = VK_FALSE;
   VkResult r = c->i.vkGetPhysicalDeviceSurfaceSupportKHR(c->phys, c->qfam, s, &sup);
   LOGI("  [%s] vkGetPhysicalDeviceSurfaceSupportKHR(qfam=%u) -> %d (%s) supported=%u",
        tag, c->qfam, (int)r, vkres(r), sup);
   if (r != VK_SUCCESS) fails++;

   VkSurfaceCapabilitiesKHR caps;
   memset(&caps, 0, sizeof(caps));
   r = c->i.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c->phys, s, &caps);
   LOGI("  [%s] vkGetPhysicalDeviceSurfaceCapabilitiesKHR -> %d (%s)", tag, (int)r, vkres(r));
   if (r != VK_SUCCESS) { fails++; return fails; }
   LOGI("  [%s]   minImageCount=%u maxImageCount=%u currentExtent=%ux%u minExtent=%ux%u "
        "maxExtent=%ux%u usage=0x%x composite=0x%x",
        tag, caps.minImageCount, caps.maxImageCount,
        caps.currentExtent.width, caps.currentExtent.height,
        caps.minImageExtent.width, caps.minImageExtent.height,
        caps.maxImageExtent.width, caps.maxImageExtent.height,
        caps.supportedUsageFlags, caps.supportedCompositeAlpha);
   if (caps_out) *caps_out = caps;

   uint32_t n = 0;
   r = c->i.vkGetPhysicalDeviceSurfaceFormatsKHR(c->phys, s, &n, NULL);
   LOGI("  [%s] vkGetPhysicalDeviceSurfaceFormatsKHR(count) -> %d (%s) n=%u", tag, (int)r, vkres(r), n);
   if (r == VK_SUCCESS && n) {
      VkSurfaceFormatKHR *f = calloc(n, sizeof(*f));
      if (f && c->i.vkGetPhysicalDeviceSurfaceFormatsKHR(c->phys, s, &n, f) == VK_SUCCESS)
         for (uint32_t k = 0; k < n; k++)
            LOGI("  [%s]   format[%u] = %s colorSpace=%d", tag, k, sfmt(f[k].format), f[k].colorSpace);
      free(f);
   } else fails++;

   n = 0;
   r = c->i.vkGetPhysicalDeviceSurfacePresentModesKHR(c->phys, s, &n, NULL);
   LOGI("  [%s] vkGetPhysicalDeviceSurfacePresentModesKHR(count) -> %d (%s) n=%u", tag, (int)r, vkres(r), n);
   if (r == VK_SUCCESS && n) {
      VkPresentModeKHR *m = calloc(n, sizeof(*m));
      if (m && c->i.vkGetPhysicalDeviceSurfacePresentModesKHR(c->phys, s, &n, m) == VK_SUCCESS)
         for (uint32_t k = 0; k < n; k++)
            LOGI("  [%s]   presentMode[%u] = %s", tag, k, spm(m[k]));
      free(m);
   } else fails++;
   return fails;
}

static int swapchain_steps(struct ctx *c, VkSurfaceKHR s, const char *tag, int do_clear)
{
   int fails = 0;
   VkSurfaceCapabilitiesKHR caps;
   memset(&caps, 0, sizeof(caps));
   fails += surface_queries(c, s, tag, &caps);
   if (fails) { LOGI("  [%s] surface queries failed, not attempting swapchain", tag); return fails; }

   uint32_t w = caps.currentExtent.width, h = caps.currentExtent.height;
   if (w == 0xFFFFFFFFu || w == 0 || h == 0xFFFFFFFFu || h == 0) { w = W; h = H; }
   uint32_t minc = caps.minImageCount ? caps.minImageCount : 2;
   if (caps.maxImageCount && minc > caps.maxImageCount) minc = caps.maxImageCount;

   VkSwapchainCreateInfoKHR scci = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = s,
      .minImageCount = minc,
      .imageFormat = VK_FORMAT_R8G8B8A8_UNORM,
      .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
      .imageExtent = { w, h },
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform = caps.currentTransform,
      .compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                           ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                           : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
      .presentMode = VK_PRESENT_MODE_FIFO_KHR,
      .clipped = VK_TRUE,
      .oldSwapchain = VK_NULL_HANDLE,
   };
   VkSwapchainKHR sc = VK_NULL_HANDLE;
   VkResult r = c->d.vkCreateSwapchainKHR(c->dev, &scci, NULL, &sc);
   LOGI("  [%s] >>> vkCreateSwapchainKHR(minImageCount=%u %ux%u RGBA8 FIFO) -> %d (%s)"
        "  <== the current blocker", tag, minc, w, h, (int)r, vkres(r));
   if (r != VK_SUCCESS) return fails + 1;

   uint32_t n = 0;
   r = c->d.vkGetSwapchainImagesKHR(c->dev, sc, &n, NULL);
   LOGI("  [%s] vkGetSwapchainImagesKHR(count) -> %d (%s) n=%u", tag, (int)r, vkres(r), n);
   if (r != VK_SUCCESS || n == 0) { fails++; goto out; }
   VkImage *imgs = calloc(n, sizeof(*imgs));
   r = c->d.vkGetSwapchainImagesKHR(c->dev, sc, &n, imgs);
   LOGI("  [%s] vkGetSwapchainImagesKHR(list) -> %d (%s)", tag, (int)r, vkres(r));
   if (r != VK_SUCCESS) { fails++; free(imgs); goto out; }
   for (uint32_t k = 0; k < n && k < 8; k++) LOGI("  [%s]   image[%u]=%p", tag, k, (void *)imgs[k]);

   uint32_t idx = 0;
   r = c->d.vkAcquireNextImageKHR(c->dev, sc, 1000000000ULL, VK_NULL_HANDLE, VK_NULL_HANDLE, &idx);
   LOGI("  [%s] vkAcquireNextImageKHR(no sync objects) -> %d (%s) index=%u", tag, (int)r, vkres(r), idx);
   if ((r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) || idx >= n) {
      fails++;
   } else {
      if (do_clear) {
         VkCommandPool pool = VK_NULL_HANDLE;
         VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                         .queueFamilyIndex = c->qfam };
         VkCommandBuffer cmd = VK_NULL_HANDLE;
         if (c->d.vkCreateCommandPool(c->dev, &pci, NULL, &pool) == VK_SUCCESS) {
            VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
               .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
            c->d.vkAllocateCommandBuffers(c->dev, &cbai, &cmd);
            VkCommandBufferBeginInfo cbbi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
               .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
            c->d.vkBeginCommandBuffer(cmd, &cbbi);
            VkImageSubresourceRange rng = { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
               .levelCount = 1, .layerCount = 1 };
            VkImageMemoryBarrier b = {
               .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
               .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
               .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
               .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
               .image = imgs[idx], .subresourceRange = rng,
            };
            c->d.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
            VkClearColorValue cc;
            memset(&cc, 0, sizeof(cc));
            cc.float32[0] = 0.25f; cc.float32[1] = 0.5f; cc.float32[2] = 0.75f; cc.float32[3] = 1.0f;
            c->d.vkCmdClearColorImage(cmd, imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cc, 1, &rng);
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = 0;
            b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            c->d.vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &b);
            c->d.vkEndCommandBuffer(cmd);
            VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
               .commandBufferCount = 1, .pCommandBuffers = &cmd };
            VkResult sr = c->d.vkQueueSubmit(c->queue, 1, &si, VK_NULL_HANDLE);
            LOGI("  [%s] vkQueueSubmit(clear swapchain image %u) -> %d (%s)", tag, idx, (int)sr, vkres(sr));
            if (sr != VK_SUCCESS) fails++;
            c->d.vkQueueWaitIdle(c->queue);
            c->d.vkDestroyCommandPool(c->dev, pool, NULL);
         }
      }
      VkPresentInfoKHR pi = {
         .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
         .swapchainCount = 1, .pSwapchains = &sc, .pImageIndices = &idx,
      };
      r = c->d.vkQueuePresentKHR(c->queue, &pi);
      LOGI("  [%s] vkQueuePresentKHR(image %u) -> %d (%s)", tag, idx, (int)r, vkres(r));
      if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) fails++;
   }
   free(imgs);
out:
   c->d.vkDestroySwapchainKHR(c->dev, sc, NULL);
   return fails;
}

/* ------------------------------------------------------------------ mode: win */
static int mode_win(struct ctx *c)
{
   int fails = 0;
   LOGI("== mode win: AImageReader -> ANativeWindow -> Android surface -> swapchain");
   AImageReader *reader = NULL;
   ANativeWindow *win = NULL;
   media_status_t ms = AImageReader_new(W, H, AIMAGE_FORMAT_RGBA_8888, 8, &reader);
   LOGI("  AImageReader_new(%dx%d RGBA_8888 max=8) -> %d reader=%p", W, H, (int)ms, (void *)reader);
   if (ms != AMEDIA_OK || !reader) return 1;
   ms = AImageReader_getWindow(reader, &win);
   LOGI("  AImageReader_getWindow -> %d win=%p", (int)ms, (void *)win);
   if (ms != AMEDIA_OK || !win) { fails++; goto out; }
   LOGI("  ANativeWindow: %dx%d format=%d", ANativeWindow_getWidth(win),
        ANativeWindow_getHeight(win), ANativeWindow_getFormat(win));

   VkSurfaceKHR surf = VK_NULL_HANDLE;
   VkAndroidSurfaceCreateInfoKHR asci = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
      .window = win,
   };
   VkResult r = c->i.vkCreateAndroidSurfaceKHR(c->instance, &asci, NULL, &surf);
   LOGI("  vkCreateAndroidSurfaceKHR(window) -> %d (%s)", (int)r, vkres(r));
   if (r != VK_SUCCESS) { fails++; goto out; }
   fails += swapchain_steps(c, surf, "win", 1);

   /* drain the ImageReader: whatever the driver copied into the window buffer */
   AImage *ai = NULL;
   ms = AImageReader_acquireNextImage(reader, &ai);
   LOGI("  AImageReader_acquireNextImage -> %d img=%p", (int)ms, (void *)ai);
   if (ms == AMEDIA_OK && ai) {
      uint8_t *data = NULL;
      int len = 0;
      int32_t stride = 0;
      AImage_getPlaneRowStride(ai, 0, &stride);
      if (AImage_getPlaneData(ai, 0, &data, &len) == AMEDIA_OK && data && len >= 4) {
         uint8_t *q = data + ((size_t)(H / 2) * stride) + (W / 2) * 4;
         LOGI("  window pixel(%d,%d) = %3u %3u %3u %3u  stride=%d  (want 64 128 191 255)",
              W / 2, H / 2, q[0], q[1], q[2], q[3], stride);
         if (q[0] == 0 && q[1] == 0 && q[2] == 0) {
            LOGE("  WINDOW PIXEL FAIL (black: the present copy did not reach the buffer)");
            fails++;
         } else {
            LOGI("  WINDOW PIXEL PASS (the presented patch reached the window buffer)");
         }
      } else {
         LOGI("  (plane data unavailable)");
      }
      AImage_delete(ai);
   } else {
      LOGI("  (no image available from the reader: present did not post)");
   }
   c->i.vkDestroySurfaceKHR(c->instance, surf, NULL);
out:
   if (reader) AImageReader_delete(reader);
   return fails;
}

/* ------------------------------------------------------------- mode: headless */
static int mode_headless(struct ctx *c)
{
   int fails = 0;
   LOGI("== mode headless: vkCreateHeadlessSurfaceEXT -> swapchain (no AHB at all)");
   if (!c->i.vkCreateHeadlessSurfaceEXT) {
      LOGE("  vkCreateHeadlessSurfaceEXT not available via gipa (VK_EXT_headless_surface not enabled?)");
      return 1;
   }
   VkSurfaceKHR surf = VK_NULL_HANDLE;
   VkHeadlessSurfaceCreateInfoEXT hci = {
      .sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT };
   VkResult r = c->i.vkCreateHeadlessSurfaceEXT(c->instance, &hci, NULL, &surf);
   LOGI("  vkCreateHeadlessSurfaceEXT -> %d (%s)", (int)r, vkres(r));
   if (r != VK_SUCCESS) return fails + 1;
   fails += swapchain_steps(c, surf, "headless", 0);
   c->i.vkDestroySurfaceKHR(c->instance, surf, NULL);
   return fails;
}

/* --------------------------------------------------------------- mode: mapper */
/* ABI mirror of work/mesa/src/util/u_gralloc/u_gralloc_fallback.c
 * (panvk_v19_mapper / panvk_v19_mapper_v5) - the same assumption the driver makes. */
typedef int32_t (*p10_import_fn)(const void *raw, void **out);
typedef int32_t (*p10_free_fn)(void *h);
typedef int32_t (*p10_getstd_fn)(void *h, int64_t type, void *buf, size_t n);
struct p10_mapper_v5 {
   p10_import_fn importBuffer;
   p10_free_fn freeBuffer;
   void *getTransportSize, *lock, *unlock, *flushLockedBuffer, *rereadLockedBuffer, *getMetadata;
   p10_getstd_fn getStandardMetadata;
};
struct p10_mapper {
   __attribute__((aligned(16))) uint32_t version;
   struct p10_mapper_v5 v5;
};
typedef int32_t (*p10_load_fn)(struct p10_mapper **);

static void hexdump(const char *what, const uint8_t *p, size_t n)
{
   char line[256];
   LOGI("  %s (%zu bytes):", what, n);
   for (size_t i = 0; i < n && i < 512; i += 16) {
      size_t off = (size_t)snprintf(line, sizeof(line), "    %04zu:", i);
      for (size_t k = i; k < i + 16 && k < n; k++)
         off += (size_t)snprintf(line + off, sizeof(line) - off, " %02x", p[k]);
      LOGI("%s", line);
   }
}

static int mode_mapper(void)
{
   int fails = 0;
   LOGI("== mode mapper: reproduce the driver's patched IMapper-V5 metadata path");
   void *bn = dlopen("libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL);
   if (!bn) bn = dlopen("/system/lib64/libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL);
   LOGI("  dlopen(libbinder_ndk.so) -> %p", bn);
   void *handle = NULL;
   const char *how = "NONE";
   if (bn) {
      typedef void *(*open_fn)(const char *, const char *);
      open_fn open_hal = (open_fn)dlsym(bn, "AServiceManager_openDeclaredPassthroughHal");
      LOGI("  dlsym(AServiceManager_openDeclaredPassthroughHal) -> %p", (void *)open_hal);
      if (open_hal) {
         static const char *ifaces[] = { "mapper", "android.hardware.graphics.mapper" };
         static const char *insts[] = { "mediatek", "default" };
         for (unsigned a = 0; a < 2 && !handle; a++)
            for (unsigned b = 0; b < 2 && !handle; b++) {
               handle = open_hal(ifaces[a], insts[b]);
               LOGI("    openDeclaredPassthroughHal(\"%s\",\"%s\") -> %p", ifaces[a], insts[b], handle);
               if (handle) how = "BINDER_NDK";
            }
      }
   }
   if (!handle) {
      void *vs = dlopen("libvndksupport.so", RTLD_NOW | RTLD_LOCAL);
      if (!vs) vs = dlopen("/system/lib64/libvndksupport.so", RTLD_NOW | RTLD_LOCAL);
      LOGI("  dlopen(libvndksupport.so) -> %p", vs);
      if (vs) {
         typedef void *(*load_sphal_fn)(const char *, int);
         load_sphal_fn load = (load_sphal_fn)dlsym(vs, "android_load_sphal_library");
         if (load) {
            handle = load("mapper.mediatek.so", RTLD_NOW | RTLD_LOCAL);
            LOGI("    android_load_sphal_library(\"mapper.mediatek.so\") -> %p", handle);
            if (handle) how = "SPHAL";
         }
      }
   }
   if (!handle) {
      handle = dlopen("/vendor/lib64/hw/mapper.mediatek.so", RTLD_NOW | RTLD_LOCAL);
      const char *e = dlerror();
      LOGI("  dlopen(/vendor/lib64/hw/mapper.mediatek.so) -> %p err=%s", handle, e ? e : "-");
      if (handle) how = "DIRECT";
   }
   if (!handle) {
      LOGE("  MAPPER LOAD FAILED -> the driver's fallback returns -ENOTSUP here");
      return 1;
   }
   LOGI("  mapper impl handle obtained via %s", how);

   p10_load_fn load = (p10_load_fn)dlsym(handle, "AIMapper_loadIMapper");
   LOGI("  dlsym(AIMapper_loadIMapper) -> %p  (NULL => the handle is not a dlopen handle)", (void *)load);
   if (!load) return fails + 1;
   struct p10_mapper *m = NULL;
   int32_t rc = load(&m);
   LOGI("  AIMapper_loadIMapper -> rc=%d mapper=%p version=%u", rc, (void *)m, m ? m->version : 0);
   if (rc || !m || m->version < 5) { LOGE("  mapper unusable"); return fails + 1; }
   if (!m->v5.importBuffer || !m->v5.getStandardMetadata) {
      LOGE("  v5 vtable incomplete: import=%p getstd=%p (vtable offset guess wrong?)",
           (void *)m->v5.importBuffer, (void *)m->v5.getStandardMetadata);
      return fails + 1;
   }

   AHardwareBuffer_Desc desc = {
      .width = W, .height = H, .layers = 1,
      .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
      .usage = AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
               AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
   };
   AHardwareBuffer *ahb = NULL;
   int arc = AHardwareBuffer_allocate(&desc, &ahb);
   LOGI("  AHardwareBuffer_allocate -> rc=%d ahb=%p", arc, (void *)ahb);
   if (arc || !ahb) return fails + 1;
   if (!g_get_native_handle) {
      LOGE("  AHardwareBuffer_getNativeHandle unresolved, cannot importBuffer");
      AHardwareBuffer_release(ahb);
      return fails + 1;
   }
   const void *raw = g_get_native_handle(ahb);
   void *imported = NULL;
   int32_t ir = m->v5.importBuffer(raw, &imported);
   LOGI("  IMapper.importBuffer(raw=%p) -> %d imported=%p", raw, ir, imported);
   if (ir || !imported) { AHardwareBuffer_release(ahb); return fails + 1; }

   static const struct { int64_t id; const char *name; } meta[] = {
      { 5, "LAYER_COUNT" }, { 7, "PIXEL_FORMAT_FOURCC" }, { 8, "PIXEL_FORMAT_MODIFIER" },
      { 10, "ALLOCATION_SIZE" }, { 15, "PLANE_LAYOUTS" },
   };
   for (unsigned k = 0; k < sizeof(meta) / sizeof(meta[0]); k++) {
      int32_t need = m->v5.getStandardMetadata(imported, meta[k].id, NULL, 0);
      LOGI("  getStandardMetadata(%lld %s) size query -> %d", (long long)meta[k].id, meta[k].name, need);
      if (need <= 0 || need > 65536) { fails++; continue; }
      uint8_t *buf = malloc((size_t)need);
      int32_t got = buf ? m->v5.getStandardMetadata(imported, meta[k].id, buf, (size_t)need) : -1;
      LOGI("  getStandardMetadata(%lld %s) -> %d", (long long)meta[k].id, meta[k].name, got);
      if (got > 0 && buf) hexdump(meta[k].name, buf, (size_t)got);
      else fails++;
      free(buf);
   }
   int32_t fr = m->v5.freeBuffer(imported);
   LOGI("  IMapper.freeBuffer -> %d", fr);
   AHardwareBuffer_release(ahb);
   return fails;
}

/* ------------------------------------------------------------------ main */
int main(int argc, char **argv)
{
   const char *icd = "/data/local/tmp/libvulkan_panfrost.so";
   const char *mode = "all";
   int list_only = 0;
   for (int i = 1; i < argc; i++) {
      if (!strncmp(argv[i], "--icd=", 6)) icd = argv[i] + 6;
      else if (!strncmp(argv[i], "--mode=", 7)) mode = argv[i] + 7;
      else if (!strcmp(argv[i], "--list")) list_only = 1;
      else if (!strcmp(argv[i], "--help")) {
         LOGI("usage: %s [--icd=PATH] [--mode=render|tri|ahb|ahbimpdef|win|headless|mapper|all] [--list]",
              argv[0]);
         return 0;
      } else if (argv[i][0] != '-') icd = argv[i];
   }
   setvbuf(stdout, NULL, _IONBF, 0);
   LOGI("=== panvk WSI/u_gralloc probe (task 10) ===");
   LOGI("icd=%s mode=%s", icd, mode);
   LOGI("env: MESA_LOG_LEVEL=%s MESA_DEBUG=%s MESA_VK_WSI_HEADLESS_SWAPCHAIN=%s PANVK_KBASE_DRI3=%s",
        getenv("MESA_LOG_LEVEL") ? getenv("MESA_LOG_LEVEL") : "(unset)",
        getenv("MESA_DEBUG") ? getenv("MESA_DEBUG") : "(unset)",
        getenv("MESA_VK_WSI_HEADLESS_SWAPCHAIN") ? getenv("MESA_VK_WSI_HEADLESS_SWAPCHAIN") : "(unset)",
        getenv("PANVK_KBASE_DRI3") ? getenv("PANVK_KBASE_DRI3") : "(unset)");

   void *lib = dlopen(icd, RTLD_NOW | RTLD_LOCAL);
   LOGI("dlopen(icd) -> %p", lib);
   if (!lib) { LOGE("dlopen failed: %s", dlerror()); return 2; }
   uint32_t (*neg)(uint32_t *) =
      (uint32_t (*)(uint32_t *))dlsym(lib, "vk_icdNegotiateLoaderICDInterfaceVersion");
   if (neg) {
      uint32_t v = 7;
      VkResult nr = (VkResult)neg(&v);
      LOGI("vk_icdNegotiateLoaderICDInterfaceVersion -> %d ver=%u", (int)nr, v);
   }
   g_gipa = (icd_gipa_fn)dlsym(lib, "vk_icdGetInstanceProcAddr");
   if (!g_gipa) { LOGE("vk_icdGetInstanceProcAddr missing"); return 2; }
   g_get_native_handle = (get_native_handle_fn)dlsym(RTLD_DEFAULT, "AHardwareBuffer_getNativeHandle");
   if (!g_get_native_handle) {
      void *nw = dlopen("libnativewindow.so", RTLD_NOW | RTLD_LOCAL);
      if (nw) g_get_native_handle = (get_native_handle_fn)dlsym(nw, "AHardwareBuffer_getNativeHandle");
   }
   LOGI("AHardwareBuffer_getNativeHandle -> %p", (void *)g_get_native_handle);

   if (!strcmp(mode, "mapper")) return mode_mapper();

   struct ctx c;
   memset(&c, 0, sizeof(c));

   int want_all = !strcmp(mode, "all");
   int want_win = want_all || !strcmp(mode, "win");
   int want_headless = want_all || !strcmp(mode, "headless");
   int want_render = want_all || !strcmp(mode, "render") || !strcmp(mode, "tri");
   int want_ahb = want_all || !strcmp(mode, "ahb") || !strcmp(mode, "ahbimpdef");

   VkResult r = create_instance(want_win, want_headless, &c.instance);
   if (r != VK_SUCCESS) { LOGE("FATAL vkCreateInstance -> %d (%s)", (int)r, vkres(r)); return 1; }
#define GI(name) do { c.i.name = (PFN_##name)g_gipa(c.instance, #name); \
   if (!c.i.name) { LOGE("  !! missing instance entrypoint %s", #name); return 1; } } while (0);
   INST_FNS(GI)
#undef GI

   uint32_t nd = 0;
   r = c.i.vkEnumeratePhysicalDevices(c.instance, &nd, NULL);
   LOGI("vkEnumeratePhysicalDevices(count) -> %d (%s) n=%u", (int)r, vkres(r), nd);
   if (r != VK_SUCCESS || nd == 0) { LOGE("FATAL no physical device"); return 1; }
   VkPhysicalDevice pds[8];
   if (nd > 8) nd = 8;
   c.i.vkEnumeratePhysicalDevices(c.instance, &nd, pds);
   c.phys = pds[0];
   for (uint32_t k = 0; k < nd; k++) {
      VkPhysicalDeviceProperties p;
      c.i.vkGetPhysicalDeviceProperties(pds[k], &p);
      LOGI("  phys[%u]: \"%s\" vendor=0x%04x device=0x%04x api=%u.%u driver=%u",
           k, p.deviceName, p.vendorID, p.deviceID, VK_VERSION_MAJOR(p.apiVersion),
           VK_VERSION_MINOR(p.apiVersion), p.driverVersion);
   }
   c.i.vkGetPhysicalDeviceProperties(c.phys, &c.props);
   if (list_only) { list_extensions(c.phys, c.i.vkEnumerateDeviceExtensionProperties); return 0; }

   r = create_device(&c, want_win || want_headless);
   if (r != VK_SUCCESS) { LOGE("FATAL vkCreateDevice -> %d (%s)", (int)r, vkres(r)); return 1; }
   if (!load_dev_fns(&c)) return 1;
   list_extensions(c.phys, c.i.vkEnumerateDeviceExtensionProperties);

   int fails = 0;
   if (want_render) {
      const int is_tri = !strcmp(mode, "tri");
      const int is_render = !strcmp(mode, "render");
#ifdef HAVE_TRIANGLE
      if (want_all || is_tri) fails += mode_triangle(&c);
#else
      if (is_tri) { LOGE("built without HAVE_TRIANGLE: tri unavailable"); fails++; }
#endif
      if (want_all || is_render) fails += mode_render(&c);
   }
   if (want_ahb) {
      fails += mode_ahb(&c, !strcmp(mode, "ahbimpdef"));
      if (want_all) fails += mode_ahb(&c, 1);
   }
   if (want_headless) fails += mode_headless(&c);
   if (want_win) fails += mode_win(&c);
   if (want_all) fails += mode_mapper();

   LOGI("=== SUMMARY mode=%s failures=%d ===", mode, fails);
   LOGI("reminder: logcat tag MESA for 'Using fallback gralloc implementation',"
        " 'P0A-V19-FULLPLANE', 'Failed to get u_gralloc_buffer_basic_info'");
   return fails ? 1 : 0;
}
```


---

## 5. 编译（在服务器上，本轮已实测通过）

前置：`/opt/android-ndk-r27c`（已有 `aarch64-linux-android35-clang`）、`/usr/bin/glslangValidator`（已有）、`python3`。

```bash
# 全部文件都在 /root/research/probe10/
cd /root/research/probe10

# 一条命令即可（脚本内部：glslangValidator 编 shader → python3 生成 SPIR-V 头 → clang 编译）
bash build.sh
```

`build.sh` 展开后的真实命令（等价手工版）：

```bash
NDK=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64
CC=$NDK/bin/aarch64-linux-android35-clang

# 1) 着色器 → SPIR-V → C 头（tri.vert.spv.h / tri.frag.spv.h，uint32_t 数组，避免对齐问题）
glslangValidator -V tri.vert -o tri.vert.spv
glslangValidator -V tri.frag -o tri.frag.spv
python3 - <<'PY'
import struct
def emit(name, src, dst):
    d = open(src,'rb').read(); assert len(d)%4==0
    w = struct.unpack('<%dI'%(len(d)//4), d)
    with open(dst,'w') as f:
        f.write('static const unsigned int %s[] = {\n'%name)
        for i in range(0,len(w),8):
            f.write('   '+','.join('0x%08xu'%x for x in w[i:i+8])+',\n')
        f.write('};\nstatic const unsigned int %s_LEN = %d;\n'%(name,len(d)))
emit('TRI_VERT_SPV','tri.vert.spv','tri.vert.spv.h')
emit('TRI_FRAG_SPV','tri.frag.spv','tri.frag.spv.h')
PY

# 2) 探针（不含 tri 时去掉 -DHAVE_TRIANGLE）
$CC -O2 -g -Wall -DHAVE_TRIANGLE -o panvk_wsi_probe panvk_wsi_probe.c \
    -ldl -llog -landroid -lmediandk -lnativewindow
```

本轮实测结果（原文）：

```
panvk_wsi_probe: ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV),
  dynamically linked, interpreter /system/bin/linker64, for Android 35,
  built by NDK r27c (12479018), with debug_info, not stripped
-rwxr-xr-x 1 root root 274064 panvk_wsi_probe
BUILD OK
NEEDED: libdl.so liblog.so libandroid.so libmediandk.so libnativewindow.so libc.so
UND(非 libc): AHardwareBuffer_allocate/describe/release, AImage_delete,
  AImage_getPlaneData, AImage_getPlaneRowStride, AImageReader_acquireNextImage/
  delete/getWindow/new, ANativeWindow_getFormat/getHeight/getWidth
```

* `-Wall` 无警告；**不链接 libvulkan**（ICD 只导出 3 个符号，必须运行时 dlopen）。
* 未定义符号全部是公有 NDK API（`libandroid`/`libmediandk`/`libnativewindow`/`liblog`），设备上一定存在。
* `AHardwareBuffer_getNativeHandle` 故意**不**做链接期引用（NDK 头里没有它），运行时 `dlsym(RTLD_DEFAULT, …)` 取；取不到时 mapper 模式会如实报错而不是崩。

---

## 6. 部署与运行（无 root）

```bash
# --- 0) 只推这两个文件；千万不要在 .so 旁边建 android_stub/ 目录 ---
#        否则 RUNPATH=$ORIGIN/../../android_stub 会命中桩库，
#        而桩里的 AHardwareBuffer_describe() 是空实现、getNativeHandle() 返回 NULL（05 号文档已实测警告）
adb push /root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so /data/local/tmp/
adb push /root/research/probe10/panvk_wsi_probe                        /data/local/tmp/
adb shell chmod 755 /data/local/tmp/panvk_wsi_probe
adb shell md5sum /data/local/tmp/libvulkan_panfrost.so
#   判据：= 4417b369591fc2b3df27e22019ccf3a2

# --- 1) 先清 logcat，再跑，再抓驱动日志 ---
adb shell logcat -c
adb shell 'cd /data/local/tmp && \
  MESA_LOG_LEVEL=info ./panvk_wsi_probe --icd=/data/local/tmp/libvulkan_panfrost.so --mode=all' \
  | tee /tmp/probe-all.out
adb shell 'logcat -d -s MESA:V P10PROBE:V *:S' > /tmp/mesa.log
```

说明：

* 运行者必须是 **uid 2000(shell)** 或 App：`/dev/mali0` 是 `crw-rw-rw-`，shell 可打开（§1.3）。
* `MESA_LOG_LEVEL=info` 其实**不必**（默认就是 INFO，`MESA_DEBUG=0`），写上更稳；想更啰嗦用 `MESA_LOG_LEVEL=debug`。
  `setprop debug.mesa.log.level info` 在本机（Enforcing，`debug.*` 属性无对应 SELinux 标签）**很可能被拒**，优先用环境变量。
* `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1` 是 wsi_common 的强制开关（`wsi_common.c:297-298`、`:1440-1441`）：**即使 surface 是 Android 类型也走 headless swapchain**，可用来把「WSI 框架层」与「AHB 路径」彻底分离。
* 单个 mode 用 `--mode=render|tri|ahb|ahbimpdef|headless|win|mapper`。

---

## 7. 各 mode 的预期输出与判据

### 7.1 `--mode=render`（**兜底第一枪**，不涉及任何 WSI）
期望依次出现（全 SUCCESS）：
```
vkCreateInstance -> 0 (VK_SUCCESS)
phys[0]: "Mali-G720-Immortalis" vendor=0x13b5 device=0x… api=1.4
vkCreateDevice(qfam=0) -> 0 (VK_SUCCESS)
  vkCreateImage(own, optimal, 64x64 RGBA8, color|src|dst) -> 0 (VK_SUCCESS)
  image mem req: size=…  typeBits=0x…  align=…
  vkAllocateMemory(image) -> 0 (VK_SUCCESS)
  vkBindImageMemory -> 0 (VK_SUCCESS)
  [render] vkQueueSubmit(clear+copy) -> 0 (VK_SUCCESS)
  [render] vkWaitForFences(5s) -> 0 (VK_SUCCESS)
  [render] pixel(0,0) = 64 128 191 255 (want 64 128 191 255)      <- 三个采样点
  [render] PIXEL PASS (clear colour read back exactly)
=== SUMMARY mode=render failures=0 ===
```
判据：`failures=0` 且 `PIXEL PASS`。
若出现 `PIXEL PASS (R/B swapped…)` **也算通过**（gralloc 侧 ABGR vs VK 侧 RGBA 的通道序差异），但要在报告里记下通道序。
若 `PIXEL FAIL (all zero)` → 停止 WSI 方向排查，问题在「提交/同步/内存」而不在 WSI。

### 7.2 `--mode=tri`（更强的「真渲染」证据）
```
  vkCreateRenderPass -> 0 / vkCreateFramebuffer -> 0
  vkCreateShaderModule(vert/frag) -> 0 / vkCreateGraphicsPipelines -> 0
  [tri] vkQueueSubmit(draw) -> 0 (VK_SUCCESS)
  [tri] corner(0,0)    =  64 128 191 255   (want clear 64 128 191 255)
  [tri] centre(32,32)  = 255   0   0 255   (want red 255 0 0 255, or BGR 0 0 255)
  [tri] PIXEL PASS (GPU rasterised the triangle and the clear survived)
```
判据：中心=红/蓝、角落=清屏色 ⇒ CSF 固件真的光栅化了图元（不是只做了一次 clear）。

### 7.3 `--mode=ahb`（**单独验证 u_gralloc 那一环，无 Surface**）
```
  AHardwareBuffer_allocate(fmt=0x1 usage=0x…290) -> rc=0 ahb=0x…
  AHardwareBuffer_describe: w=64 h=64 stride=64 fmt=0x1 usage=0x… layers=1
  native handle: version=12 numFds=1 numInts=…      <- fd 的大小会打印出来
  vkCreateImage(EXTERNAL AHB handleType) -> 0 (VK_SUCCESS)
  vkGetAndroidHardwareBufferPropertiesANDROID -> 0 (VK_SUCCESS) allocSize=… typeBits=0x… format=R8G8B8A8_UNORM
  >>> vkAllocateMemory(import AHB, dedicated image) -> <r>   <== the u_gralloc step
  vkBindImageMemory(ahb) -> <r>
  [ahb] … PIXEL PASS …
```
判据：
* `vkAllocateMemory` → `VK_SUCCESS` ⇒ **u_gralloc 这一环通了**，接着 `BindingImageMemory` + 回读应 PASS。
* `vkAllocateMemory` → `VK_ERROR_INVALID_EXTERNAL_HANDLE` ⇒ 就是当前卡点，**转到 §8 用 logcat 定位到子步骤**。

### 7.4 `--mode=ahbimpdef`
`AHardwareBuffer_allocate(fmt=0x22 …)`；若本机 allocator 接受该格式，则
`vkGetAndroidHardwareBufferPropertiesANDROID` 会直接 `VK_ERROR_INVALID_EXTERNAL_HANDLE`
（因为 `vk_android.c:1066-1076` 的 external-format 分支必须问 u_gralloc）。
判据：这能证明「u_gralloc 在 common properties 路径上确实被调用且失败」——与 7.3 组合即可把失败点夹在两步之间。

### 7.5 `--mode=headless`
```
  vkCreateHeadlessSurfaceEXT -> 0 (VK_SUCCESS)
  [headless] vkGetPhysicalDeviceSurfaceSupportKHR(qfam=0) -> 0 (VK_SUCCESS) supported=1
  [headless] vkGetPhysicalDeviceSurfaceCapabilitiesKHR -> 0 (VK_SUCCESS)
  [headless]   minImageCount=2 maxImageCount=4 currentExtent=64x64 …
  [headless] vkGetPhysicalDeviceSurfaceFormatsKHR(count) -> 0 n=3
  [headless] vkGetPhysicalDeviceSurfacePresentModesKHR(count) -> 0 n=3
  [headless] >>> vkCreateSwapchainKHR(…) -> 0 (VK_SUCCESS)
  [headless] vkGetSwapchainImagesKHR(count) -> 0 n=2
  [headless] vkAcquireNextImageKHR(no sync objects) -> 0 index=0
  [headless] vkQueuePresentKHR(image 0) -> 0 (VK_SUCCESS)
```
判据：全 SUCCESS ⇒ **WSI 框架层/swapchain 生命周期是好的**，卡点被严格限定在 AHB/dma-buf 导入这一段。

### 7.6 `--mode=win`（当前卡点所在）
```
  AImageReader_new(64x64 RGBA_8888 max=8) -> 0 reader=0x…
  AImageReader_getWindow -> 0 win=0x…
  ANativeWindow: 64x64 format=1
  vkCreateAndroidSurfaceKHR(window) -> 0 (VK_SUCCESS)
  [win] vkGetPhysicalDeviceSurfaceSupportKHR(qfam=0) -> 0 supported=1
  [win] vkGetPhysicalDeviceSurfaceCapabilitiesKHR -> 0  (minImageCount=2 maxImageCount=4 currentExtent=64x64)
  [win] vkGetPhysicalDeviceSurfaceFormatsKHR(count) -> 0 n=3
  [win] vkGetPhysicalDeviceSurfacePresentModesKHR(count) -> 0 n=3
  [win] >>> vkCreateSwapchainKHR(minImageCount=2 64x64 RGBA8 FIFO) -> -1000011001 (VK_ERROR_INVALID_EXTERNAL_HANDLE)
```
判据：`vkCreateSwapchainKHR` 的返回值就是最终结论。若为 SUCCESS，继续看
`vkGetSwapchainImagesKHR` → `vkAcquireNextImageKHR` → `vkQueuePresentKHR` → `window pixel(...)`；
`WINDOW PIXEL PASS` 表示 `panvk_wsi.c:261-300` 的 CPU 拷贝（AHB lock → window lock → memcpy → unlockAndPost）真的把像素送到了 BufferQueue。

### 7.7 `--mode=mapper`（**不需要 Vulkan，也不需要 GPU 能跑**）
```
  dlopen(libbinder_ndk.so) -> 0x…
  dlsym(AServiceManager_openDeclaredPassthroughHal) -> 0x…
    openDeclaredPassthroughHal("mapper","mediatek") -> 0x…        <- 期望这里就有句柄
  mapper impl handle obtained via BINDER_NDK
  dlsym(AIMapper_loadIMapper) -> 0x…
  AIMapper_loadIMapper -> rc=0 mapper=0x… version=5
  AHardwareBuffer_allocate -> rc=0 ahb=0x…
  IMapper.importBuffer(raw=0x…) -> 0 imported=0x…
  getStandardMetadata(5 LAYER_COUNT) -> 8   (+ hexdump)
  getStandardMetadata(7 PIXEL_FORMAT_FOURCC) -> …  (+ hexdump)
  getStandardMetadata(8 PIXEL_FORMAT_MODIFIER) -> … (+ hexdump)
  getStandardMetadata(10 ALLOCATION_SIZE) -> …     (+ hexdump)
  getStandardMetadata(15 PLANE_LAYOUTS) -> …       (+ hexdump)
```
判据：这五项 metadata 的**原始字节**是修 `panvk_v19_query_mapper()` 解析逻辑的唯一依据
（fork 里 `panvk_v19_get_scalar()` 假定「字符串 + i64 类型 + 恰好 width 字节的裸值」，若实际是 `Extension{name,value}` 之类就会解析失败并返回 -EINVAL）。

---

## 8. 「u_gralloc 是否被调用、失败在哪一步」——logcat 证据表

抓取：`adb shell logcat -d -s MESA:V *:S`（tag 是 `MESA`，来自 `util/log.h:37` 的 `MESA_LOG_TAG`；`.so` 内亦可见字面量 `MESA`）。

按出现顺序，**每条日志对应一个确定的子步骤**：

| logcat（tag `MESA`） | 来源 | 含义 / 下一步 |
|---|---|---|
| `Using fallback gralloc implementation` | `u_gralloc_fallback.c:470` | 已确定落在 FALLBACK（CROS/LIBDRM/QCOM 全灭） |
| `No gralloc hwmodule detected (video buffers won't be supported)` | `u_gralloc_fallback.c:456` | `hw_get_module` 是桩（`hardware_stub.cpp:6-11`），符合预期 |
| `[P0A-V19-FULLPLANE] mapper load failed` | `u_gralloc_fallback.c:110` | 三条加载路由全失败 → mapper 模式可复现（SELinux/命名空间问题） |
| `[P0A-V19-FULLPLANE] AIMapper_loadIMapper absent` | `:115` | 句柄拿到了但 `dlsym` 失败 → 句柄不是 dlopen 句柄（路由用错）或库版本不符 |
| `[P0A-V19-FULLPLANE] init how=%s rc=%d mapper=%p version=%u` | `:120` | **关键**：`how` ∈ BINDER_PASSTHROUGH/SPHAL/DIRECT 说明哪条路通了；`version` 必须 ≥5 |
| `[P0A-V19-FULLPLANE] import rc=%d` | `:283` | `IMapper.importBuffer(native_handle)` 失败 → AHB 句柄/权限问题 |
| `[P0A-V19-FULLPLANE] metadata layer_rc=… fourcc_rc=… modifier_rc=… alloc_rc=… planes_rc=…` | `:301-305` | **关键**：哪个 rc≠0 就是哪个 metadata 解不出来（对照 `--mode=mapper` 的 hexdump 修 parser） |
| `[P0A-V19-FULLPLANE] accepted fourcc=0x… modifier=0x… planes=%d` | `:311` | 全通 → 这一环其实是好的，失败在更后面（`GetMemoryFdPropertiesKHR` / mem type 不匹配） |
| `[P0A-V19-FULLPLANE] complete metadata unavailable rc=%d; refusing guessed layout` | `:429` | FALLBACK 主动 fail-closed ⇒ 返回给 `vk_android_get_ahb_layout()` |
| `Failed to get u_gralloc_buffer_basic_info` | `vk_android.c:1075` | 环闭合：u_gralloc 失败被翻译成 `VK_ERROR_INVALID_EXTERNAL_HANDLE` |
| `u_gralloc_get_buffer_basic_info failed` | `vk_android.c:151` | 另一处调用点（`vk_gralloc_to_drm_explicit_layout` 直调路径），含义同上 |
| `AHB (format=%u,usage=0x%llx) has no GPU usage` / `Unsupported AHB desc.layers(%u) > 1` | `vk_android.c:1019/1028` | 说明卡在 common 的参数校验，与 u_gralloc/WSI 无关 |
| （panvk 自己的 `panvk_errorf`）`No compatible mem type: img req (…), fd req (…)` | `panvk_android.c:78/104` | u_gralloc 通了但 dma-buf 与图像的 memoryTypeBits 不交集 → 是 kbase 导入/内存类型问题，不是 u_gralloc |

> 结论性判读：**`mapper load failed` 与 `metadata *_rc≠0` 是两种完全不同的病**，前者是权限/域名问题（改用 App 上下文或换加载路由），后者是解析/元数据问题（对照 hexdump 改 `panvk_v19_query_mapper`）。**不要在这两者之间凭猜。**

---

## 9. 兜底验证路径：WSI 短期打不通时，如何证明「驱动能正确渲染」

按证据强度从弱到强，三步都**不需要** Surface / swapchain / AHardwareBuffer 的 WSI 路径：

**第 1 步（最小、必做）`--mode=render`**
1. `vkCreateInstance`（启用 `VK_KHR_surface`）→ 打印 `VkResult`；
2. 枚举物理设备，打印 `deviceName/vendorID/deviceID/apiVersion`（判据：出现 Mali，**不能**是 lavapipe）；
3. `vkCreateDevice`（选 GRAPHICS 队列族）+ 加载设备函数表；
4. `vkCreateImage`（64×64 RGBA8，OPTIMAL，COLOR_ATTACHMENT|TRANSFER_SRC|TRANSFER_DST）；
5. `vkGetImageMemoryRequirements` → 选 memoryType → `vkAllocateMemory` → `vkBindImageMemory`；
6. 一个 command buffer：`UNDEFINED→TRANSFER_DST` 屏障 → `vkCmdClearColorImage(0.25,0.5,0.75,1.0)` → `TRANSFER_DST→TRANSFER_SRC` → `vkCmdCopyImageToBuffer` 到 HOST_VISIBLE|HOST_COHERENT buffer → BUFFER 屏障到 HOST_READ；
7. `vkQueueSubmit` + `vkWaitForFences(5s)`（超时=5e9 ns，不会挂死）；
8. `vkMapMemory` → 校验 3 个采样点 = `64,128,191,255`（±2；R/B 交换也算渲染成功，但要记录通道序）。
预期输出即 §7.1。**这一关过了，「驱动能提交并正确渲染」就已经被证明了**，后续 WSI 的所有失败都不会再牵扯"驱动是否能画"。

**第 2 步（更强）`--mode=tri`**：同一 image 上走 `vkCreateRenderPass`（loadOp=CLEAR）→「
`vkCreateShaderModule`(内嵌 SPIR-V，`build.sh` 用 glslangValidator 生成) → `vkCreateGraphicsPipelines`
→ `vkCmdBeginRenderPass/vkCmdBindPipeline/vkCmdDraw(3,1,0,0)/vkCmdEndRenderPass` → `CopyImageToBuffer` → 回读；
判据：中心像素=红（或 BGR 蓝）、角落=清屏色。这证明 **CSF 固件真的执行了光栅化管线**（比 clear 强）。

**第 3 步（把 WSI 层单独摘出来）`--mode=headless`**：`vkCreateHeadlessSurfaceEXT` + `--mode=win` 的 caps/formats/modes/swapchain 全套，
不碰 AHardwareBuffer。判据见 §7.5：全通 ⇒ 问题**只**在 AHB/dma-buf 导入。

**如果 render 也失败**，失败点就落在下表（探针会精确打印到行）：

| 现象 | 定位 |
|---|---|
| `FATAL vkCreateInstance` / `no physical device` | kbase 打不开（`/dev/mali0` 权限/CSF 固件）→ `vkCreateInstance` 前的日志会带 `failed to open kbase device`（panvk 侧 `VK_ERROR_INCOMPATIBLE_DRIVER`） |
| `vkCreateDevice` 非 0 | 队列族/特性协商 |
| `vkCreateImage` / `vkGetImageMemoryRequirements` 失败 | panvk 图像层（layout/modifier handler） |
| `vkAllocateMemory(image)` 失败 | kbase BO 分配 |
| `vkQueueSubmit` / `vkWaitForFences` 失败或超时 | CSF 提交/同步（结合 `PANVK_DEBUG`、kbase 探针 `tests/kbase-probe/`） |
| 提交成功但像素全 0 | 回读路径或 cache 一致性（把 `vkCmdPipelineBarrier` 换 `vkCmdCopyImageToBuffer` 后加 `vkQueueWaitIdle` 再 map 复核） |

---

## 10. 只能人工在手机上做的步骤（本会话不假设能操作真机）

本会话对手机**只做了只读读取**（`getprop`/`ls`/`grep`/`lshal`，经 Shizuku uid 2000），**没有**安装、没有改设置、没有 push 文件。以下必须由人执行（屏幕权限可能是 virtual-only，且需要真实文件传输）：

1. 打开 USB 调试并授权主机（或把两个文件放到手机可访问位置：`libvulkan_panfrost.so` + `panvk_wsi_probe`）。
2. 推送与赋权：`adb push … /data/local/tmp/`、`chmod 755`。
3. 运行探针、抓 `logcat`（`logcat -c` / `logcat -d -s MESA:V P10PROBE:V *:S`）。**日志是本次验证的核心证据**，必须完整回传。
4. 若 shell 域被 SELinux 挡住 mapper：用仓库里现成的 App 探针 `tests/android-loader-app/`（`build-apk.sh` → `panvk-loader-test.apk`）安装运行，对比同一批日志（`untrusted_app` 域 vs `shell` 域）。这一步**必须人工在手机上安装/启动**。
5. 如果要在非 shell 域做更细的对比，需要在 App 里执行探针逻辑（把 `panvk_wsi_probe.c` 编成 `.so`，JNI 入口 `ANativeWindow_fromSurface` 传 window）——属于人工步骤。
6. 任何涉及 `/vendor`、`setprop`、重启、清数据、卸载 App 的动作，本报告一律不建议/不要求。

---

## 11. 最小可执行验证清单（按顺序，每步带命令与判据）

| # | 命令（设备上，`/data/local/tmp`） | 判据（PASS） | 失败则 |
|---|---|---|---|
| 0 | `adb push` 两个文件；`md5sum /data/local/tmp/libvulkan_panfrost.so` | `4417b369591fc2b3df27e22019ccf3a2` | 传输/产物不对，全部作废 |
| 1 | `logcat -c` | —— | —— |
| 2 | `MESA_LOG_LEVEL=info ./panvk_wsi_probe --mode=render` | `SUMMARY … failures=0` 且 `[render] PIXEL PASS` | 转 §9 末表；**不要**继续 WSI |
| 3 | `./panvk_wsi_probe --mode=tri` | 中心红/蓝 + 角落清屏色 + `PIXEL PASS` | 记录（可能是 pipeline 细节），不阻塞 4/5 |
| 4 | `./panvk_wsi_probe --mode=ahb` | `vkAllocateMemory(import AHB…) -> 0` + `vkBindImageMemory -> 0` + `PIXEL PASS` | `VK_ERROR_INVALID_EXTERNAL_HANDLE` ⇒ 走第 5、6 步定位 |
| 5 | `./panvk_wsi_probe --mode=mapper` | 某条路由给出句柄、`version=5`、5 个 metadata 都有正 size 且有 hexdump | 无句柄 ⇒ 权限/路由问题；有句柄但 metadata 解不出 ⇒ 记录 hexdump 去改 parser |
| 6 | `logcat -d -s MESA:V *:S` | 按 §8 表逐条对照，确定 `init how=/import rc=/metadata *_rc=` 哪一个非 0 | 按 §8 右列行动 |
| 7 | `./panvk_wsi_probe --mode=ahbimpdef` | properties 查询返回 `INVALID_EXTERNAL_HANDLE`（证明 u_gralloc 在 common 路径被调用） | 若 properties 反而成功，说明本机把 0x22 归一化成已知格式，改看第 4 步 |
| 8 | `./panvk_wsi_probe --mode=headless` | `vkCreateHeadlessSurfaceEXT -> 0` + `vkCreateSwapchainKHR -> 0` | 若这里也失败 ⇒ WSI 框架层问题，与 AHB 无关 |
| 9 | `./panvk_wsi_probe --mode=win` | `vkCreateSwapchainKHR -> 0`（当前预期 `-1000011001`）→ 再看 acquire/present/`WINDOW PIXEL PASS` | 记录精确返回码；随后进 §12 的「下一步候选」 |

> 顺序的理由：2 先把「驱动能不能画」钉死；4/5 把「u_gralloc 环」独立钉死；8 把「WSI 框架」钉死；9 才回到卡点本身。
> 这样即使 9 仍失败，也已经能明确回答「失败是 AHB 导入一环，而不是驱动或 WSI 框架」。

---

## 12. 未验证 / 风险（诚实标注）

1. **探针只做到「编译通过 + 静态检查」**，本轮**没有在真机上运行过**（本会话不假设能自动操作真机；见 §10）。所有 §7 的预期输出是按源码推导的，未实测。
2. **`--mode=tri` 的运行时正确性未经真机验证**（渲染通道/管线参数是标准写法，但没跑过）。若它异常而 `--mode=render` 通过，请以 `render` 为准，并对照仓内 `tests/offscreen/triangle.c` 交叉验证。
3. `--mode=mapper` 里的 IMapper V5 vtable 布局（`importBuffer/freeBuffer/getTransportSize/lock/unlock/flushLockedBuffer/rereadLockedBuffer/getMetadata/getStandardMetadata`）是**照抄 fork 自己的 `panvk_v19_mapper_v5`**（`u_gralloc_fallback.c:64-71`），并非从 AIDL 头文件核对。若该假设错，探针会在 `getStandardMetadata` 处崩溃（`importBuffer` 已经能证明布局前几项）；**请在 `logcat` 里留意 tombstone**，这本身也是有价值的结论。
4. `metadata` 类型号 5/7/8/10/15 同样是照抄 fork（`:288-292`），未与 AIDL `StandardMetadataType` 枚举核对。
5. shell 域（`u:r:shell:s0`）能否 `dlopen` `/vendor/lib64/hw/mapper.mediatek.so`（`same_process_hal_file`）**未证实**——这是当前最大的未知，探针的 `mapper load failed` 一句话即可判定；`libui.so` 用同构写法是强旁证，但不是证明。
6. 本轮未验证 IMapper 返回的 modifier 具体值（`0x0` LINEAR / AFBC / …），这直接决定 panvk 侧要不要加 MTK modifier handler——**`--mode=mapper` 的 hexdump 正是为此**。
7. 未评估「改 meson 让 imapper4/5 后端真正编进来」的代价（那需要 `dep_android_ui`/`dep_android_mapper4` 与 AIDL graphics-common 头，见 04 号文档 §，且与 `android-stub=true` 的部署约定冲突）；本报告只提供**不改一行代码**的验证手段。

---

## 13. 可复现命令索引（本报告所有事实的取证命令）

```bash
# 产物与配置
md5sum /root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so \
       /root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so
readelf -d /root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so
readelf --dyn-syms -W …libvulkan_panfrost.so | awk '$7!="UND"{print $8}'
grep -n '' /root/zenithblue/build/android-v4/meson-logs/meson-log.txt | sed -n '3p'
ls /root/zenithblue/build/android-v4/src/util/u_gralloc/lib_mesa_u_gralloc.a.p/
strings -a …libvulkan_panfrost.so | grep -F 'P0A-V19-FULLPLANE'
# 源码（本 fork）
grep -n 'imapper' /root/zenithblue/work/mesa/src/util/u_gralloc/meson.build
sed -n '6,11p' /root/zenithblue/work/mesa/src/android_stub/hardware_stub.cpp
sed -n '25,34p' /root/zenithblue/work/mesa/src/util/u_gralloc/u_gralloc.c
sed -n '72,131p;277,311p;367,448p;451,473p' /root/zenithblue/work/mesa/src/util/u_gralloc/u_gralloc_fallback.c
sed -n '323,455p' /root/zenithblue/work/mesa/src/panfrost/vulkan/panvk_wsi.c
sed -n '405,458p' /root/zenithblue/work/mesa/src/panfrost/vulkan/panvk_android.c
sed -n '141,160p;670,690p;1060,1080p' /root/zenithblue/work/mesa/src/vulkan/runtime/vk_android.c
sed -n '300,325p' /root/zenithblue/work/mesa/src/vulkan/runtime/vk_instance.c
sed -n '36,53p' /root/zenithblue/work/mesa/src/util/log.h
grep -n 'EXT_headless_surface' /root/zenithblue/work/mesa/src/panfrost/vulkan/panvk_instance.c
# 设备（只读）
adb shell getprop ro.product.model / ro.board.platform / ro.build.version.sdk
adb shell ls -lZ /dev/mali0
adb shell ls -l /vendor/lib64/hw/ | grep -E 'mapper|gralloc|allocator'
adb shell cat /vendor/etc/vintf/manifest/mapper.mediatek.xml
adb shell 'grep -ac AIMapper_loadIMapper /vendor/lib64/hw/mapper.mediatek.so'
adb shell 'grep -ac AServiceManager_openDeclaredPassthroughHal /system/lib64/libbinder_ndk.so'
adb shell 'grep -ac AIMapper_loadIMapper /system/lib64/libui.so'
adb shell 'grep -ac AHardwareBuffer_getNativeHandle /system/lib64/libnativewindow.so'
adb shell lshal | grep -i mapper
```

---

## 摘要（≤14 行）

1. 产物已核对：`android-v4` 的 `.so` 与 dist 版 md5 一致（`4417b369…`），**只导出 3 个 ICD 符号** ⇒ 验证必须 dlopen + `vk_icdGetInstanceProcAddr`（无 root）。
2. 卡点精确定位：失败在 `vkCreateSwapchainKHR` 第 4 步 `AllocateMemory(import AHB)` 内部的 `vk_android_get_ahb_layout` → `u_gralloc_get_buffer_basic_info`；**R8G8B8A8 的 properties 查询不碰 u_gralloc**（只有 IMPLEMENTATION_DEFINED 才碰）。
3. 该 `.so` 的 u_gralloc 只有 CROS/LIBDRM/QCOM/FALLBACK 四个后端：`android-stub=true` 使 **imapper4/5 压根没编**（`.a.p` 无对应 `.o`），且桩 `hw_get_module` 恒 `-ENOENT` ⇒ **必然落到 fork 改写的 FALLBACK → binder-NDK IMapper V5**，它失败即 fail-closed（`refusing guessed layout`）。
4. 手机事实（只读实测）：PHZ110/mt6989/SDK36；`/dev/mali0` 是 `crw-rw-rw-` ⇒ shell 无 root 可建 instance/device；VINTF 里接口名就是 `mapper`、实例 `mediatek`，`mapper.mediatek.so` 含 `AIMapper_loadIMapper`，`libui.so` 用同构写法 ⇒ fork 的加载参数**与 manifest 一致**。
5. 交付物：`/root/research/probe10/panvk_wsi_probe.c`（1394 行，**NDK r27c 编译通过、`-Wall` 无警告**）+ `build.sh` + `tri.vert/frag`；本文件含完整源码。
6. 七个 mode：`render`（无 surface，证明驱动能画）/`tri`（真光栅化）/`ahb`（**单独复现 u_gralloc 环**）/`ahbimpdef`/`headless`（WSI 框架单独验证）/`win`（AImageReader 的 Surface，即卡点步骤）/`mapper`（不碰 Vulkan，直接 hexdump 厂商 IMapper 的 5 项 metadata）。
7. `--mode=win` 用 `AImageReader_getWindow` 拿 `ANativeWindow`（公有 NDK，**不需要 Java/Activity**），每步打印 `VkResult`；`headless` 用 `vkCreateHeadlessSurfaceEXT`（`.so` 里确实带了该扩展）。
8. 「u_gralloc 是否被调用、失败在哪一步」有确定的 logcat 判据（tag `MESA`，默认 INFO 就会打）：`mapper load failed` / `init how= rc= version=` / `import rc=` / `metadata *_rc=` / `refusing guessed layout` / `Failed to get u_gralloc_buffer_basic_info`——§8 给了逐条对应表与分支行动。
9. 兜底证明「驱动能渲染」：`--mode=render` 八步（image→clear→CopyImageToBuffer→map）校验像素 `64,128,191,255`（R/B 交换也算过但需记录通道序）；更强的是 `--mode=tri`（中心红/角落清屏色）。
10. 顺序建议：先 `render`（钉死驱动）→ 再 `ahb` + `mapper`（钉死 u_gralloc 环）→ 再 `headless`（钉死 WSI 框架）→ 最后 `win`（卡点）。
11. 需人工在手机上做的：推送/chmod、跑探针、抓 logcat；若 shell 域被 SELinux 挡住 mapper，用仓库现成的 `tests/android-loader-app/` 在 App 域复跑对比（本会话对手机**只做了只读读取，未改任何东西**）。
12. 最大未验证点：shell 域能否 `dlopen` 那个 `same_process_hal_file`（`--mode=mapper` 一句话判定）；以及 IMapper 返回的 fourcc/modifier/plane metadata 具体内容（hexdump 即为答案，可直接用于修 `panvk_v19_query_mapper` 的解析假设）。
13. 探针**未在真机运行过**（本会话不假设能自动操作真机），§7 的预期输出按源码推导；`mapper` 模式的 vtable/类型号是照抄 fork 自身假设，若错会崩溃——这本身也是结论。
14. 全程未修改 `/root/mesa`、`/root/MobileGL`、任何 build 目录与手机；只在 `/root/research/`（本文件 + `probe10/`）写文件，未执行任何 `rm -rf`。

*（任务 10 / 生成于源码服务器 `64.81.112.146`；真机事实为 uid 2000 只读实测；未在真机运行探针）*
