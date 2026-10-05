# 27 — v60：干净单变量构建（v54 基线 + C1）

**结论：成功。** v60 = **v54 逐位基线 + C1（唯一新变量）**，P5 与 v58 缓存失效全部撤回。
实测证据链：**撤掉 C1 后重编 = `a9cba64a…`，与 v54 载荷 `.so`（20 006 408 B）逐位相同** —— 证明树里已无任何残留污染变量。

* 构建树：`/root/zenithblue/work/mesa`（**不是** `/root/mesa`）
* 构建目录：`/root/zenithblue/build/android-v4`
* 日期：2026-10-05
* 上一轮：`/root/research/26-c1-heap-suppression.md`（v59 = v58 + C1，**带 P5 污染**，不能用于归因）

---

## 1. 撤回清单（5 个文件）

| # | 文件 | 从 | 到 | sha256（撤回后） |
|---|------|-----|-----|------------------|
| 1 | `src/vulkan/runtime/vk_android.c` | P5 | `.bak-1791175338` | `d582a9f6…` |
| 2 | `src/panfrost/vulkan/panvk_image.c` | P5 | `.bak-1791175338` | `55c04e76…` |
| 3 | `src/panfrost/lib/pan_mod.c` | P5 | `.bak-1791175338` | `b3b8c61c…` |
| 4 | `src/panfrost/lib/pan_desc.c` | P5 | `.bak-1791175338` | `0d362bd9…` |
| 5 | `src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c` | v58 的 2 行 | `.bak-1791176223` | `f3bf4ea5…` |

命令（已执行，逐条 `cp -f`，**未**用 `git checkout/stash/reset`）：

```bash
cd /root/zenithblue/work/mesa
for f in src/vulkan/runtime/vk_android.c src/panfrost/vulkan/panvk_image.c \
         src/panfrost/lib/pan_mod.c src/panfrost/lib/pan_desc.c; do
  cp -f "$f.bak-1791175338" "$f"
done
cp -f src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c.bak-1791176223 \
      src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c
```

**保留不动**：`src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c`（= C1，sha256 `83356733…`）。

### 1.1 撤回后 6 个文件的完整哈希（`/root/research/27-work/v60-sources.sha256`）

```
d582a9f645b5625da71742b7dbb23eca7293fc202eac2aac6ac3066d58edbfb2  src/vulkan/runtime/vk_android.c
55c04e769003309826b197e8afca3a1774ac977f6d46bcd425c0592cc1884954  src/panfrost/vulkan/panvk_image.c
b3b8c61c2c429f859e9de7448b208988a72d53d2dc6af085ca667822cfa62ce4  src/panfrost/lib/pan_mod.c
0d362bd90e215663bf7639b5ae161870a3c3da438de375ef96ab42eda3544daa  src/panfrost/lib/pan_desc.c
f3bf4ea51a582f736d6f6aea1286830996c1dc05f860aea2771e5a6b5864162c  src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c
83356733df9d4001ace6f3705593eb5a155480d80e4ece6a4504f2ef0fd248dc  src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c   (C1)
```

---

## 2. 撤回正确性验证（源头级）

### 2.1 P5 撤净 —— 4 个文件里 `MTK AFBC` 归零

```
src/vulkan/runtime/vk_android.c      MTK_AFBC=0   800000000000072=0
src/panfrost/vulkan/panvk_image.c    MTK_AFBC=0   800000000000072=0
src/panfrost/lib/pan_mod.c           MTK_AFBC=0   800000000000072=0
src/panfrost/lib/pan_desc.c          MTK_AFBC=0   800000000000072=0
```

### 2.2 更早的 WSI 补丁（05 号方案 A）**仍在** —— `vk_android.c`

```
self-described count: 1                       ← 与 v54 一致
DRM_FORMAT_MOD_LINEAR count: 3                ← >0 ✔
683: * == NULL ⇒ 空指针），所以这里必须给出 DRM_FORMAT_MOD_LINEAR。
756/761/770/779/785:  mesa_loge("AHB layout fallback: …")
796:   out->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
802-803: mesa_logi("AHB layout fallback: %ux%u fmt=0x%x fourcc=0x%08x pitch=%llu "
                  "usage=0x%llx -> DRM_FORMAT_MOD_LINEAR",
838:   "usage=0x%llx); using self-described LINEAR layout",
```

⇒ 「AHB layout fallback … -> **DRM_FORMAT_MOD_LINEAR**」这条 LINEAR 路径**完整保留**，被撤掉的只是 P5 的 AFBC 覆盖。

### 2.3 P2（tiler heap 续期）**仍在** —— `panvk_vX_gpu_queue.c`

```
63:   #define KBASE_WAIT_TIMEOUT_NS  (10ll * 1000000000ll)      ← 10 秒常量在
759:  uint64_t watchdog = (uint64_t)start + KBASE_WAIT_TIMEOUT_NS;
2815: mesa_logi("kbase: tiler heap renewal (uAPI %u.%u, submits %u, "
2825: "kbase: tiler heap renewal failed");
"tiler heap renewal" count = 2   "KBASE_WAIT_TIMEOUT_NS" count = 2
```

用户门铃**快路径**（`if (*active) { … return; }`）保留 —— 与 v54 相同（对照见 `25-cacheinvalidate-kick-fix.md` §38）。

### 2.4 v58 的 2 行缓存失效确实撤掉，且**只**撤掉了这 2 行

`diff .bak-1791177577( v59 态 ) → 当前`：

```diff
-   kbase_cache_invalidate_range((const void *)active, sizeof(*active));
    if (*active) {
       ...
-      kbase_cache_invalidate_range((const void *)active, sizeof(*active));
       if (*active)
          return;
    }
```

净效果 = 撤回注释块 + 2 行调用；**`if (*active) … return;` 快路径结构不变**。

### 2.5 交叉印证：`gpu_queue.c` == v54 的该文件 + P2，逐位

用第 17 轮的 P2 证据 `17-v54-gpu_queue.diff` 对当前文件做 **reverse-apply**：

```
$ git apply --check --reverse -p0 17-v54-gpu_queue.diff
*** reverse-applies cleanly ***

$ (reverse-apply on a copy)
pre-P2 backup .bak-1791172414 hash = a909a8d5f7165cbd43d4581b7bc6156e5a4d4caaa5f7a4f3783d7e5011c96228
reversed (P2 removed) hash         = a909a8d5f7165cbd43d4581b7bc6156e5a4d4caaa5f7a4f3783d7e5011c96228   ← 相同
```

⇒ 当前 `gpu_queue.c` = **v54 原文件 + P2，别无其它**。v58 的 2 行确实不在。`kbase_cache_invalidate_range` 在该文件里仍有 4 处，那是 **v54 基线自带**（`kbase_subqueue_wait_seqno` 等），非 v58 新增。

### 2.6 C1 仍在（运行时门控，非删除）

C1 是「在 kbase 上」抑制，通过 `cmdbuf_skips_gpu_heap_ops()` 判 `phys_dev->kbase_node_path[0] != '\0'`，
故 `cs_vt_end` / `cs_finish_fragment` / `cs_frag_end` / `TILER_OOM` 的**符号在源码里仍出现**（计数 2/1/10），
但**调用点被条件包住**：

| 位置 | 变化 |
|------|------|
| `prepare_vi()` ×2 处 `cs_vt_end(...)` | → `if (!cmdbuf_skips_gpu_heap_ops(cmdbuf)) cs_vt_end(...)` |
| TILER_OOM handler 注册（两处 `cs_set_exception_handler(...TILER_OOM...)`） | → 包在 `if (!cmdbuf_skips_gpu_heap_ops(...))` 内 |
| `cs_finish_fragment` / `cs_frag_end` 分发 | → `if (cmdbuf_skips_gpu_heap_ops(...)) { /* heap renewal 代管 */ } else if (td_count == 1) …` |

`cs_vt_start` **未**被抑制（保留）✔ —— 与任务描述一致。

---

## 3. ★ 确定性对照（本轮最强证据）

| 步骤 | 源码状态 | `.so` sha256 | 大小 |
|------|----------|--------------|------|
| **① 撤回 P5(4 文件) + v58(2 行)，保留 C1** | 待验证 | — | — |
| **② 撤掉 C1**（`cp .bak-1791177353`）→ `ninja`（`NINJA_EXIT=0`） | **= v54** | **`a9cba64afa935370906e1a8533550816c6a018f4d6808cb8951dd4e1099333f1`** | **20 006 408 B** |
| 　　v54 载荷 `.so`（从 `mgl-panvk-v54.apk` 解出） | = v54 | `a9cba64afa935370906e1a8533550816c6a018f4d6808cb8951dd4e1099333f1` | 20 006 408 B |
| **③ 贴回 C1** → `ninja`（`NINJA_EXIT=0`） | **= v54 + C1** | **`1335b5c07ed28afd93f76edac4e5fc8a6341c5f74346fb57352a506cc4196330`** | **20 007 840 B** |
| **④ 复现性**：`touch` 后二次 `ninja`（`NINJA_EXIT=0`） | 同 ③ | `1335b5c0…` **再次逐位相同** | 20 007 840 B |

> ### ⇒ **「撤 C1 后是否逐位等于 `a9cba64a`」= 是，完全逐位相同（含大小 20 006 408 B）。**
> 这同时证明：**树里无任何残留污染变量**；构建环境对 v54 可**确定性复现**；**C1 是唯一增量**。

对照日志：`/root/research/27-build-control-noc1.log`（无 C1）、`/root/research/27-build-v60.log`（③）、
`/root/research/27-build-v60-repro.log`（④）。
哈希留存：`/root/research/27-work/control-noc1-clean.so.sha256`、`…/v60.so.sha256`、`…/v60.so.md5`、`…/v60.so.size`。

### 3.1 撤回的污染量（字节账）

| 版本 | 组成 | `.so` 大小 | 相对 v54 |
|------|------|------------|----------|
| v54 | 基线 | 20 006 408 | — |
| **v60** | v54 + **C1** | **20 007 840** | **+1 432** |
| v59 | v54 + P2* + P5 + v58 + C1 | 20 012 600 | +6 192 |
| v58 | v54 + P2* + P5 + v58 | 20 011 416 | +5 008 |

（*P2 已在 v54 内。v60 − v59 = −4 760 B = 撤掉的 P5 + v58 缓存失效的体积。P5 = 3 616 B，
v58 2 行 = 1 144 B。）

---

## 4. `readelf -d` 与 v54 逐条比对

```
$ readelf -d v54.so            > v54-readelf-d.txt
$ readelf -d v60.so            > v60-readelf-d.txt
$ diff v54-readelf-d.txt v60-readelf-d.txt
2c2    offset 0x1154ef0  ->  0x11553f0
21c21  PLTGOT  0x1157bc8 ->  0x11580c8
28c28  INIT_ARRAY  0x1156ea8 -> 0x11573a8
30c30  FINI_ARRAY  0x1156e98 -> 0x1157398
```

**只有 4 处绝对偏移变化（整体常数平移 +0x500 = 1 280），标签语义逐条相同：**

| 项 | v54 | v60 | 判定 |
|----|-----|-----|------|
| 条目数 | 32 | 32 | ✔ 相同 |
| NEEDED | 7 条（liblog/libnativewindow/libsync/libm/libz/libdl/libc） | 7 条，同名同序 | ✔ 相同 |
| SONAME | `libvulkan_panfrost.so` | 同 | ✔ |
| RUNPATH | `$ORIGIN/../../android_stub` | 同 | ✔ |
| FLAGS / FLAGS_1 | `SYMBOLIC BIND_NOW` / `NOW` | 同 | ✔ |
| VERNEEDNUM | 3 | 3 | ✔ |
| RELACOUNT | 15730 | （未变，同一 .rela.dyn 起点 0x27c8） | ✔ |

段表（节头偏移）：

| 节 | v54 | v60 |
|----|-----|-----|
| `.rela.dyn` | 0x27c8 | **0x27c8（同）** |
| `.rela.plt` | 0x5eb08 | **0x5eb08（同）** |
| `.rodata` | 0x60100 | **0x60100（同）** |
| `.text` | 0x9ba060 | **0x9ba060（同）** |
| `.data.rel.ro` | 0x10e4bd0 | 0x10e50d0（+0x500） |
| `.data` | 0x1159330 | 0x1159830（+0x500） |

⇒ `.text` / `.rodata` **起点完全未动**（C1 的代码增量被既有对齐吸收），差异集中在 `.data.rel.ro` 之后。
**无新增依赖、无 SONAME 变化、无符号可见性变化** —— 动态装载契约与 v54 等价。

> 注：同目录另存 `v59-readelf-d.txt` 供对照；`27-work/v54-readelf-d.txt` 与仓库内
> `17-readelf-v54.txt` **逐字节相同**（已校验），故本次 v54 参照取样可信。

---

## 5. P5 在**二进制**层是否真的清零

```
$ strings <v60 .so> | grep -c "MTK AFBC"                  →  0        ← 要求 = 0 ✔
$ strings <v60 .so> | grep -c "DRM_FORMAT_MOD_LINEAR"      →  2        ← 要求 >0 ✔
$ strings <v60 .so> | grep -c "AHB layout fallback"        →  6        ← 与 v54 完全一致 ✔
$ strings <v60 .so> | grep -c "self-described"             →  1        ← 与 v54 完全一致 ✔
$ strings <v60 .so> | grep -c "800000000000072"            →  0        ✔

对照 v59（污染件）："MTK AFBC" = 3 ；"AHB layout fallback" = 7   ← 多出的 1 条正是 P5 的 AFBC 回退消息
对照 v54（基线）  ："MTK AFBC" = 0 ；"AHB layout fallback" = 6 ；"self-described" = 1
```

⇒ **P5 清零确认**；WSI 的 LINEAR 回退路径在二进制里与 **v54 计数完全一致**（6 / 1），不是「被撤成没有」而是「回到 v54 的样子」。

---

## 6. v60 APK

* 路径：**`/root/final/mgl-panvk-v60.apk`**
* 大小：**10 191 407 B**；sha256 **`d1848cb66f56dd7f48d2dda7c36ad9458ccffc6816366752f5bf7ca409869f6d`**
* `versionCode` = **60**，`versionName` = `6.0-clean-c1-kbase-heap-suppress`，package `com.dsh.plugin.driver.g720`
* 签名：`CN=DSH Mali Driver, OU=dev, O=dsh, L=CN, ST=CN, C=CN`（同一 keystore）；
  `apksigner verify --print-certs` 通过；`unzip -t` → **No errors detected**
* 打包脚本：`/root/pack_v60.sh`，日志 `/root/research/27-pack-v60.log`（`PACK_EXIT=0`）

### 6.1 载荷校验（`.so` 非空且等于新 `.so`）

```
lib/arm64-v8a/libvulkan_freedreno.so  20 007 840 B
   unzip -p … | sha256sum = 1335b5c07ed28afd93f76edac4e5fc8a6341c5f74346fb57352a506cc4196330
   源 .so    sha256sum     = 1335b5c07ed28afd93f76edac4e5fc8a6341c5f74346fb57352a506cc4196330   ✔ 相等
lib/arm64-v8a/libMobileGL.so          16 956 584 B
   unzip -p … | sha256sum = 72919c73a7e07630f329bb4ad9605c606a9aac9e2fc6596683ee7b9f9c76848b
   /root/v54/…/libMobileGL.so = 72919c73a7e07630f329bb4ad9605c606a9aac9e2fc6596683ee7b9f9c76848b   ✔ v54 原物
classes.dex                                 1 328 B
   unzip -p … | sha256sum = 6bd3abde2c53506f1b54bb88a8069c3cc394c2fb08440e4ca475cbf8fd64f4ad
   /root/v54/classes.dex    = 6bd3abde2c53506f1b54bb88a8069c3cc394c2fb08440e4ca475cbf8fd64f4ad   ✔ v54 原物
   （并与 `unzip -p mgl-panvk-v54.apk classes.dex` 相同）
```

三个载荷字节数均 **≠ 0** ✔。

### 6.2 manifest / env 与 v54 逐字符一致（只差 version*）

manifest **源文件**逐字符 diff（`/root/v54/AndroidManifest.xml` vs 本轮 heredoc，见 `27-work/v60-manifest-src.xml`）：

```diff
3c3
<     package="com.dsh.plugin.driver.g720" android:versionCode="54" android:versionName="5.4-p2-tiler-heap-renew">
---
>     package="com.dsh.plugin.driver.g720" android:versionCode="60" android:versionName="6.0-clean-c1-kbase-heap-suppress">
```

**仅此一行**（= 只有 versionCode/versionName 两个属性）。编译后 `aapt2 dump xmltree` 对比
（`17-v54-manifest.txt` vs `27-v60-manifest.txt`）同样只有 version* 两行不同：

```diff
3,4c3,4
< versionCode=54 / versionName="5.4-p2-tiler-heap-renew"
> versionCode=60 / versionName="6.0-clean-c1-kbase-heap-suppress"
```

* `pojavEnv` / `boatEnv` 环境串**逐字符继承 v54**（`LIBGL_ES=3:POJAV_RENDERER=opengles3:MOBILEGL_BACKEND_TYPE=DirectVulkan:MOBILEGL_ESPRYT_USE_ANGLE=0:MOBILEGL_MAGMA_R11G11B10F_FALLBACK=0:MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log:MESA_DEBUG=1:PANVK_DEBUG=1:LIBGL_DEBUG=1:EGL_LOG_LEVEL=debug`）
* **未添加** `MESA_VK_WSI_HEADLESS_SWAPCHAIN`：`grep -c` = **0** ✔
* `renderer` / `minMCVer` / `maxMCVer` / `fclPlugin` / `des` / `extractNativeLibs` 全部同 v54

---

## 7. 回滚

**改动前快照时间戳：`1791177577`**（v59 态，6 个文件全留，哈希见 `27-work/v59-state.sha256`）。

回到本轮之前（v59 态）：

```bash
cd /root/zenithblue/work/mesa
for f in src/vulkan/runtime/vk_android.c src/panfrost/vulkan/panvk_image.c \
         src/panfrost/lib/pan_mod.c src/panfrost/lib/pan_desc.c \
         src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c \
         src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c; do
  cp -f "$f.bak-1791177577" "$f"
done
cd /root/zenithblue/build/android-v4
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
ninja
```

只回 C1（v60 → v54 基线）：

```bash
cd /root/zenithblue/work/mesa
cp -f src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c.bak-1791177353 \
      src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c
cd /root/zenithblue/build/android-v4 && ninja     # 期望 sha256 = a9cba64a…
```

只把 C1 贴回去（v54 → v60）：

```bash
cd /root/zenithblue/work/mesa
cp -f /root/research/27-work/cmd_draw.C1-on.c \
      src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c
cd /root/zenithblue/build/android-v4 && ninja     # 期望 sha256 = 1335b5c0…
```

**纪律遵守**：构建树为 `/root/zenithblue/work/mesa`；**未**使用 `git checkout/stash/reset`（仅只读 `git status/diff/apply --check`）；
**未**用 `rm -rf` 删既有目录；**未**改 `/root/mesa`、`/root/MobileGL`；**未**操作手机。

---

## 8. 产物索引

| 路径 | 说明 |
|------|------|
| **`/root/final/mgl-panvk-v60.apk`** | **v60 插件 APK**，versionCode 60，`sha256 d1848cb6…` |
| `/root/27-libvulkan_panfrost-v60.so` | v60 驱动 `.so`，`sha256 1335b5c0…`，20 007 840 B |
| `/root/pack_v60.sh` | 打包脚本（可重放） |
| `/root/research/27-work/v60.so.sha256` `.md5` `.size` | v60 三元组 |
| `/root/research/27-work/control-noc1-clean.so.sha256` | ★ 确定性对照哈希 = `a9cba64a…` |
| `/root/research/27-work/control-noc1-clean.so` | 对照 `.so` 实物（= v54） |
| `/root/research/27-work/cmd_draw.C1-on.c` | C1 版源文件存档（`83356733…`） |
| `/root/research/27-work/v59-state.sha256` | 改动前 6 文件哈希（快照 `1791177577`） |
| `/root/research/27-work/v60-sources.sha256` | 改动后 6 文件哈希 |
| `/root/research/27-work/v54-apk/` | 从 v54 APK 解出的参照载荷 |
| `/root/research/27-work/v54-readelf-d.txt` `v60-readelf-d.txt` `v59-readelf-d.txt` | readelf 比对 |
| `/root/research/27-work/v60-manifest-src.xml` | v60 manifest 源（与 v54 只差 1 行） |
| `/root/research/27-build-control-noc1.log` | 对照编译日志（exit 0） |
| `/root/research/27-build-v60.log` | v60 编译日志（exit 0） |
| `/root/research/27-build-v60-repro.log` | 复现性编译日志（exit 0） |
| `/root/research/27-pack-v60.log` | 打包 + 载荷/manifest 校验日志 |
| `/root/research/27-v60-manifest.txt` | v60 APK manifest xmltree dump |

---

## 9. 一句话

**v60 = v54（逐位可复现的干净基线）+ C1（唯一变量）**；撤 C1 即回到 `a9cba64a…`（20 006 408 B，与 v54 载荷逐位相同），
P5 与 v58 缓存失效已全部撤回（`MTK AFBC` = 0，`DRM_FORMAT_MOD_LINEAR` > 0，`AHB layout fallback` 计数回到 v54 的 6），
P2 与 05 号方案 A 的 WSI LINEAR 回退保留，`readelf -d` 语义与 v54 逐条一致。
