# 30 — v63：v54 基线 + C2（干净单变量版本）

**结论：成功。** v63 = **v54 逐位基线 + C2（唯一新变量）**。

证据链（本轮实测，可复现）：
**撤掉 C1 + 撤掉 v58 的 2 行缓存失效后重编 ⇒ 逐位等于 `a9cba64a…`（20 006 408 B，与 v54 载荷 `.so` 完全相同）**；
再**只**把 C2 块（字节级取自 v62 的同一块）贴回 ⇒ v63 = `c03f0e7b…`（20 007 048 B），**二次重编逐位相同**。

* 构建树：`/root/zenithblue/work/mesa`（**不是** `/root/mesa`）
* 构建目录：`/root/zenithblue/build/android-v4`
* 日期：2026-10-05　上一轮：`/root/research/29-v62-c2-tiler-wait.md`
* 纪律：**未**用 `git checkout/stash/reset`；改前均 `cp <f> <f>.bak-$(date +%s)`；未 `rm -rf` 既有目录；未改 `/root/mesa`、`/root/MobileGL`；**未操作手机**。

---

## 0. 结论速览

| 项 | 值 |
|---|---|
| 撤回的变量 | **C1**（kbase heap-op 抑制）、**v58 的 2 行缓存失效**（`kbase_subqueue_publish`） |
| 保留的变量 | **C2**（上游 open MR !44173，`get_tiler_desc()` 顶部 `cs_wait_slots(b, dev->csf.sb.all_iters_mask)`） |
| C2 是否重新贴回 | **是**，且与 v62 中的原块**逐字节相同**（429 B / 10 行），锚点唯一性已断言 |
| **撤 C2 后是否逐位等于 `a9cba64a`** | **是** —— `a9cba64afa935370906e1a8533550816c6a018f4d6808cb8951dd4e1099333f1`，**20 006 408 B**，逐位相同 |
| 编译 exit 0？ | **是**（控制构建 + v63 两次构建，`NINJA_EXIT=0`） |
| v63 `.so` | `c03f0e7b7e391b20edfcadcfc4b69068693dacdb785fb6ad0c6bf6e6fcdb1106`，20 007 048 B |
| v63 APK | `/root/final/mgl-panvk-v63.apk`，`e0c249da36b111b61dfa28df28ad09ff89dd6ecfd1c915c475f38f9349ba3feb`，10 187 311 B |
| 源文件（树内现状） | `cmd_draw.c` = `7527e88e…`（= v54 + C2）；`gpu_queue.c` = `f3bf4ea5…`（**= v54**） |

---

## 1. 备份判定（任务书要求「把判定写进报告」）

**判据说明（重要）**：C2 的独有判据是**归属注释** `upstream MR !44173`；
`cs_wait_slots(b, dev->csf.sb.all_iters_mask)` 的计数**不能**单独作为判据 ——
v54 本身就有一处同形调用（`mark_crc_valid_after_fragment()`，PAN_ARCH==10 的 CRC 有效性路径，
v54 文件 `:1613` → v63 文件 `:1623`；见 29 报告 §相同结论）。任务书原话「`.bak-1791177353` 不含 C2」**判定正确**。

| 文件 | 字节 | mtime | `kbase_node_path` / `cmdbuf_skips_gpu_heap_ops`（C1） | `upstream MR !44173`（C2） | `cs_wait_slots(all_iters)` 计数 | md5 | 判定 |
|---|---|---|---|---|---|---|---|
| `cmd_draw.c.bak-1791177353` | 188 403 | 13:15:53 | **0 / 0** | **0** | 1（既有 CRC 路径） | `ed4341fb…` | **= v54 的 `cmd_draw.c`（清洁基线）** |
| `cmd_draw.c.bak-1791177577` | 189 819 | 13:19:37 | 7 | 0 | 1 | `7922af87…` | = v54 + C1（**无** C2） |
| `cmd_draw.c.bak-1791178999` | 189 819 | 13:43:19 | 7 | 0 | 1 | `7922af87…`（与上一行相同） | = v54 + C1（**无** C2） |
| `cmd_draw.c`（= v62） | 190 248 | 13:44:17 | 7 | **1** | 2（C2 + 既有 CRC 路径） | — | = v54 + C1 + C2 |
| `gpu_queue.c.bak-1791172414` | 121 817 | 11:53:34 | — | — | P2 判据 3 | `ac84fc4c…` | v54 编辑**前**（pre-P2） |
| `gpu_queue.c.bak-1791176223` | 122 818 | 12:57:03 | — | — | P2 判据 4 | `b879e20c…` | **= v54（含 P2）** |
| `gpu_queue.c.bak-1791178351` | 122 818 | 13:32:31 | — | — | P2 判据 4 | `b879e20c…`（同上） | **= v54（含 P2）** |
| `gpu_queue.c`（= v62） | 122 965 | 13:33:28 | — | — | P2 判据 4 | — | = v54 + v58 的 2 行 |

### 1.1 `gpu_queue.c` 备份的**反演证明**（不是靠时间戳猜的）

把 `/root/research/v54_edit.py` 里的 `gq1/gq2/gq3` 三个编辑施加到 **`.bak-1791172414`**（v54 编辑前的 11:53:34 状态）上：

```
OK /tmp/v54chk/gq.c md5=b879e20c6f7c1a4d2775b00ae50d2077
b879e20c6f7c1a4d2775b00ae50d2077  /root/zenithblue/work/mesa/.../panvk_vX_gpu_queue.c.bak-1791176223
```

⇒ **`.bak-1791176223` 逐位等于 v54 的 `gpu_queue.c`**；该文件里 P2 判据齐备：
`KBASE_WAIT_TIMEOUT_NS`（10 s 常量）在 `:63` 定义、`:759` 使用；`kbase_tiler_submit_count` 4 处；
续期路径 `:2802`（submit 间隔判定）/`:2815`、`:2820`（logcat 续期日志）/`:2825`（失败日志）全在。✓

### 1.2 谱系更正（写清以免下一轮再踩）

任务书的公式「v61 = v54 + P2 + C1 + 2 行缓存失效 + C2」**把 P2 重复计了一次**：
P2（tiler heap 续期：去掉死字段 `submit->tiler_work_estimate` 门、按提交数续期、加 logcat）
**本来就包含在 v54 基线内** —— `v54_edit.py` 的注释即写作 `P2 / report 14 fix B`，v54 `.so` 相对 v53 的 +808 B 就是 P2 的新增字符串（见 `17-p2-implementation.md`）。
这不影响任何结论：**v63 = v54 + C2**。

---

## 2. 撤回清单（2 个文件；其余 4 个 P5 文件本来就是 v54 状态）

| # | 文件 | 从 | 到 | 撤回后 sha256 | 与 27 轮记录一致？ |
|---|---|---|---|---|---|
| 1 | `src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c` | v62（C1+C2） | `.bak-1791177353`（v54） | `6b4c154983a8d7481f0d7a5e6a48a525a8f978c58ca9f84d50e4c8a28b24ef94` | — |
| 2 | `src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c` | v62（v54+2 行） | `.bak-1791176223`（v54） | `f3bf4ea51a582f736d6f6aea1286830996c1dc05f860aea2771e5a6b5864162c` | ✓ `27-work/v60-sources.sha256` |
| — | `src/vulkan/runtime/vk_android.c` | 未动（已 = v54） | — | `d582a9f6…` | ✓ |
| — | `src/panfrost/vulkan/panvk_image.c` | 未动（已 = v54） | — | `55c04e76…` | ✓ |
| — | `src/panfrost/lib/pan_mod.c` | 未动（已 = v54） | — | `b3b8c61c…` | ✓ |
| — | `src/panfrost/lib/pan_desc.c` | 未动（已 = v54） | — | `0d362bd9…` | ✓ |

```bash
cd /root/zenithblue/work/mesa
cp -f src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c.bak-1791177353  src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c
cp -f src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c.bak-1791176223 src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c
```

撤回后判据（`30-work/step1-revert-control.sh` 输出）：

```
cmd_draw.c:  kbase_node_path=0  cmdbuf_skips_gpu_heap_ops=0  upstream MR !44173=0
             cs_vt_end=2  cs_finish_fragment=2  cs_frag_end=1  TILER_OOM=10  cs_wait_slots=1(:1613 既有)
gpu_queue.c: KBASE_WAIT_TIMEOUT_NS=2  tiler_submit_count=4  publish 区内 cache_invalidate=0
4 个 P5 文件: MTK AFBC = 0 / 0 / 0 / 0
```

---

## 3. 确定性对照（★ 必验项）

| # | 步骤 | 期望 | 实测 | 大小 | NINJA_EXIT |
|---|---|---|---|---|---|
| ① | **撤 C1 + 撤 2 行缓存失效** ⇒ `ninja` | = **v54** | **`a9cba64afa935370906e1a8533550816c6a018f4d6808cb8951dd4e1099333f1`** | **20 006 408 B** | **0** |
| ② | v54 载荷 `.so`（`/root/v54/lib/arm64-v8a/libvulkan_freedreno.so`，独立复核） | = ①  | `a9cba64a…33f1` | 20 006 408 B | — |
| ③ | **贴回 C2** ⇒ `ninja`（v63） | v54 + C2 | `c03f0e7b7e391b20edfcadcfc4b69068693dacdb785fb6ad0c6bf6e6fcdb1106` | 20 007 048 B | **0** |
| ④ | 复现性：`touch` 源文件后二次 `ninja` | 同 ③ | `c03f0e7b…b1106` **逐位相同** | 20 007 048 B | **0** |

> ### ⇒ 「撤 C2 后是否逐位等于 `a9cba64a`」= **是**，完全逐位相同（含大小 20 006 408 B）⇒ 树里**已无 C1 / 2 行缓存失效 / P5 任何残留**。

产物留存：`/root/research/30-work/control-noC1-noC2.so`（= v54，20006408 B）、`/root/research/30-work/v63.so`（20007048 B）、
`/root/v63/lib/arm64-v8a/libvulkan_freedreno.so`（v63 载荷，可直接换装做 A/B）。

---

## 4. C2 重贴：字节级证据

1. 从 v62 文件取 `:1244-1253`（**10 行 / 429 B**）→ `/root/research/30-work/c2-region.txt`：
```c
（空行）
   {
      /* The tiler heap is shared across render passes; wait for our own
       * prior async tiling work to retire before reprogramming it.
       * (upstream MR !44173 -- "wait for prior tiling work before reusing
       * tiler heap", reported as tile-aligned corruption on Mali-G720) */
      struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
      cs_wait_slots(b, dev->csf.sb.all_iters_mask);
   }
（空行）
```
2. **锚点唯一性断言**：`panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);` ＋ 紧接的 `struct panvk_physical_device *phys_dev =` ⇒ 计数 **= 1**（否则脚本 `sys.exit`）；并断言插入前文件内不含 `upstream MR !44173`。
3. **插入后与 v62 文件的 diff**（`30-work/30-new-vs-v62.diff`）= **恰好 6 个 hunk，全部只涉及 C1**：
```
@@ -241,25 +241,6 @@   @@ -4232,8 +4213,7 @@   @@ -4250,8 +4230,7 @@
@@ -4536,15 +4515,13 @@  @@ -4559,9 +4536,8 @@   @@ -4666,10 +4642,7 @@
cmdbuf_skips_gpu_heap_ops 出现 6 次 ；upstream MR !44173 出现 0 次
```
   ⇒ C2 块在两份文件之间**逐字节相同**（diff 里根本没有 C2 的行）。
4. v63 文件 `:1225-1234` 与 `c2-region.txt` **逐位相同** ✓
   （C2 的 `cs_wait_slots` 在 v63 位于 `:1232`、在 v62 位于 `:1251`，差 19 行 = C1 新函数被撤掉，方向正确）。

---

## 5. `.so` 验证

| 检查 | 期望 | 实测 |
|---|---|---|
| `strings <v63.so> \| grep -c "MTK AFBC"` | 0 | **0** ✓ |
| `strings <v63.so> \| grep -c 44173`（不得泄进二进制） | 0 | **0** ✓ |
| 源文件 `grep -c "kbase_node_path"`（C1 判据） | 0 | **0** ✓ |
| 源文件 `grep -c "cmdbuf_skips_gpu_heap_ops"` | 0 | **0** ✓ |
| 源文件 `grep -c "upstream MR !44173"`（C2 判据） | 1 | **1** ✓（`:1232` 为 C2，`:1623` 为既有 CRC 路径） |
| `cs_vt_end / cs_finish_fragment / cs_frag_end` 调用数 | 2 / 2 / 1（v54 的无条件发射） | **2 / 2 / 1** ✓ |
| `kbase_subqueue_publish`（`:700-745`）内 `kbase_cache_invalidate_range` | 0 | **0** ✓ |
| `readelf -d` vs v54 | 同 32 条目、同顺序，仅 offset 随体积变化 | 只差 4 行 offset 字段（`0x1154ef0→0x1155170`、PLTGOT/INIT_ARRAY/FINI_ARRAY），与 v62 的对照表现一致 ✓ |

---

## 6. APK `/root/final/mgl-panvk-v63.apk`

* versionCode **63**，versionName `6.3-v54-plus-c2`，包名 `com.dsh.plugin.driver.g720`（= v54）
* 载荷 = **新驱动** + **v54 的 `libMobileGL.so` / `classes.dex`**

| 载荷 | 字节（APK 内） | sha256（APK 内 == 源，非空） |
|---|---|---|
| `lib/arm64-v8a/libvulkan_freedreno.so` | 20 007 048 | `c03f0e7b…b1106` == 构建产物 ✓ |
| `lib/arm64-v8a/libMobileGL.so` | 16 956 584 | `72919c73…848b` == `/root/v54/…` ✓ |
| `classes.dex` | 1 328 | `6bd3abde…f4ad` == `/root/v54/classes.dex` ✓ |

其他校验：
* `aapt2 dump xmltree` 与 `17-v54-manifest.txt` 对比 ⇒ **只有 `versionCode`/`versionName` 两行不同**，其余逐字符一致（含 `pojavEnv`/`boatEnv` 调试 env 原样）✓
* `MESA_VK_WSI_HEADLESS_SWAPCHAIN` 计数 = **0** ✓；`PANVK_KBASE_HEAP_RENEW_INTERVAL` 计数 = **0** ✓（两者都**没有**加）
* 包内 `.so` 的 `strings | grep -c "MTK AFBC"` = **0** ✓
* APK sha256 `e0c249da36b111b61dfa28df28ad09ff89dd6ecfd1c915c475f38f9349ba3feb`，10 187 311 B（与 v54/v55/v57 APK 同尺寸档）
* 打包脚本：`/root/pack_v63.sh`（基于 `pack_v62.sh`，仅 version* 与注释不同）；日志 `30-work/pack-v63.log`

---

## 7. 回滚命令（随时可退回 v62；v63 前状态已备份）

```bash
# 退回 v62（含 C1 + 2 行缓存失效 + C2）—— 备份 = 本轮改前的 v62 状态
cd /root/zenithblue/work/mesa
cp -f src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c.bak-1791179657  src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c   # 190248 B
cp -f src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c.bak-1791179657 src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c  # 122965 B
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
cd /root/zenithblue/build/android-v4 && ninja          # 期望 sha256 = 61d35b1c…（v62），20009576 B

# 退回 v54（清洁基线，无 C2）
cp -f .../panvk_vX_cmd_draw.c.bak-1791177353 src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c
cp -f .../panvk_vX_gpu_queue.c.bak-1791176223 src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c && ninja   # 期望 a9cba64a…
```

本轮新增备份（纪律用，勿删）：`panvk_vX_cmd_draw.c.bak-1791179657`（= v62，190248 B）、
`panvk_vX_gpu_queue.c.bak-1791179657`（= v62，122965 B）、以及 step2 的 `cmd_draw.c.bak-<step2 ts>`。
参考副本：`30-work/cmd_draw.v62-keep.c`、`30-work/gpu_queue.v62-keep.c`。

---

## 8. 遗留 / 风险

1. **v63 尚未真机测试** —— 本轮被明确要求「禁止操作手机」。A/B 材料已就位：
   `/root/v63/lib/arm64-v8a/libvulkan_freedreno.so`（v63 = `c03f0e7b…`）与 `/root/v62/lib/…`（v62 = `61d35b1c…`）可直接换装做单变量对照。
2. C2 的注释比上游多 2 行归属说明（上游 8 行 / 本地 10 行），**代码行与上游逐字相同**（见 `29-v62-c2-tiler-wait.md` §与上游 diff）。
3. 本轮未触碰 `kbase_kmod.c`、超时常量（仍 10 s）与其余 P5 文件。
