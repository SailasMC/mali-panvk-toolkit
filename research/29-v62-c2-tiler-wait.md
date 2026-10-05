# 29 — v62 = v61 + C2（上游 open MR !44173：`get_tiler_desc()` 复用 tiler heap 前先等待）

任务：实施型子智能体 / task-29。目标 **v62 = 当前树（v61 状态）+ C2**。
状态：**已完成**（编译 exit 0、确定性对照通过、APK 已出、全部校验通过）。

---

## 0 结论速览

| 项 | 结果 |
|---|---|
| C2 改在何处 | `src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c:1244-1252`（插入块），在 `get_tiler_desc()` 内、`panvk_get_cs_builder()` 之后 |
| 与 22 号报告 / 上游 MR 是否一致 | **一致**（位置、表达式、mask 来源逐字一致；见 §3 的逐条复核） |
| 撤 C2 后是否逐位等于 `1915d16e` | **是，逐位相同**（`1915d16e8a30…`，20 008 768 B） |
| 编译 exit 0？ | **是**（3 次 ninja 全部 exit 0；另真实构建参数 `-fsyntax-only` PAN_ARCH=12/10 均 exit 0） |
| v62 `.so` sha256 | `61d35b1cd82c72f5c507811665db2ffd43fd0e2259939f3a0f62343f504dfa83`（20 009 576 B） |
| v62 `.apk` sha256 | `36fe040f1433924c48c83f0c061de9833937eade9a3e89032b92f85f5152f884`（10 191 407 B） |

> **本轮未上真机**（任务明令「禁止操作手机」，且交付物只到 APK）。C2 的**效果**属于下一轮真机 A/B 的事。

---

## 1 交付物

| 文件 | sha256 | 字节 |
|---|---|---|
| `/root/final/mgl-panvk-v62.apk` | `36fe040f1433924c48c83f0c061de9833937eade9a3e89032b92f85f5152f884` | 10 191 407 |
| 新驱动（APK 内 `lib/arm64-v8a/libvulkan_freedreno.so`） | `61d35b1cd82c72f5c507811665db2ffd43fd0e2259939f3a0f62343f504dfa83` | 20 009 576 |
| 构建产物 `/root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so` | `61d35b1cd82c72f5c507811665db2ffd43fd0e2259939f3a0f62343f504dfa83` | 20 009 576 |
| 打上的源码 `/root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c` | `ab5bddab05dd14a73179155d78c94597ae9f8e099a60be68b7e06a74790aa6d4` | 189 985 |
| 改前备份 `…/panvk_vX_cmd_draw.c.bak-1791178999` | `83356733df9d4001ace6f3705593eb5a155480d80e4ece6a4504f2ef0fd248dc` | 189 819 |
| 打包脚本 `/root/pack_v62.sh` | `b28447debbb9baa1fbf791cd2104255f2874b720923f073237aaa4c370eab7dd` | — |

日志与中间产物：`/root/research/29-work/`（`29-build-v62.log`、`29-build-control-noC2.log`、`29-build-v62-repro.log`、`c2-delta.diff`、`c2-only.diff`、`inserted-region.txt`、`cc_cmd.sh`、`v62.so.sha256`、`control-noC2.so.sha256`、`v62-repro.so.sha256`、`bak_ts.txt`、`29-readelf-v62.txt`），打包日志 `/root/research/29-pack-v62.log`，manifest dump `/root/research/29-v62-manifest.txt`。

---

## 2 C2 改在何处（文件:行号 + 插入内容）

**文件**：`src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c`
**函数**：`get_tiler_desc()`（定义在 `:1233-1234`）
**锚点**：`git apply -p1` 报 `Hunk #1 succeeded at 1241 (offset 19 lines)`，即上游 hunk 头 `@@ -1222,6 +1222,14 @@` 在本树落地为 `@@ -1241,6 +1241,16 @@`。
**落地行号**：`1244-1252`（插入的 9 行块本身；含前后空行则 1241→1253）。

插入内容（本树 1242-1256 实际文本）：

```c
1242:   struct cs_builder *b =
1243:      panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
1244:
1245:   {
1246:      /* The tiler heap is shared across render passes; wait for our own
1247:       * prior async tiling work to retire before reprogramming it.
1248:       * (upstream MR !44173 -- "wait for prior tiling work before reusing
1249:       * tiler heap", reported as tile-aligned corruption on Mali-G720) */
1250:      struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
1251:      cs_wait_slots(b, dev->csf.sb.all_iters_mask);
1252:   }
1253:
1254:   struct panvk_physical_device *phys_dev =
1255:      to_panvk_physical_device(cmdbuf->vk.base.device->physical);
```

**插入内容摘要**：在 `get_tiler_desc()` 取到 vertex/tiler 子队列的 `cs_builder` 之后、任何 tiler-desc/tiler-heap 上下文编程之前，插入一个作用域块：取 `struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device)`，然后 `cs_wait_slots(b, dev->csf.sb.all_iters_mask)` —— 即**在复用/重新编程 tiler heap 前，先等本子队列此前发出的异步 tiling 工作（所有 iteration 记分板槽）退休**。`dev` 被限制在块作用域内，不与函数后文的任何同名变量冲突。

`git diff` 显示这是一处**纯新增块**：`-1222,6 +1241,16`，无删改行（`c2-only.diff` 里的其他 hunk 是 v61 既有的 P2/C1 改动，非本轮引入）。

---

## 3 复核结论（是否与报告描述一致）

任务要求自行复核「插入位置」与「`all_iters_mask` 的来源」。逐条如下：

### 3.1 与上游 MR 的逐字比对 —— 我额外从上游拉了原始 diff

```
curl -s https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/44173.diff
```

上游原文（**2026-09-02，open，标题 panvk/csf: wait for prior tiling work before reusing tiler heap**）：

```
@@ -1222,6 +1222,14 @@ get_tiler_desc(struct panvk_cmd_buffer *cmdbuf)
 
    struct cs_builder *b =
       panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);
+
+   {
+      /* The tiler heap is shared across render passes; wait for our own
+       * prior async tiling work to retire before reprogramming it. */
+      struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
+      cs_wait_slots(b, dev->csf.sb.all_iters_mask);
+   }
+
    struct panvk_physical_device *phys_dev =
       to_panvk_physical_device(cmdbuf->vk.base.device->physical);
```

**结论**：本地补丁 `/root/research/vertex-work/mesa-mr44173-tiler-heap-wait.diff`（822 B）与上游 **逐字一致**，唯一差别是本地版在注释里多插了 2 行归属说明（`(upstream MR !44173 -- …)`），故上游是 `+1222,14`（8 行）、本地是 `+1222,16`（10 行）。**代码行 `struct panvk_device *dev = …` 与 `cs_wait_slots(b, dev->csf.sb.all_iters_mask);` 与上游完全相同。**

### 3.2 插入位置 ✓ 与报告一致

- 报告 §C2 写「`panvk_vX_cmd_draw.c:1215`（`get_tiler_desc()`），在 `struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);` 之后插入」。
- 本树实测：函数 `get_tiler_desc` 在 `:1234`；插入块在 `:1245-1252`，**紧接** `panvk_get_cs_builder()`（`1242-1243`）、**紧在** `struct panvk_physical_device *phys_dev`（`1254`）之前。语义位置与报告/上游**完全一致**。
- **行号说明（唯一的表述偏差）**：报告写 `:1215`、上游 hunk 头写 `-1222`、本树落地 `1244/1241`。三者差 ~19-22 行，原因是**本树在同一个文件的上方多出 P2/C1 的改动**（例如 `cmdbuf_skips_gpu_heap_ops()` 新增 ~19 行注释+函数，位于 `:244` 附近），使下方所有行号整体下移。`git apply` 以上下文匹配，落地位置正确；这属于**行号漂移，不是位置偏差**。报告里 `:1215` 是「约」值，不必修正，但引用时建议用「函数内 + 锚点」定位。

### 3.3 `all_iters_mask` 来源 ✓ 与报告/上游一致

- 表达式 `dev->csf.sb.all_iters_mask` 中 `dev = to_panvk_device(cmdbuf->vk.base.device)`（`struct panvk_device *`）。
- 字段声明：`src/panfrost/vulkan/panvk_device.h:130-132`，匿名结构 `sb { uint16_t all_mask; uint16_t all_iters_mask; }`。
- 赋值处：`src/panfrost/vulkan/panvk_vX_device.c:535-547`：
  ```c
  device->csf.sb.count        = csif_info->scoreboard_slot_count;   /* ≤16 */
  device->csf.sb.all_mask     = BITFIELD_MASK(count);
  assert(count > PANVK_SB_ITER_START);                              /* = 3 */
  device->csf.sb.iter_count   = count - PANVK_SB_ITER_START;
  #if PAN_ARCH == 10
  device->csf.sb.iter_count   = MIN2(iter_count, PANVK_SB_ITER_COUNT);  /* = 5 */
  #endif
  device->csf.sb.all_iters_mask = BITFIELD_RANGE(PANVK_SB_ITER_START, iter_count);
  ```
  常量：`PANVK_SB_ITER_START = 3`、`PANVK_SB_ITER_COUNT = 5`（`csf/panvk_cmd_buffer.h:282-283`）。
- 即 `all_iters_mask` = **全部「异步 iteration」记分板槽的位掩码**（G720/arch10：`iter_count = min(count-3, 5)`，典型 8 槽设备 → bits 3..7 = `0xf8`），正是「等待本子队列此前发出的所有异步 tiling 工作」所需的掩码。语义与报告「复用 tiler heap 前等待先前 tiling 工作退休」**吻合**。
- 交叉印证：本树 `mark_crc_valid_after_fragment()`（`panvk_vX_cmd_draw.c:1642`）早就有同形态的 `cs_wait_slots(b, dev->csf.sb.all_iters_mask);`（PAN_ARCH==10 的 CRC 有效性路径），说明该掩码在本驱动里就是「等异步 tiling 收敛」的既有惯用法。

### 3.4 语法/编译复核 ✓

- 用真实构建参数（取自 `compile_commands.json` 里 `csf/panvk_vX_cmd_draw.c` 且 `PAN_ARCH=12` 的那条命令）改成 `-fsyntax-only` 跑 `PAN_ARCH=12` 与 `PAN_ARCH=10`：**两次都 exit 0**（无 warning 输出）。
- 3 次完整 ninja 构建全部 **exit 0**（每次 12 步：v10..v14 的 `cmd_draw.c` 重编 + 5 个静态库重链 + `.so` 重链），只见既有的 `-Wc23-extensions`（`panvk_vX_gpu_queue.c:820`，P2 引入，与 C2 无关）。

### 3.5 二进制定证 —— C2 确实进了 code，且没动 ABI

对比 v61 payload 与 v62 payload（`objdump -h` / `readelf --dyn-syms`）：

| section | v61 | v62 | Δ |
|---|---|---|---|
| `.text` | `0x729580` (7 507 328 B) | `0x729810` (7 508 368 B) | **+0x290 = +656 B** |
| `.relro_padding` | `0x3e0` | `0x150` | −656 B（对齐填充被吸收） |
| 其余全部 section | — | — | **逐字节同尺寸（diff 为空）** |
| `.dynsym` | — | — | **完全一致**（导出符号表零变化） |
| 文件总长 | 20 008 768 | 20 009 576 | +808 B |

`+656 B .text` = 5 个 arch 变体（v10..v14）各自的 `get_tiler_desc()` 里内联了这条 `cs_wait_slots`（`cs_wait_slots` 是 `genxml/cs_builder.h:935` 的 static inline），量级自洽；`.dynsym` 零变化说明**没有改动对外 ABI / 入口点**。

---

## 4 构建与确定性对照（实测）

构建命令（每次）：

```bash
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
cd /root/zenithblue/build/android-v4
ninja src/panfrost/vulkan/libvulkan_panfrost.so
```

| # | 树状态 | `cmd_draw.c` sha256 | ninja | `.so` sha256 | 字节 | 判定 |
|---|---|---|---|---|---|---|
| 1 | **C2 ON** | `ab5bddab…` | exit 0 | `61d35b1c…dfa83` | 20 009 576 | v62 |
| 2 | **撤 C2**（`cp` 回 `.bak-1791178999`，`touch` 强制重编） | `83356733…` 与改前一致 | exit 0 | `1915d16e8a3096c5d44cf039510e4dcb4735da32c3165ee5d9214ea64081cd89` | 20 008 768 | **逐位等于 v61 ✓ PASS** |
| 3 | **再贴 C2**（同一 patch 二次 `git apply`） | `ab5bddab…` 与 #1 一致 | exit 0 | `61d35b1c…dfa83`（与 #1 相同） | 20 009 576 | **可复现 ✓ PASS** |

- #2 不仅 sha256 相同，**字节数也相同**（20 008 768），即撤掉 C2 后构建**逐位回到 v61**，说明本轮唯一变量就是 C2、且构建链确定性成立。
- #2 时另做标记复核：`upstream MR !44173` 注释计数 = **0**（C2 已彻底移除），而 `cs_wait_slots(b, dev->csf.sb.all_iters_mask);` 计数 = 1（那是 `:1642` 既有的 CRC 路径，非 C2 残留）—— 排除了「revert 不干净」的假 PASS 可能。
- #3 的 `git apply` 是**同一补丁文件二次应用**，`git apply` 不依赖上一次状态。
- 另注（非问题）：`ninja -n` 会报 17 步待做，根因是 `src/git_sha1.h` 是 always-stale（PHONY）生成头，`ninja -d explain` 显示 `src/git_sha1.h is dirty` 传播到 `panvk_vX_physical_device.c` 等消费者；真实构建靠 meson 的 **restat** 剪掉这些边（日志里 `[1/22]` → `[2/12]` 即此现象），故实际每次只重编 12 步。**构建图本身是干净的**，`1915d16e` 的可复现性不受影响。

---

## 5 APK 校验（`/root/final/mgl-panvk-v62.apk`）

打包方式完全沿用 `pack_v61.sh` 的流程（`aapt2 link` → `zip` 追加 `lib`+`classes.dex` → `zipalign -p 4` → `apksigner` 同 keystore `/root/dsh-driver.keystore` / alias `dshdriver`），新脚本 `/root/pack_v62.sh`；其 manifest 块与 `pack_v61.sh` 的 diff **仅第 4 行 versionCode/versionName**（已实测）。

| 校验 | 期望 | 实测 | 判定 |
|---|---|---|---|
| badging versionCode / versionName | `62` / `6.2-c2-tiler-heap-wait` | `versionCode='62' versionName='6.2-c2-tiler-heap-wait'` | ✓ |
| APK 内 payload `.so` sha256 | 等于新 `.so`，非空 | `61d35b1c…dfa83` = 构建产物 | ✓ |
| APK 内 `.so` 字节数 | 非 0 | 20 009 576 | ✓ |
| APK 内 `libMobileGL.so` | = v54 | `72919c73a7e07630f329bb4ad9605c606a9aac9e2fc6596683ee7b9f9c76848b` = `/root/v54/…` | ✓ |
| APK 内 `classes.dex` | = v54 | `6bd3abde2c53506f1b54bb88a8069c3cc394c2fb08440e4ca475cbf8fd64f4ad` = `/root/v54/classes.dex` | ✓ |
| manifest 与 v54 逐字符对照 | **只**差 version* | diff 仅 `versionCode 54→62` + `versionName` 两行 | ✓ |
| `MESA_VK_WSI_HEADLESS_SWAPCHAIN` | 0（未添加） | grep -c = 0 | ✓ |
| `strings <packaged .so> \| grep -c "MTK AFBC"` | 0（P5 仍撤回） | 0 | ✓ |
| `readelf -d` 与 v54 对照 | 一致 | 32 项、NEEDED 全同；仅 4 处**地址偏移**随体积变化（`0x1154ef0→0x1155a70` 等），无条目增删 | ✓ |
| 签名 | 可安装 | `apksigner` 成功（`apk.idsig` 另行生成） | ✓ |

**载荷确认**：APK = 新驱动（v61+C2）+ v54 的 `libMobileGL.so` + v54 的 `classes.dex`；manifest/env 与 v54 逐字符一致（**未**加 `MESA_VK_WSI_HEADLESS_SWAPCHAIN`）。

---

## 6 回滚命令

C2 是**单点、单文件、可精确回滚**的：

```bash
# 方式 A（推荐，逐位回到 v61 已实测）
cp -f /root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c.bak-1791178999 \
      /root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
cd /root/zenithblue/build/android-v4 && ninja src/panfrost/vulkan/libvulkan_panfrost.so
# 预期：1915d16e8a3096c5d44cf039510e4dcb4735da32c3165ee5d9214ea64081cd89  20008768 B  (= v61)

# 方式 B（反向打补丁，等效）
cd /root/zenithblue/work/mesa
git apply -R -p1 /root/research/vertex-work/mesa-mr44173-tiler-heap-wait.diff
```

回滚到 v61 后若需重出 APK：`/root/pack_v61.sh`（输出 `/root/final/mgl-panvk-v61.apk`，sha256 `4c1443ed03fa23f9e6ed61db48cdf9d6ff9d10839e92b8c579f6980d4560291a`）。

**已遵守的纪律**：全程使用 `/root/zenithblue/work/mesa`（未碰 `/root/mesa`、`/root/MobileGL`）；每次 `export PATH=/opt/android-ndk-r27c/…`；改前 `cp` 出 `.bak-$(date +%s)`；未用 `git checkout/stash/reset`；未 `rm -rf` 既有目录；未操作手机。

---

## 7 交给下一轮的话（重要，别误判）

1. **C2 与 v61 的卡住问题是正交的**。v61 真机症状是 `timeout on subqueue`、subqueue 1/2 的 `extract` 停在 wrapper 的 CALL **之前**（很早就发生）——那是**门铃/唤醒（fast-path lost wakeup）**路径；C2 改的是 `get_tiler_desc()` 里对 **tiler heap 世代**的等待，**完全不触及**该路径。因此：**不要期待 v62 修掉 `timeout on subqueue`**；若 v62 仍卡，那是**预期内**的，不能据此否定 C2。
2. C2 的可观测价值在**画面正确性**（tile 对齐错乱 / 巨大三角 / 斜切板条），与 22 号报告的判别表一致：`sub 0..3` 全 OK 但 `VA_REPEAT=400` 出现 `DRIFT` ⇒ 指向 tiler heap 世代问题（C1/C2 域）。
3. 上游 MR !44173 **仍是 open、未合并**；我们这棵树 `1915d16e` 之后没有别的相关提交。C2 是「第三方在 Mali-G720 MC10 上复现」的**加剧项/候选主因**，不是已定论的主因（跑通方没有它也工作）。
4. 已备好的 A/B 材料：v61 与 v62 的 `.so` 都在服务器上（`/root/v61/lib/arm64-v8a/libvulkan_freedreno.so` = `1915d16e…`，`/root/v62/lib/arm64-v8a/libvulkan_freedreno.so` = `61d35b1c…`），可直接换装做单变量真机对照。
5. **未验证项（诚实标注）**：本轮**没有**任何真机运行数据；C2 是否降低崩溃率/改善画面，**必须**下一轮真机 A/B 才能定论。另：C2 会引入一次额外的记分板等待（略微串行化），如真机出现性能回退，用 §6 的命令即可一键回滚。
