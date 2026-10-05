# 21 · P5 实施记录 —— MTK AFBC modifier 回退（跑通方方向）

> 任务：真机 v54 已能进主界面并渲染数秒，但**画面错乱**（问题 1）。最强假设 =
> 交换链/AHB 图像的 **DRM modifier 方向搞反**（我们强推 `DRM_FORMAT_MOD_LINEAR`，
> 跑通方铁律要求 AFBC `0x0800000000000072`）。本报告记录 P5 的确切改动。
> **本次改的是 `/root/zenithblue/work/mesa`（不是 `/root/mesa`）**；未改 `/root/mesa`、
> `/root/MobileGL`；未碰手机（无 adb、无安装、未触碰红线三件套）。
>
> 基线 = 树内现状 v54（`.so` sha256 `a9cba64afa935370906e1a8533550816c6a018f4d6808cb8951dd4e1099333f1`，
> 20006408 B）。**未做任何 git checkout/stash/reset**（树内 49+ 未提交改动原样保留）。

---

## 0. 一页结论

| 项 | 值 |
|---|---|
| 跑通方要求的 modifier | `DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 \| AFBC_FORMAT_MOD_SPARSE \| AFBC_FORMAT_MOD_SPLIT \| AFBC_FORMAT_MOD_YTR)` = **`0x0800000000000072`** |
| 改动文件 | `vk_android.c`（2 处）、`panvk_image.c`（1 处）、`pan_mod.c`（1 处）、`pan_desc.c`（1 处）（+ 2 个 `#include`） |
| 行为开关 | `PANVK_GRALLOC_AFBC_FALLBACK`（**默认 1 = 跑通方方向**；=0 ⇒ 逐字回到 v54 行为） |
| 编译 | `ninja` **exit 0**，无 error/warning |
| 新 `.so` | `d0476a0c155af5debbd9cf3a7eee4b8aaab1b6a70bb1947901c2a959c6a9dd1a`，20010648 B |
| v56 APK | `/root/final/mgl-panvk-v56.apk`，sha256 `1ecfb2ffc6cf33d8b28376d079da38a3a38e3b01d647acafd87b55594fcefbec`，versionCode **56** |
| APK 载荷校验 | `unzip -p … lib/arm64-v8a/libvulkan_freedreno.so \| sha256sum` = **新 `.so` sha256**，20010648 B（非空，实测） |

---

## 1. 跑通方依据（原样引用，非推断）

先例仓库：`wonderkast02/panvk-g720-kbase-csf`（MT6899 / Mali-G720 MC8 / kbase-CSF）。
其补丁实体在服务器上：`/root/panvk-mtk/patches/panvk_mtk.patch`（903 行）。

**（a）铁律（19 号报告 §5.2 逐字抄录）**：

> 规则：**"Nunca converter `DRM_FORMAT_MOD_INVALID` em `DRM_FORMAT_MOD_LINEAR` por suposição."**
> （绝不把 `DRM_FORMAT_MOD_INVALID` 凭假设转成 `LINEAR`）

**（b）patch 里 modifier 的确切表达式**（`panvk_mtk.patch`，`src/panfrost/vulkan/panvk_image.c` hunk，
patch 行 321-408；以及 `src/vulkan/runtime/vk_android.c` hunk，patch 行 ~687-694）：

```c
   out->drmFormatModifier = info.modifier;
+  if (out->drmFormatModifier == DRM_FORMAT_MOD_INVALID) {
+     out->drmFormatModifier =
+        DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 |
+                                AFBC_FORMAT_MOD_SPARSE |
+                                AFBC_FORMAT_MOD_SPLIT |
+                                AFBC_FORMAT_MOD_YTR);
+  }
```
```c
-   assert(panvk_image_can_use_mod(image, iusage, mod, false));
+   if (mod == DRM_FORMAT_MOD_INVALID ||
+       !panvk_image_can_use_mod(image, iusage, mod, false)) {
+      mod = DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 |
+                                    AFBC_FORMAT_MOD_SPARSE |
+                                    AFBC_FORMAT_MOD_SPLIT |
+                                    AFBC_FORMAT_MOD_YTR);
+      if (!panvk_image_can_use_mod(image, iusage, mod, false)) {
+         mod = DRM_FORMAT_MOD_LINEAR;              /* 只有 AFBC 也不可用才 LINEAR */
+      }
+   }
```
其注释原文：*"MediaTek gralloc reports DRM_FORMAT_MOD_INVALID for AHardwareBuffer
images but allocates them as ARM AFBC (32x8 sparse/split/YTR)."*

**（c）数值核对（本次自行算过，非抄）**：
`DRM_FORMAT_MOD_ARM_AFBC(x) = fourcc_mod_code(DRM_FORMAT_MOD_VENDOR_ARM=0x08, x)`
= `0x08 << 56 | x`。常量值（`include/drm-uapi/drm_fourcc.h`）：
`AFBC_FORMAT_MOD_BLOCK_SIZE_32x8=0x2`、`AFBC_FORMAT_MOD_YTR=1<<4=0x10`、
`AFBC_FORMAT_MOD_SPLIT=1<<5=0x20`、`AFBC_FORMAT_MOD_SPARSE=1<<6=0x40`
⇒ `x = 0x2|0x10|0x20|0x40 = 0x72` ⇒ **`0x0800000000000072`**。
本树 `src/panfrost/lib/pan_format.h:26` 的 `PAN_SUPPORTED_MODIFIERS` 第 3 项正是这个组合
⇒ 该 modifier 在 panvk 里**有 handler 且可选**（`pan_mod_afbc_match()` 用 `drm_is_afbc()`，
不是精确值匹配）。

**（d）什么时候用 AFBC / 什么时候才允许 LINEAR**（跑通方语义，本次逐字照搬）：

| 条件 | 结果 |
|---|---|
| modifier 已知且 `panvk_image_can_use_mod()` 通过 | 用该 modifier（不动） |
| modifier == `DRM_FORMAT_MOD_INVALID` **或** 不可用 | **先试 AFBC `0x0800000000000072`** |
| AFBC 也不可用（`panvk_image_can_use_mod()` 为 false，如 `PANVK_DEBUG=noafbc`、storage/host-copy 用途、YUV 多平面、格式不支持…） | 才 `DRM_FORMAT_MOD_LINEAR` |
| AHB 自描述回退里 YUV / 深度 / 多 layer / 未声明 CPU usage / stride 探不到 | **一律拒绝（fail-closed），不猜** |

---

## 2. 我们 v54 的行为（改动前，供对照）

| 位置 | v54 行为 |
|---|---|
| `src/vulkan/runtime/vk_android.c:183` | `out->drmFormatModifier = info.modifier;` —— u_gralloc 报 `INVALID` 就**直通**，随后在 `panvk_image_get_explicit_mod()` 里撞断言 |
| `src/vulkan/runtime/vk_android.c:790-805`（自描述回退，真机实测会打印 `AHB layout fallback: … -> DRM_FORMAT_MOD_LINEAR`） | 无条件 `DRM_FORMAT_MOD_LINEAR`，并用 `desc.stride*bpp` 当 rowPitch |
| `src/panfrost/vulkan/panvk_image.c:327` | `assert(panvk_image_can_use_mod(...))` —— INVALID 直接断言 |
| `src/panfrost/lib/pan_mod.c:166`、`pan_desc.c:289` | AFBC body offset 只按 `pan_afbc_body_offset()` 对齐（非 TILED 时 = 128），**没有 MTK 的 4096 对齐** |

设备证据：18 号报告 §2.2 已核实真机日志里**确实出现** `I/MESA: AHB layout fallback: …`
（`vk_android.c:802`，`mesa_logi`）⇒ 自描述回退这条路径**在真机上真的走了**，
所以「方向反了」这条假设有现场抓手。

---

## 3. P5 的确切改动（本次实施，逐处）

开关统一为 `PANVK_GRALLOC_AFBC_FALLBACK`，`debug_get_bool_option(..., true)` ⇒ **默认 1**。
`=0` 时四处判断全部短路 ⇒ 与 v54 行为逐字相同（可做干净 A/B）。

### 3.1 `src/vulkan/runtime/vk_android.c`

**(1) 新增常量 + 编译期核对（`:56-67`，位于 include 之后、首次使用之前）**

```c
/* MTK gralloc 的 AHB 实际布局 = ARM AFBC 32x8|SPARSE|SPLIT|YTR
 * = 0x0800000000000072 … */
#define PANVK_MTK_AFBC_MOD                                                     \
   DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 |                   \
                           AFBC_FORMAT_MOD_SPARSE | AFBC_FORMAT_MOD_SPLIT |    \
                           AFBC_FORMAT_MOD_YTR)
_Static_assert(PANVK_MTK_AFBC_MOD == 0x0800000000000072ULL,
               "MTK AFBC modifier must be 0x0800000000000072");
```
（`_Static_assert` 保证「跑通方数值」被编译器钉死；本文件用 C11，见 `meson.build:13 c_std=c11`。）

**(2) `vk_gralloc_to_drm_explicit_layout()`（u_gralloc 成功但报 INVALID 的那条路）`:193-200`**

```c
   out->drmFormatModifier = info.modifier;
   /* P5：跑通方铁律 —— u_gralloc 报 INVALID 时不猜 LINEAR，改用 MTK 实际
    * AFBC modifier。默认开启；PANVK_GRALLOC_AFBC_FALLBACK=0 回到 v54 直通。 */
   if (out->drmFormatModifier == DRM_FORMAT_MOD_INVALID &&
       debug_get_bool_option("PANVK_GRALLOC_AFBC_FALLBACK", true)) {
      out->drmFormatModifier = PANVK_MTK_AFBC_MOD;
   }
   out->drmFormatModifierPlaneCount = info.num_planes;
```
⇒ 与跑通方 hunk (b) 第一段**同方向、同数值**。副作用（正向）：顺便消除了
`INVALID` 直通到 `panvk_image_get_explicit_mod()` 断言的老隐患。

**(3) 自描述回退 `vk_android_ahb_layout_from_desc()`（真机实际走的那条）`:816-855`**

```c
   const bool afbc_fallback =
      debug_get_bool_option("PANVK_GRALLOC_AFBC_FALLBACK", true);
   …
   if (afbc_fallback) {
      /* rowPitch 必须留 0，否则会触发 AFBC 的 explicit-layout 分支，用线性
       * pitch 反算 AFBC tile 行距（实测会 "WSI pitch too small" 直接失败）。 */
      out->drmFormatModifier = PANVK_MTK_AFBC_MOD;
      out_layouts[0].offset = 0;
      out_layouts[0].rowPitch = 0;
      out_layouts[0].size = 0;
      mesa_logi("AHB layout fallback: %ux%u fmt=0x%x fourcc=0x%08x "
                "usage=0x%llx -> MTK AFBC 0x%llx", …);
   } else {
      out->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;   /* = v54 */
      out_layouts[0].rowPitch = row_pitch;
      out_layouts[0].size = row_pitch * desc->height;
      mesa_logi("AHB layout fallback: … -> DRM_FORMAT_MOD_LINEAR", …);
   }
```

> **为什么 AFBC 分支必须让 `rowPitch = 0`（本次的关键实现细节，非跑通方原文）**：
> `pan_mod.c:66-69` / `pan_desc.c` 侧，`layout_constraints.wsi_row_pitch_B != 0`
> 会打开 **AFBC 的 explicit-layout 分支**（`pan_mod.c:88-121`）：它把
> `wsi_row_pitch_B * superblock_height / afbc_tile_payload_size_B * superblock_width`
> 当作资源宽度，并要求 `>= 图像宽度`，否则 `mesa_loge("WSI pitch too small")` + **建图失败**。
> 我们只有「线性行距」（`desc.stride*bpp`，例如 1080×4B）：`34560/1024*32 = 1056 < 1080`
> ⇒ 必然失败。传 0 则改走「按图像尺寸推导 AFBC 布局」那条路，等价于
> 「这块 AHB 就是标准 MTK AFBC 分配」这一前提。
> 与此同时 `strict_import()`（`panvk_image.c:404-410`）对非 YUV 的 AFBC 返回 false，
> 本来也不会用 WSI pitch ⇒ 行为自洽。

### 3.2 `src/panfrost/vulkan/panvk_image.c` — 同一常量副本 + 优雅降级（`:316-360`）

同名宏副本（common runtime 层不能 include panfrost 头文件，两处必须逐字一致），
`panvk_image_get_explicit_mod()` 里的断言替换为跑通方 hunk (b) 第二段：

```c
   if (debug_get_bool_option("PANVK_GRALLOC_AFBC_FALLBACK", true) &&
       (mod == DRM_FORMAT_MOD_INVALID ||
        !panvk_image_can_use_mod(image, iusage, mod, false))) {
      mesa_logw("panvk: explicit mod 0x%llx unusable, trying MTK AFBC 0x%llx", …);
      mod = PANVK_MTK_AFBC_MOD;
      if (!panvk_image_can_use_mod(image, iusage, mod, false)) {
         mesa_logw("panvk: MTK AFBC unusable, using LINEAR");
         mod = DRM_FORMAT_MOD_LINEAR;
      }
   } else {
      assert(panvk_image_can_use_mod(image, iusage, mod, false));
   }
```
安全性已核对：`panvk_image_can_use_mod(..., DRM_FORMAT_MOD_INVALID, ...)` 不会崩
（`drm_is_afbc(INVALID)==false` → 走到 `pan_image_test_props()`，而
`pan_image.h:343-346` 对 `mod_handler == NULL` 直接 `return PAN_MOD_NOT_SUPPORTED`）。
`PANVK_DEBUG=linear` 时 `forced_linear` 会让 AFBC 也返回 false ⇒ 自动落到 LINEAR，不会强推 AFBC。

### 3.3 `src/panfrost/lib/pan_mod.c:168-175` + `src/panfrost/lib/pan_desc.c:291-295`（P5/H4，4096 对齐）

跑通方 `panvk_mtk.patch` 的 `pan_mod.c` / `pan_desc.c` hunk 逐字照搬，只是加了开关：

```c
   /* pan_mod.c */
   if ((props->modifier & AFBC_FORMAT_MOD_SPLIT) &&
       debug_get_bool_option("PANVK_GRALLOC_AFBC_FALLBACK", true))
      body_offset_B = ALIGN_POT(body_offset_B, 4096);
   /* pan_desc.c: get_afbc_att_mem_props() 同一条件 */
```
必要性：`pan_afbc_header_align()`（`pan_afbc.h:362-370`）对**非 TILED** 只给 128 对齐，
而 MTK gralloc 对 split AFBC 一律 4096 对齐 body。两处**必须同时改**，否则
「GPU 写 payload 的位置」与「显示/采样管线读的位置」不一致 —— 这本身就能造成花屏。
（第三处 `pan_desc.c:801` 的同类计算在 `PAN_ARCH < 9` 分支里，G720=arch12 不走，故未改，与跑通方一致。）
另加 `#include "util/u_debug.h"`（`pan_mod.c:18`、`pan_desc.c:8`；两文件的构建目标都依赖
`idep_mesautil`，`debug_get_bool_option` 可用 —— 编译已验证）。

### 3.4 明确**保留**的 fail-closed 守卫（一行都没删）

`vk_android.c` 自描述回退里原样保留，且对这些情况**依然直接拒绝、不猜**：
`desc->layers > 1`、格式映射表未命中（YUV / 深度 / BLOB / RAW / 未知）、
未声明 CPU 访问（`AHARDWAREBUFFER_USAGE_CPU_READ/WRITE_MASK` 全 0，除非
`PANVK_GRALLOC_ANY_USAGE=1`）、`stride==0` 且 `AHardwareBuffer_lockPlanes()` 也探不到、
`row_pitch < width*bpp`。
`panvk_image.c` 侧 `samples==1` / 非 3D / 非 depth-stencil 三个断言也原样保留。

---

## 4. 开关与 A/B 方案

| env | 行为 |
|---|---|
| （不设）/ `PANVK_GRALLOC_AFBC_FALLBACK=1` | **P5 生效**：INVALID→AFBC `0x…072`；自描述回退声明 AFBC；AFBC+SPLIT body 4096 对齐 |
| `PANVK_GRALLOC_AFBC_FALLBACK=0` | 四处判断全部关闭 ⇒ **逐字回到 v54**（自描述回退→LINEAR；INVALID 直通/断言；body 128 对齐） |
| `PANVK_GRALLOC_NO_FALLBACK=1` | 完全不做自描述回退（旧行为，仍在，优先级高于 AFBC 开关） |
| `PANVK_GRALLOC_ANY_USAGE=1` | 允许未声明 CPU usage 的 AHB（默认关） |

⇒ 同一份 v56 APK 内即可 A/B：追加 `PANVK_GRALLOC_AFBC_FALLBACK=0` 到 `pojavEnv`/`boatEnv`
就是 v54 语义。**v56 的 manifest/env 与 v54 逐字符一致**（差异仅 versionCode/Name，见 §6），
所以默认跑的就是「P5 生效」这一档；想对照再手改 env。

---

## 5. 回滚

**源码回滚（改动前备份，时间戳 1791175338）**：

```bash
cd /root/zenithblue/work/mesa && \
for f in src/vulkan/runtime/vk_android.c src/panfrost/vulkan/panvk_image.c \
         src/panfrost/lib/pan_mod.c src/panfrost/lib/pan_desc.c; do
  cp -f "$f.bak-1791175338" "$f"; done
# 然后增量重编：
cd /root/zenithblue/build/android-v4 && \
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH && ninja
```
（旧件备份：`<file>.bak-1791175338`，与源文件同目录。）

**产物回滚**：直接装回 `/root/final/mgl-panvk-v54.apk`（未改动，v54 的 `.so` 也在
`/root/v54/lib/arm64-v8a/libvulkan_freedreno.so`，sha256 `a9cba64a…`）。
**不需要**回滚构建树就能做行为对照 —— 设 `PANVK_GRALLOC_AFBC_FALLBACK=0` 即可。

---

## 6. 构建与产物校验（全部实测）

```
# 构建树：/root/zenithblue/work/mesa ；构建目录：/root/zenithblue/build/android-v4
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
ninja            # exit 0，log = /root/research/21-build.log（无 error / warning）
```
* 第一次编译 `vk_android.c:188: error: use of undeclared identifier 'PANVK_MTK_AFBC_MOD'`
  —— 宏定义位置晚于首次使用；**已把宏定义移到文件靠前的 include 之后**（`:56-67`）后重编通过。
  该行号仅存在于中间态，不是最终产物的问题。

| 产物 | 值 |
|---|---|
| 新 `.so` | `/root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so`，20010648 B，sha256 **`d0476a0c155af5debbd9cf3a7eee4b8aaab1b6a70bb1947901c2a959c6a9dd1a`** |
| v54 `.so`（对照） | 20006408 B，sha256 `a9cba64afa935370906e1a8533550816c6a018f4d6808cb8951dd4e1099333f1` |
| v56 APK | `/root/final/mgl-panvk-v56.apk`，10191407 B，sha256 **`1ecfb2ffc6cf33d8b28376d079da38a3a38e3b01d647acafd87b55594fcefbec`** |
| APK 内 `libvulkan_freedreno.so` | sha256 = `d0476a0c…`，**20010648 B（非空，实测）** |
| APK 内 `libMobileGL.so` | sha256 = `72919c73a7e07630f329bb4ad9605c606a9aac9e2fc6596683ee7b9f9c76848b`（= v54） |
| APK 内 `classes.dex` | sha256 = `6bd3abde2c53506f1b54bb88a8069c3cc394c2fb08440e4ca475cbf8fd64f4ad`（= v54） |
| badging | `versionCode='56' versionName='5.6-p5-mtk-afbc-modifier'`，签名者 `CN=DSH Mali Driver`（与 v54 同 keystore） |
| manifest 对比 v54 | `diff 17-v54-manifest.txt 21-v56-manifest.txt` **只有 versionCode/versionName 两行**；`pojavEnv`/`boatEnv` 逐字符相同，**未加** `MESA_VK_WSI_HEADLESS_SWAPCHAIN` |

**二进制内容证据**（证明新代码真的进了载荷）：

| 字符串 | v54 `.so` | v56 `.so` |
|---|---|---|
| `panvk: explicit mod 0x%llx unusable, trying MTK AFBC 0x%llx` | 0 | 1 |
| `panvk: MTK AFBC unusable, using LINEAR` | 0 | 1 |
| `PANVK_GRALLOC_AFBC_FALLBACK` | 0 | 1 |
| `AHB layout fallback: … -> MTK AFBC 0x%llx` | 0 | 1 |

（另：`0x0800000000000072` 的 8 字节小端字面量在 v54/v56 里各有 4 处 —— 因为
`PAN_SUPPORTED_MODIFIERS` 本来就有这一项，**不能**用它区分新旧；判别用上面的新字符串。）

---

## 7. 风险与不确定（如实）

1. **方向本身是强假设，不是已证事实**。跑通方是 MT6899/G720 MC8/uAPI 1.30，我们是
   MT6989/G720 MC12/uAPI 1.21（19 号报告 §5.3）。跑通方的铁律适用于它的 gralloc 行为；
   如果 PHZ110 的 gralloc 对**这条 AHB（走的还是 u_gralloc 失败之后的自描述回退）**
   实际给的是线性缓冲，那么声明 AFBC 会把原来「至少能看几秒」变成更糟。⇒ **这正是留 env 开关的原因**，
   一次真机 A/B 就能判定。
2. **自描述回退本来就是猜**（u_gralloc 已经完全失败，我们只有 AHB desc）。所以 P5 不是
   「把错的改成对的」，而是「把一种假设换成有第三方背书的那种假设」。真正的根治是让
   u_gralloc 能描述这块 AHB（或走 vk_gralloc 成功路径），本轮**没有**动它。
3. **4096 对齐会影响所有 AFBC+SPLIT 图像**（不止 AHB）。它自洽（写方 `pan_mod.c` 与
   描述符 `pan_desc.c` 同时改），但若某个内部 AFBC 渲染目标此前依赖 128 对齐的尺寸/偏移，
   尺寸会变（`.so` 大 240 B 也说明代码/常量有变）。判据：offscreen/内部 AFBC 用例是否仍通过。
   `PANVK_GRALLOC_AFBC_FALLBACK=0` 可一键排除。
4. **rowPitch=0 是一个设计选择**（§3.1(3) 的理由）。若 panvk 需要 gralloc 的真实 AFBC 行距
   才能匹配，AFBC 分支可能仍错 —— 那时 AHBC 导入应回头走 u_gralloc 的 `info.strides`。
5. **本次不触碰问题 2**（作业「已完成但内核超时」→ 10 s 后 `VK_ERROR_DEVICE_LOST`）；
   v56 里 `KBASE_WAIT_TIMEOUT_NS` 仍是 10 s，`kbase_subqueue_publish` 的 doorbell 快速路径仍在。
   所以真机仍可能 10 s 后 DEVICE_LOST —— **那不是 P5 的回归**。

---

## 8. 下次真机该看哪条日志（判据）

前置：确认设备跑的**确实是 v56**（`unzip -p … \| sha256sum` 或设备侧 sha256；
18 号报告 §2.4 就是被「跑的不是目标 .so」坑过）。

1. **走了哪条路 / 选了什么 modifier**（`I/MESA`，`vk_android.c`）：
   * `AHB layout fallback: WxH fmt=0x… fourcc=0x… usage=0x… -> MTK AFBC 0x800000000000072`
     ⇒ **P5 生效**，自描述回退声明了 AFBC。
   * `AHB layout fallback: … pitch=… usage=0x… -> DRM_FORMAT_MOD_LINEAR` ⇒ 声明的是 LINEAR
     （说明 `PANVK_GRALLOC_AFBC_FALLBACK=0` 或 P5 没进这份 `.so`）。
   * `u_gralloc cannot describe AHB (…) ; using self-described layout` ⇒ 确认是自描述回退这条路径
     （v54 这句写的是 `… self-described LINEAR layout`，v56 改成中性措辞 —— 可用它区分新旧二进制）。
2. **panvk 侧有没有降级**（`W/MESA`，`panvk_image.c`）：
   * `panvk: explicit mod 0x… unusable, trying MTK AFBC 0x800000000000072`
   * `panvk: MTK AFBC unusable, using LINEAR` ← **出现这行 = AFBC 在本机/本格式上不可用，P5 没生效**，
     `pan_format.c`/`panvk_image_can_use_mod()` 的哪条拒绝要接着查。
3. **画面是否还错乱**：主界面渲染数秒内截图对比；同时 `MESA_DEBUG=1/PANVK_DEBUG=1` 下留意
   `WSI pitch …` / `WSI offset …` 之类 `mesa_loge`（若出现 ⇒ AFBC explicit-layout 分支被误触发，
   即 §3.1(3) 的 rowPitch=0 假设被破坏）。
4. A/B：把 `PANVK_GRALLOC_AFBC_FALLBACK=0` 加进 env 重跑，对比 1) 的日志行与画面。
