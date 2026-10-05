# 任务 11：让自编 Mesa PanVK 创建"真正的 Android 交换链"（实施报告）

> 实施者：子智能体；时间：2026-10-05 11:0x
> 目标树：`/root/zenithblue/work/mesa`（45 个未提交改动，**未做任何 git 回退/清理**）
> 构建目录：`/root/zenithblue/build/android-v4`（实测判定，见 §1）
> 备份时间戳：`TS=1791169250`
> 未触碰：`/root/mesa`、`/root/MobileGL`、手机（无 adb/安装/启动）

---

## 0. 摘要（14 行）

1. **构建目录判定**：4 个 build 目录中只有 `android-v4` 有 `libvulkan_panfrost.so`，其 md5 = `4417b369591fc2b3df27e22019ccf3a2` = 出厂件 md5；`strings | grep -c wsi_x11` = **0**，`build.ninja` 里 `wsi_common_x11|wsi_x11` 出现 **0** 次，meson `platforms=['android']` ⇒ **05 号正确、06 号的"出厂件含 x11 WSI"不成立**。
2. **优先①a 已完成**：`u_gralloc_fallback.c` 的 `!gr_mod || !gr_mod->lock_ycbcr` 由 `-EINVAL` 改为 **`-EAGAIN`**（09 §1.1 的一行修复）。
3. **优先①b 已完成**：同文件新增 `panvk_infer_linear_modifier()`，实现 MR !43659 式 **LINEAR 推断**（modifier 无效 + 单平面 + offset 0 + `fstat(fd).st_size % pitch == 0`）⇒ 给出 `DRM_FORMAT_MOD_LINEAR`。
4. **⚠️ 关键发现**：本机 WSI 自分配的 AHB 用 **format 1 (R8G8B8A8_UNORM/HAL RGBA_8888)**，`is_hal_format_yuv(1)==false` ⇒ **①a 对这条链本身不生效**；真正卡住它的是 V19 实验里那句"元数据不可用就硬失败"（`return stable_ret`）。因此 ①b 才是让交换链通过的那一步。
5. **优先②（05 方案 A）已完成**：`vk_android.c` 新增「AHB 自描述回退」约 150 行 +110 行注释，**严格加性**（仅当 `u_gralloc_get_buffer_basic_info()` 已经失败才生效），带 `PANVK_GRALLOC_NO_FALLBACK=1` / `PANVK_GRALLOC_ANY_USAGE=1` 两个开关。
6. `stride==0` 时用 `AHardwareBuffer_lockPlanes()` 实测真实字节行距（NDK API 29+）；为此在 `src/android_stub/nativewindow_stub.cpp` 补了桩（链接带 `-Wl,--no-undefined`，否则构建失败）。
7. 回退路径**只在 usage 声明了 CPU 可读/可写时才认 LINEAR**（Android 上 AFBC/UBWC 不可 CPU 映射 ⇒ 声明可映射即证明线性），否则 fail-closed 拒猜。YUV/深度/BLOB/多 layer/多平面一律拒绝。
8. 两次**增量编译分别进行、分别保留旧 .so**（未一次改完再编），`ninja` exit=0。
9. 新 .so：`md5=e08e07645c16d8ebaa11ca70a09884fd`、`size=20005320`、`sha256=a0b2451ee15a17bf25b91e195e0b59f4ad93732d…`；`SONAME`/`NEEDED` **与旧件逐条一致**（见 §4）。
10. 旧 .so 双重备份：build 目录与 dist 目录各一份 `.bak-1791169250`（md5 `4417b369…`，与出厂件逐字节相同）。
11. APK：`/root/final/mgl-panvk-v50.apk`，`versionCode=50`/`versionName=5.0-wsi-patched`，`pojavEnv` **已去掉 `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`**（实测 grep 计数 0），保留 `MOBILEGL_BACKEND_TYPE=DirectVulkan` 与 `MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log`。
12. 载荷校验：APK 内 `lib/arm64-v8a/libvulkan_freedreno.so` 的 sha256 与新 .so **逐位相同**（`a0b2451e…53f2d`）。
13. **诚实结论**：本次改动修的是"交换链创建/布局"这一环（原先必然失败）。但 **`VK_ERROR_DEVICE_LOST(-4)` 不能声称已消除**——它是在 v46–v49 的 **headless 交换链**配置下、于 MGL 纹理上传 `vkQueueSubmit` 处出现的，那条路径**不经过 AHB/gralloc/本补丁**，属独立病灶（疑似 kbase/CSF GPU fault），需要真机 logcat/dmesg 才能定位。
14. 全部改动均可一键回滚（§6），回滚不需要动 git。

---

## 1. 构建目录判定（以 md5/strings 实测为准）

```
$ find /root/zenithblue/build -name libvulkan_panfrost.so
/root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so
  size=20003136  md5=4417b369591fc2b3df27e22019ccf3a2  x11:0

$ md5sum /root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so
4417b369591fc2b3df27e22019ccf3a2      ← 与 android-v4 产物逐字节一致
```

四个 build 目录：`android-bionic/`、`android-v2/`、`android-v3/`、`android-v4/`（+`host-tools/`）；**只有 android-v4 有产物**。

补充证据（排除 06 号"含 x11 WSI"的说法）：

| 检查 | 结果 |
|---|---|
| `strings <dist so> \| grep -c wsi_x11` | **0** |
| `grep -c 'wsi_common_x11\|wsi_x11' build/android-v4/build.ninja` | **0** |
| meson `intro-buildoptions.json` → `platforms` | `['android']` |
| 同上 → `vulkan-drivers` / `gallium-drivers` | `['panfrost']` / `[]` |
| 同上 → `android-stub` | `True` |

> 06 号的误判来源已定位：编译命令里有 `-I/root/zenithblue/work/android-deps-x11/include`——那只是**依赖 include 目录的名字**，不代表编了 x11 WSI。⇒ **重编必须在 `build/android-v4` 增量进行**。

**编译前置（坑）**：直接 `ninja` 会以 `/bin/sh: 1: aarch64-linux-android35-clang: not found` 失败（PATH 里没有 NDK）。正确姿势：

```bash
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
cd /root/zenithblue/build/android-v4 && ninja
```

---

## 2. 逐处改动（3 个文件，全部严格加性）

完整 diff 另存为三份，便于逐行核对：
* `/root/research/11-diff-src_util_u_gralloc_u_gralloc_fallback.c.txt`（106 行）
* `/root/research/11-diff-src_vulkan_runtime_vk_android.c.txt`（181 行）
* `/root/research/11-diff-src_android_stub_nativewindow_stub.cpp.txt`（18 行）

### 2.1 ①a `src/util/u_gralloc/u_gralloc_fallback.c`（`fallback_gralloc_get_yuv_info`，约 :326）

```diff
    if (!gr_mod || !gr_mod->lock_ycbcr) {
-      return -EINVAL;
+      /* task-11 (09 §1.1)：这里必须是 -EAGAIN，不是 -EINVAL。… */
+      return -EAGAIN;
    }
```

理由（09 §1.1）：`is_hal_format_yuv()` 把 `HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED(0x22)` 也当 YUV，而调用方用 `-EAGAIN` 表示"其实不是 YUV，请继续走 fourcc 分支"；返回 `-EINVAL` 会被 `if (ret != -EAGAIN) return ret;` 原样上抛，直接让 `vk_gralloc_to_drm_explicit_layout()` 以 `VK_ERROR_INVALID_EXTERNAL_HANDLE` 失败。无 gralloc0 模块（`gr_mod==NULL`）在语义上等价于"问不到，不是 YUV"。

### 2.2 ①b 同文件：新增 `panvk_infer_linear_modifier()`（!43659 的 LINEAR 推断）

插在 `fallback_gralloc_get_buffer_info()` 之前（新增 include：`util/u_debug.h`、`<sys/stat.h>`）。判定条件（全部满足才认）：

```
1. !debug_get_bool_option("PANVK_GRALLOC_NO_INFER_LINEAR", false)   ← 开关
2. out->modifier == DRM_FORMAT_MOD_INVALID（没有真实元数据）
3. out->num_planes == 1 且 offsets[0] == 0 且 strides[0] > 0
4. handle 有效、data[0] >= 0
5. fstat(data[0]).st_size > 0 且 st_size % strides[0] == 0        ← !43659 的"紧凑"判据
6. implied_h = st_size / strides[0] 落在 (0, 65536]
⇒ out->modifier = DRM_FORMAT_MOD_LINEAR；补 alloc_size/layer_count；打日志
   "[PANVK-LINEAR-INFER] …"
```

并在 `fallback_gralloc_get_buffer_info()` 尾部把原来的硬失败改成"先试推断"：

```diff
    int stable_ret = panvk_v19_query_mapper(hnd->handle, out);
    if (stable_ret != 0) {
+      if (panvk_infer_linear_modifier(hnd, out,
+                                      "vendor mapper metadata unavailable"))
+         return 0;
       mesa_logw("[P0A-V19-FULLPLANE] complete metadata unavailable rc=%d; refusing guessed layout",
                 stable_ret);
       return stable_ret;
    }
```

> **为什么这是本机交换链的关键一步（重要）**：
> `panvk_wsi.c:370` 用 `.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM`（=1=HAL RGBA_8888）分配交换链 AHB。
> `is_hal_format_yuv(1) == false`（`u_gralloc_internal.c` 的 `droid_yuv_formats[]` 只含 0x22/0x23/YV12）⇒ **不经过 ①a 那条分支**；
> 于是流程落到 RGB 分支拿到 `modifier = DRM_FORMAT_MOD_INVALID`，随后被 V19 的"元数据不可用就 return stable_ret"拦死。
> ①b 正好把 `MOD_INVALID` 升级成 `LINEAR`（panvk 对 INVALID 没有 handler，会空指针），并保留 V19 优先（有真实元数据时仍用真实元数据）。

### 2.3 ② `src/vulkan/runtime/vk_android.c`：AHB 自描述回退（05 方案 A）

新增 include `util/u_debug.h`；在 `vk_android_get_ahb_layout()` 之前插入三个 static 函数：

* `vk_android_ahb_format_to_drm()` —— AHB/HAL 格式 → DRM fourcc + bpp：

| AHB 值 | 常量 | fourcc | bpp |
|---|---|---|---|
| 1 | `R8G8B8A8_UNORM` | `DRM_FORMAT_ABGR8888` | 4 |
| 2 | `R8G8B8X8_UNORM` | `DRM_FORMAT_XBGR8888` | 4 |
| 5 | `B8G8R8A8_UNORM` | `DRM_FORMAT_ARGB8888` | 4 |
| 4 | `R5G6B5_UNORM` | `DRM_FORMAT_RGB565` | 2 |
| 0x16 | `R16G16B16A16_FLOAT` | `DRM_FORMAT_ABGR16161616F` | 8 |
| 0x2b | `R10G10B10A2_UNORM` | `DRM_FORMAT_ABGR2101010` | 4 |
| 3 | `R8G8B8_UNORM` | `DRM_FORMAT_BGR888` | 3 |
| 0x22 | `IMPLEMENTATION_DEFINED` | `DRM_FORMAT_XBGR8888` | 4 |
| 其它 | YUV/BLOB/D16…S8/RAW | **拒绝**（return false） | — |

（与 `u_gralloc_internal.c:get_fourcc_from_hal_format()/get_hal_format_bpp()` 的方向一致；已核对 `HAL_PIXEL_FORMAT_RGBA_8888 → DRM_FORMAT_ABGR8888`。）

* `vk_android_ahb_probe_row_pitch()` —— 仅在 `desc.stride * bpp == 0` 时调用 `AHardwareBuffer_lockPlanes(AHB, CPU_READ_OFTEN, -1, NULL, &planes)`，取 `planes[0].rowStride`（**字节**，含 gralloc 对齐填充）。锁成功本身也证明该 buffer 可被 CPU 映射（⇒ 非 AFBC）。
* `vk_android_ahb_layout_from_desc()` —— 校验后填 `VkImageDrmFormatModifierExplicitCreateInfoEXT`：
  `drmFormatModifier = DRM_FORMAT_MOD_LINEAR`、`planeCount = 1`、`offset = 0`、`rowPitch = desc.stride*bpp`、`size = rowPitch*height`。

拒绝条件（fail-closed，全部 mesa_loge 后返回 `VK_ERROR_INVALID_EXTERNAL_HANDLE`）：`layers > 1`、格式不在上表、**usage 未声明任何 CPU 读/写位**、`stride==0` 且 lockPlanes 失败、`rowPitch < width*bpp`。

调用点改动（`vk_android_get_ahb_layout()` 尾部）：

```diff
-   return vk_gralloc_to_drm_explicit_layout(&gr_handle, out,
-                                            out_layouts, max_planes);
+   VkResult result = vk_gralloc_to_drm_explicit_layout(&gr_handle, out,
+                                                       out_layouts, max_planes);
+   if (result == VK_SUCCESS)
+      return result;
+   if (debug_get_bool_option("PANVK_GRALLOC_NO_FALLBACK", false))
+      return result;                    /* 一键回到旧行为，便于 A/B 对照 */
+   mesa_logw("u_gralloc cannot describe AHB (…); using self-described LINEAR layout", …);
+   return vk_android_ahb_layout_from_desc(ahardware_buffer, &description, out,
+                                          out_layouts, max_planes);
```

**加性证明**：`result == VK_SUCCESS` 时立即返回，与改动前**逐指令等价**；新代码只在 `u_gralloc_get_buffer_basic_info()` 已经失败之后才可达。

### 2.4 ②（配套）`src/android_stub/nativewindow_stub.cpp`：`AHardwareBuffer_lockPlanes` 补桩

```c
int32_t AHardwareBuffer_lockPlanes(AHardwareBuffer *buffer, uint64_t usage,
                                   int32_t fence, const ARect *rect,
                                   AHardwareBuffer_Planes *outPlanes)
{ (void)…; return -EINVAL; }      /* 离线桩故意失败 ⇒ 调用方 fail-closed */
```

必要性：链接命令带 `-Wl,--no-undefined`（实测 build.ninja 中 9 处），而本构建链接的是 `src/android_stub/libnativewindow.so`（ASCII 桩），桩里没有这个符号就会链接失败。**设备上不会被用到**：dist/APK 里没有 `android_stub/` 目录，`RUNPATH=$ORIGIN/../../android_stub` 落空 ⇒ 运行时解析到系统真实 `libnativewindow.so`（与既有的 `AHardwareBuffer_describe/allocate` 同一机制）。已用 `readelf -sW --dyn-syms` 确认它仍是 **UND**（§4）。

### 2.5 刻意**未做**的改动

* **M1c（给 WSI 的 AHB usage 加 `CPU_WRITE_OFTEN`）**：不必要且可能改变分配布局。该 AHB 已有 `CPU_READ_OFTEN`（`panvk_wsi.c:373`），足以通过 §2.3 的 usage 门；而 present 路径只做 `AHardwareBuffer_lock(CPU_READ_OFTEN)` + memcpy **读**，从不需要 CPU 写。⇒ 保持最小改动（如需，一行可加）。
* 方案 B（新增 `u_gralloc_ahb.c` 后端）：05 §3 已论证其架构上拿不到 `AHardwareBuffer*`、且会抢占全局 AUTO 后端，不做。
* 未改 `panvk_wsi.c`、未改 `u_gralloc.c`/`u_gralloc_internal.c`。

---

## 3. 编译证据（两次独立增量编译）

**第一步（只有 ①a+①b）**

```
$ ninja -n | tail -1
[19/19] Linking target src/panfrost/vulkan/libvulkan_panfrost.so
$ ninja            # exit 0
[4/4] Linking target src/panfrost/vulkan/libvulkan_panfrost.so
real 0m0.643s
```

**第二步（② + 桩）**

```
$ ninja -n | wc -l
22
$ ninja            # exit 0
[2/7] Compiling C++ object src/android_stub/libnativewindow.so.p/nativewindow_stub.cpp.o
[3/7] Linking target src/android_stub/libnativewindow.so
[5/7] Compiling C object src/vulkan/runtime/libvulkan_lite_runtime.a.p/vk_android.c.o
[7/7] Linking target src/panfrost/vulkan/libvulkan_panfrost.so
```

（每次 `ninja` 都会先跑一次 `Generating src/git_sha1.h` 自定义命令——这是该 build 目录的既有行为，会连带重编几个 panvk 对象，属正常。）无 warning/error；`-Werror=implicit-function-declaration`、`-Werror=missing-prototypes`、`-Werror=return-type` 等均已通过。

---

## 4. 产物验证

| 文件 | size | md5 | sha256（前 40） |
|---|---|---|---|
| `build/android-v4/…/libvulkan_panfrost.so`（新） | 20005320 | `e08e07645c16d8ebaa11ca70a09884fd` | `a0b2451ee15a17bf25b91e195e0b59f4ad93732d` |
| `build/android-v4/…/libvulkan_panfrost.so.bak-1791169250`（旧） | 20003136 | `4417b369591fc2b3df27e22019ccf3a2` | — |
| `dist/android-g720-v12-csf/libvulkan_panfrost.so`（已更新） | 20005320 | `e08e07645c16d8ebaa11ca70a09884fd` | `a0b2451ee15a17bf25b91e195e0b59f4ad93732d` |
| `dist/…/libvulkan_panfrost.so.bak-1791169250` | 20003136 | `4417b369591fc2b3df27e22019ccf3a2` | — |

**`readelf -d`（新件，与旧件逐条一致，未变坏）**

```
0x000000000000001d (RUNPATH) Library runpath: [$ORIGIN/../../android_stub]
0x0000000000000001 (NEEDED)  Shared library: [liblog.so]
0x0000000000000001 (NEEDED)  Shared library: [libnativewindow.so]
0x0000000000000001 (NEEDED)  Shared library: [libsync.so]
0x0000000000000001 (NEEDED)  Shared library: [libm.so]
0x0000000000000001 (NEEDED)  Shared library: [libz.so]
0x0000000000000001 (NEEDED)  Shared library: [libdl.so]
0x0000000000000001 (NEEDED)  Shared library: [libc.so]
0x000000000000000e (SONAME)  Library soname: [libvulkan_panfrost.so]
```

**UND 动态符号（关键：新增的 lockPlanes 仍是 UND ⇒ 运行时走系统库）**

```
 AHARDWAREBUFFER/… 43..83: UND ANativeWindow_setBuffersGeometry
                            UND AHardwareBuffer_allocate
                            UND ANativeWindow_lock
                            UND AHardwareBuffer_describe
                            UND AHardwareBuffer_getNativeHandle
                            UND AHardwareBuffer_lockPlanes      ← 新增
```

**新增字符串（证明三个改动都进了二进制）**

```
[PANVK-LINEAR-INFER] %s: fourcc=0x%08x pitch=%d size=%llu implied_h=%llu -> DRM_FORMAT_MOD_LINEAR
PANVK_GRALLOC_NO_INFER_LINEAR / PANVK_GRALLOC_NO_FALLBACK / PANVK_GRALLOC_ANY_USAGE
AHB layout fallback: %ux%u fmt=0x%x fourcc=0x%08x pitch=%llu usage=0x%llx -> DRM_FORMAT_MOD_LINEAR
u_gralloc cannot describe AHB (%ux%u fmt=0x%x stride=%u usage=0x%llx); using self-described LINEAR layout
AHB layout fallback: usage=0x%llx declares no CPU access; cannot prove LINEAR
AHB layout fallback: stride==0 and lockPlanes probe failed; refusing to guess a row pitch
AHB layout fallback: %u layers unsupported / pitch %llu < width(%u)*bpp(%u) / format 0x%x has no known linear mapping
```

---

## 5. APK 交付

* 路径：**`/root/final/mgl-panvk-v50.apk`**，size=10187311，`sha256=677d81eb29c7c57938573dfd9de20d39…`
* 打包脚本（新写，可重放）：`/root/pack_v50.sh`（用法 `bash /root/pack_v50.sh [panvk.so]`）
* `aapt2 dump badging`：

```
package: name='com.dsh.plugin.driver.g720' versionCode='50' versionName='5.0-wsi-patched'
minSdkVersion:'26' targetSdkVersion:'34'
application: label='MobileGL Magma + PanVK'
```

* `pojavEnv` / `boatEnv`（两者相同；`grep -c HEADLESS` 实测 **0**）：

```
LIBGL_ES=3:POJAV_RENDERER=opengles3:MOBILEGL_BACKEND_TYPE=DirectVulkan:
MOBILEGL_ESPRYT_USE_ANGLE=0:MOBILEGL_MAGMA_R11G11B10F_FALLBACK=0:
PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent:MESA_DEBUG=1:PANVK_DEBUG=1:
PANVK_GRALLOC_NO_FALLBACK=0:PANVK_GRALLOC_NO_INFER_LINEAR=0:
MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log
```

  * 与 v49 的差异**只有** `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1` 被删除（外加新增两个=0 的显式开关）；保留了 v49 已验证的 `PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent`、`MESA_DEBUG=1`、`PANVK_DEBUG=1`，目的是让本次真机实验**只有一个自变量**。
  * `renderer=magma_panvk:libMobileGL.so:libMobileGL.so`、`fclPlugin=true`、`package` 与 v46/v49 相同 ⇒ 可直接覆盖安装。

* 载荷（`unzip -l`）：

```
lib/arm64-v8a/libMobileGL.so          16956584   ← 取自 /root/v49（md5 2dfbe8d7622c90c2759eae5260c1e666）
lib/arm64-v8a/libvulkan_freedreno.so  20005320   ← 新 libvulkan_panfrost.so 改名
classes.dex                               1328   ← 取自 /root/v49（sha256 6bd3abde…）
```

* **载荷一致性**：`unzip -p` 取出的 `libvulkan_freedreno.so` 的 sha256 = `a0b2451ee15a17bf25b91e195e0b59f4ad93732d7fa771afb6a2ac7be3853f2d`，与新 .so **逐位相同**。

* 签名：`/root/dsh-driver.keystore`（alias `dshdriver`），与 v46–v49 同一把钥匙、同一包名 ⇒ 升级安装不需要卸载。

---

## 6. 回滚命令（不需要也不允许用 git）

```bash
# 6.1 回滚源码（三个文件）
cd /root/zenithblue/work/mesa
cp -f src/util/u_gralloc/u_gralloc_fallback.c.bak-1791169250 src/util/u_gralloc/u_gralloc_fallback.c
cp -f src/vulkan/runtime/vk_android.c.bak-1791169250        src/vulkan/runtime/vk_android.c
cp -f src/android_stub/nativewindow_stub.cpp.bak-1791169250 src/android_stub/nativewindow_stub.cpp

# 6.2 重编回旧件
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
cd /root/zenithblue/build/android-v4 && ninja
cp -f src/panfrost/vulkan/libvulkan_panfrost.so /root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so

# 6.3 或者最快：直接用备份 .so 覆盖（无需重编）
cp -f /root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so.bak-1791169250 \
      /root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so
#     （build 目录也有一份同名备份，md5 均为 4417b369591fc2b3df27e22019ccf3a2）

# 6.4 行为级开关（不重编、不换包，只改 pojavEnv）
#     PANVK_GRALLOC_NO_FALLBACK=1      → 关掉 ②（05 方案 A）
#     PANVK_GRALLOC_NO_INFER_LINEAR=1  → 关掉 ①b（LINEAR 推断）
#     PANVK_GRALLOC_ANY_USAGE=1        → 放宽 ② 的 CPU-usage 门（默认关闭）
```

---

## 7. 风险与未验证项（如实）

1. **`VK_ERROR_DEVICE_LOST (-4)` 未被证明解决**。它出现在 v46–v49 的 **headless 交换链**配置下（`docs/09`：`FATAL: Vulkan error VK_ERROR_DEVICE_LOST (-4) at VkTextureManager.cpp / vkQueueSubmit(texture upload batch)`），那条路径**不创建 AHB、不经过 u_gralloc、也不经过本补丁**。本补丁把"交换链无法创建"这一关打通，但纹理上传的 GPU fault 是**独立病灶**，需要真机 `logcat`/`dmesg`（mali/kbase fault 记录）才能定位。**不要把 v50 的失败/成功直接等同于本补丁的成败。**
2. **LINEAR 推断的正确性只在"声明了 CPU 访问"的前提下成立**。若某设备/某分配路径给出"声明可 CPU 访问但实际是 AFBC"的 buffer，则会出现画面斜切/花屏而非报错。兜底手段：`PANVK_GRALLOC_NO_INFER_LINEAR=1`（回到 fail-closed）。本机 WSI 的 AHB 是我们自己用 `R8G8B8A8_UNORM + CPU_READ_OFTEN` 分配的，风险低。
3. **②从未在真机执行过**（任务禁止碰手机）⇒ 只做了静态/链接层验证，没有运行时证据。真机首跑必须看 `/sdcard/MG/mgl.log` 里是否出现 `AHB layout fallback: … -> DRM_FORMAT_MOD_LINEAR` 或 `[PANVK-LINEAR-INFER] …`。
4. `panvk_v19_query_mapper()` 仍优先于推断（有真实 modifier 时用真实的）——这是有意的，但意味着**同一份 .so 在不同设备上可能走不同分支**，排查时必须靠日志区分。
5. `desc.stride` 的单位假设为**像素**（NDK 头原文 "Row stride in pixels"）。该假设与 `panvk_wsi.c:280-290` present 路径里既有的 `desc.stride * 4` 一致，属项目内部自洽；但若某 ROM 的 `GraphicBuffer::getStride()` 返回字节，则会算错 pitch（症状=斜切）。真机若出现斜切，优先怀疑此处（可用 `lockPlanes` 路径强制实测）。
6. 未做 `vkCreateImage`/`vkAllocateMemory` 的成功性验证（需要设备）；05 号给出的成功率估计为 70–80% 让 `vkCreateImage`/`vkAllocateMemory` 通过。
7. APK 未做安装/启动验证（禁止碰手机）；包名/签名/versionCode 与 v46–v49 同构，安装风险与既往版本相同。

---

## 8. 手机端下一步（交给主 agent，按顺序）

1. 覆盖安装 `/root/final/mgl-panvk-v50.apk`（同包名同签名，无需卸载；versionCode 50 > 49）。
2. 清空 `/sdcard/MG/` 下旧日志，启动一次渲染器，然后把 `/sdcard/MG/mgl.log` 抓回来。
3. **判定本补丁是否生效**（按优先级找这三类行）：
   * `AHB layout fallback: <W>x<H> fmt=0x1 fourcc=0x34324241 pitch=… usage=0x… -> DRM_FORMAT_MOD_LINEAR`（② 生效，交换链走了自描述路径）
   * `[PANVK-LINEAR-INFER] vendor mapper metadata unavailable: … -> DRM_FORMAT_MOD_LINEAR`（①b 生效）
   * 反例：`u_gralloc_get_buffer_basic_info failed` 且没有上面两行 ⇒ 回退没被触发（另需查 `PANVK_GRALLOC_NO_FALLBACK` 是否被外部环境置 1）。
   * 还应确认日志里**不再**出现 headless 交换链特征（v50 已删该 env）。
4. 若交换链建起来了但 `VK_ERROR_DEVICE_LOST(-4)` 仍在 `vkQueueSubmit(texture upload)` 处：
   * `logcat -b all -d | grep -iE 'mali|kbase|fault|csf|gpu'` 抓 GPU fault；
   * `dmesg | grep -iE 'mali|kbase|fault'`；
   * 这就是**下一个独立任务**（kbase/CSF fault），不要继续在本补丁上加码。
5. 需要 A/B 对照时，只改 `pojavEnv`（见 §6.4），其余不动。
   * A=`PANVK_GRALLOC_NO_FALLBACK=1`+`PANVK_GRALLOC_NO_INFER_LINEAR=1`（完全旧行为，用来确认"确实没有交换链"）
   * B=默认（本补丁全开）
