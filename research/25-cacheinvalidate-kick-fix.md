# 25 · CS_ACTIVE 缓存失效修复（保留快路径）→ v58

> **对象**：真机 OPPO PHZ110 / MT6989 / Immortalis-G720 MC12 / arch v12 / Android 16 / 无 root / kbase CSF uAPI **1.21**。
> **上游依据**：`/root/research/23-completion-timeout-crash.md` §2（`:716-725` 快速路径无缓存失效）、§5.1（先例对照）、§6 Fix 1 的"外科版"。
> **本轮任务**：回滚 v57 的删除 → 保留快路径 → **只加"读 `CS_ACTIVE` 前失效缓存"这一处最小改动** → 超时常量恢复 10 s → 打 v58。
> **纪律**：未 `git checkout/stash/reset`（树内 60 项未提交改动原样保留，本轮只新增 2 个未跟踪备份文件）；未 `rm -rf`；
> 未改 `/root/mesa`、`/root/MobileGL`；**未操作手机**（无 adb、无安装、未碰三件套）。
> **本轮新增写入**：`/root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c`（唯一改动的源文件）、
> `/root/research/25-*`、`/root/pack_v58.sh`、`/root/v58/**`、`/root/v58-libvulkan_panfrost.so`、`/root/final/mgl-panvk-v58.apk`。
> **标注**：【实测】= 真机 logcat 证据（父代理给定）；【已定论】= 有 file:line 或命令输出；【推断】= 演绎。

---

## 0. 一页结论

| 项 | 值 |
|---|---|
| 改动文件 | `src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c`（唯一） |
| 改动函数/位置 | `kbase_subqueue_publish()`（起 `:697`），**只在两处读 `*active` 之前各加一条失效**：**`:733`** 与 **`:737`** |
| 快路径 | **完整保留**（`:734-740`，文字与 v56/上游逐字相同） |
| `CS_ACTIVE` 首读 | 改动前直接读 `*active` ⇒ 改动后先 `kbase_cache_invalidate_range(active, 4)` 再读 |
| 硬件门铃后的二次读 | 同上，**两次读都失效** |
| 超时常量 | `:63` `KBASE_WAIT_TIMEOUT_NS (10ll * 1000000000ll)` —— **已从 v57 的 120 s 恢复 10 s** ✔ |
| 确定性对照 | 回滚 v56 源码 → 增量重编 → `.so sha256 = d0476a0c155af5…` = **任务给定 v56 基线，逐位相同** ✔ |
| 编译 | `ninja` **exit 0**（12 步，唯一告警是改动前既有的 `-Wc23-extensions`，行号随改动位移到 `:834`） |
| 新 `.so` | `libvulkan_panfrost.so`，**20 011 416 B**，`sha256 ac198f58…`，`md5 2f77a52b…`（副本 `/root/v58-libvulkan_panfrost.so`） |
| v58 可复现性 | 同一源码再编一次 → `sha256 ac198f58…` **再次逐位相同** ✔ |
| APK | `/root/final/mgl-panvk-v58.apk`，10 191 407 B，`sha256 9df71f79…`，versionCode **58**，versionName `5.8-fix1-csinvalidate` |
| 载荷校验 | `unzip -p … libvulkan_freedreno.so \| sha256sum` = `ac198f58…` = 新 `.so`；`bytes = 20011416 ≠ 0` ✔；`libMobileGL.so`/`classes.dex` 与 v54 原物**逐字节相同** ✔ |
| manifest | 与 v54（`/root/research/17-v54-manifest.txt`）**只差 versionCode/versionName 两行**；`MESA_VK_WSI_HEADLESS_SWAPCHAIN` 命中 **0** ✔ |

---

## 1. 为什么"删掉快路径"是错的（实测依据，本轮不回退此结论）

| 版本 | `kbase_subqueue_publish()` | 超时 | 真机结果 | 有无 GPU 故障 |
|---|---|---|---|---|
| **v54**（`a9cba64a`） | **有**快路径 | 10 s | 进主界面、**FPS 51**、稳定约 **10 s** 后崩（用户态看门狗 → `VK_ERROR_DEVICE_LOST`） | **无** |
| **v57**（`3a76cce8`） | **删除**，每次 publish 无条件 `KICK` | 120 s | **约 5 s 就崩**，且更早 | **有**：`E/MESA: kbase: CSF group 0/1/2 fatal error: status 0x08c003c3 (exception 0xc3), sideband 0x0000005fd49d3400` |

`0xC3` = MMU **`TRANSLATION_FAULT_3`**，即 **CSF 固件读了一个未映射的 VA**。这是 **CS 组级致命错误**，
不是用户态超时；它证明"每次提交都强推 `KBASE_IOCTL_CS_QUEUE_KICK`"这条路径在本机（kbase uAPI **1.21**）
会破坏固件/CSG 状态，**比原症状更早、更硬**。

⇒ **结论（沿用父代理口径）**：这条用户门铃快速路径在本机是**必需且有用**的，不能删。
先例 B（`BkaNeko/panvk-mtk-driver`，G610 / uAPI **1.18**）删它跑通，**不能外推到本机**：
两者 kbase uAPI 版本不同（1.18 vs 1.21），CSG 调度/门铃语义已经变了。
v57 的失败**不否证**报告 23 §3.1 的"陈旧 `active` ⇒ 丢唤醒"假说，它否证的只是"**直接删掉**"这个修法。

---

## 2. 先例复读：`:716-725` 这一处，两个先例各自怎么写的

| 先例 | 位置 | 写法 | 对本机的可移植性 |
|---|---|---|---|
| **panvk-mtk**（`/root/panvk-mtk/patches/panvk_mtk.patch:189-207`，G610/uAPI 1.18） | `kbase_subqueue_publish()` | **整段删除**，改为无条件 `kbase_kmod_csf_queue_kick()`；同补丁把超时 10 s→120 s | ✗ **本机实测更糟**（§1，0xc3）。已由 v57 证伪 |
| **wonderkast02**（G720，报告 23 §5.2 A2） | 同函数 | **逐字保留**快速路径，**没有**任何额外的 barrier/失效 | ✓ 保留是对的，但它**也没给出**"正确的失效写法" |

复核（本轮实读）：

```
$ grep -n 'CS_ACTIVE\|cache_invalidate\|doorbell\|active' /root/panvk-mtk/patches/panvk_mtk.patch
189:-   /* Active queues can consume a userspace doorbell without an ioctl.  Check
...
197:-   if (*active) {
...
204:+   /* Always kick the scheduler. …
```

⇒ **两个先例在"读 `CS_ACTIVE` 之前"都没有写任何失效**：一个删掉，一个照抄。
所以"正确的缓存失效/读序"只能取**本树自己已经建立、且已被本机验证过的模式**：

* 本树读 GPU 写、CPU 读的内存前，**统一用 `kbase_cache_invalidate_range()`**（`:182-194`：逐 cacheline `dc civac` + `dsb sy`）——
  这就是树内"已提供的访问器"，**不自造 barrier**；
* 同一文件里最贴近的先例是**等待循环**：`:804` 在读 `cell->seqno`（GPU 用 `SYNC_ADD64` 写的完成单元）**之前**先失效：
  `kbase_cache_invalidate_range((const void *)cell, kbase_seqno_stride());`（在 `:803` 之前的 `:804` 行，v58 源）；
* `kbase_gpu_wmb()`（`:148-157`）**只是 `dsb sy`**，语义是"把 CPU 的写排空到一致性点"，**不做任何失效**；
  因此"读之前需要失效"这件事，树内既有的唯一正确写法就是调 `kbase_cache_invalidate_range()`。

⇒ **v58 采用**：**保留快速路径原样**，在其**两次 `*active` 读之前各插一条
`kbase_cache_invalidate_range((const void *)active, sizeof(*active));`**。
安全性（为什么 `dc civac` 在这一页不会丢数据）：整个 user_io **output 页**（`user_io + 8192`）
在本树里**只有 CPU 读、没有任何 CPU 写**——`grep` 全部引用：`:352-355`（超时日志，读）、
`:729-731`/`:734-739`（publish，读）、`:785-789`（等待循环，读）。`CS_EXTRACT @0x0` 与
`CS_ACTIVE @0x8` 同处一条 64 B cacheline，**两者都是固件/内核写的**，失效该行不可能丢掉 CPU 脏数据。
（对照：CPU 自己写的 `CS_INSERT` 在 **input 页**，硬件门铃在 **page 0**，都不在这条线上。）

---

## 3. 确切改动（file:line，前 → 后）

文件：`/root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c`
函数：`kbase_subqueue_publish()`（定义起 `:697`）
全文 diff：`/root/research/25-fix.diff`（`diff -u` v56 源码 vs v58 现状，未截断）

### 3.1 新增的两条失效（本轮的**唯一**代码改动）

```c
   kbase_gpu_wmb();
   uint8_t *output_page = (uint8_t *)subq->kbase.user_io + 8192;
   volatile uint32_t *active =
      (volatile uint32_t *)(output_page + CS_USER_IO_OUTPUT_CS_ACTIVE);

   kbase_cache_invalidate_range((const void *)active, sizeof(*active));   /* ★ :733 新增 */
   if (*active) {                                                          /*    :734 首读 */
      *(volatile uint32_t *)subq->kbase.user_io = 1;
      kbase_gpu_wmb();
      kbase_cache_invalidate_range((const void *)active, sizeof(*active));/* ★ :737 新增 */
      if (*active)                                                         /*    :738 二次读 */
         return;
   }

   kbase_kmod_csf_queue_kick(dev->kmod.dev, subq->kbase.ringbuf_dev);      /*    :742 兜底 kick（保留） */
```

| 行 | 改动 |
|---|---|
| **`:733`** | **新增** `kbase_cache_invalidate_range((const void *)active, sizeof(*active));`（首读前） |
| **`:737`** | **新增** 同一条（硬件门铃写完之后、二次读之前）——"写门铃 → `dsb sy` → 失效 → 再读"的读序 |
| `:734` / `:738` | `if (*active)` 两次读**原样保留**（v56 逐字） |
| `:742` | `kbase_kmod_csf_queue_kick()` 兜底**原样保留** |

### 3.2 注释（`:711-727`，只加注释，不改语义）

在原有 `/* Active queues can consume a userspace doorbell … */` 后追加说明：
`CS_ACTIVE` 由固件/内核写、CPU 缓存可能陈旧；`kbase_gpu_wmb()` 只是 `dsb sy`、**不失效**；
陈旧非 0 ⇒ 跳过 kick ⇒ 丢唤醒 ⇒ 10 s 看门狗超时（"extract halted, insert > extract"）；
**保留**快路径（删它会触发 0xc3 MMU translation fault）；失效写法对齐 `:804`。

### 3.3 超时常量：**已恢复 10 s**

| | 行 | 内容 |
|---|---|---|
| v56（回滚后） | `:63` | `#define KBASE_WAIT_TIMEOUT_NS  (10ll * 1000000000ll)` |
| v57 | `:68` | `(120ll * 1000000000ll)` |
| **v58** | **`:63`** | **`(10ll * 1000000000ll)`** ✅（回滚 v56 时一并回来，未再改动） |

复核命令与输出：

```
$ grep -n "define KBASE_WAIT_TIMEOUT_NS" panvk_vX_gpu_queue.c
63:#define KBASE_WAIT_TIMEOUT_NS  (10ll * 1000000000ll)
```

### 3.4 源码指纹

```
$ sha256sum src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c
abe2b41a705a3e966d37b8d6c6fac0c31ffb2230fd7496ab1674c6205e34e70a   ← v58 现状
```

| 源码快照 | 路径 | sha256 |
|---|---|---|
| v56（快路径 + 10 s，无失效） | `…/csf/panvk_vX_gpu_queue.c.bak-1791176223` | `f3bf4ea51a582f73…` |
| v57（删快路径 + 120 s） | `…/csf/panvk_vX_gpu_queue.c.bak-v57-1791177010` | `647e26c9a4d4f6e6…`（= 报告 24 记录的 v57 源） |
| **v58（快路径 + 失效 + 10 s）** | `…/csf/panvk_vX_gpu_queue.c.v58-csinv` | **`abe2b41a705a3e96…`** |

**其它文件一行未动**：`kbase_kmod.c`（`csi_handlers` 仍为 `BASE_CSF_TILER_OOM_EXCEPTION_FLAG`）、
`cell->error` 判定、`tiler_work_estimate`、诊断日志 —— 全部保持 v56 状态（`git status --porcelain` 60 → 62 项，
新增的 2 项就是本轮的 `.bak-v57-1791177010` 与 `.v58-csinv` 两个未跟踪文件）。

---

## 4. 确定性对照（必查项）

该构建树此前已被报告 24 证明是**确定性**的；本轮**重新做了一遍 v56 方向的对照**，而不是引用旧结论：

```
① 回滚源码：cp -f panvk_vX_gpu_queue.c.bak-1791176223 panvk_vX_gpu_queue.c
   → sha256(f3bf4ea51a582f736d6f6aea1286830996c1dc05f860aea2771e5a6b5864162c) 校验通过
   → grep: :63 = 10ll*1e9 ✔   grep: :719/:722 快路径在 ✔
② 增量重编：ninja src/panfrost/vulkan/libvulkan_panfrost.so
   → NINJA_EXIT=0   （日志 /root/research/25-build-v56-recheck.log）
   → sha256 = d0476a0c155af5debbd9cf3a7eee4b8aaab1b6a70bb1947901c2a959c6a9dd1a
              == 任务给定 v56 基线 d0476a0c  ← 逐位相同 ✔（size 20 010 648 B，与报告 24 基线一致）
③ 只加 §3.1 的两行失效（+注释）→ sha256(源码) = abe2b41a…
④ 增量重编：NINJA_EXIT=0（/root/research/25-build-v58.log）
   → sha256 = ac198f581a76669eca2688f6c1a723fc8505a1c1af66bf506d9c5f4ae5013c2d（size 20 011 416 B）
⑤ 再用同一 v58 源码重编一次（touch 后重跑 ninja，/root/research/25-build-v58-recheck.log）
   → sha256 = ac198f58…  ← 逐位相同 ✔（v58 亦可复现）
```

⇒ **① ⇒ ② 证明"回滚是干净的、且基线与任务给定值逐位吻合"**；
**③ ⇒ ④/⑤ 证明 v58 相对 v56 的二进制差异只可能来自 §3.1 的两条失效与注释**。

---

## 5. ELF 证据（v56 → v58）

```
$ diff /root/research/25-sec-v56.txt /root/research/25-sec-v58.txt
  [13] .eh_frame   PROGBITS  … 086280 → 0862a8   (+0x28)
  [14] .text       PROGBITS  … 7298b8 → 729c80   (+0x3c8 = 968 B)
  [17] .data.rel.ro          0722c8 → 0722c8   （大小不变，地址随 .text 下移）
  [24] .data      PROGBITS   … 009290 → 009290   （大小不变）
  [25] .bss       NOBITS     003568 → 003568    （大小不变）
  其余差异只有 section 偏移/`.strtab`/`.symtab` 因体积增长而下移
```

* **`.text` 增大 968 B**：正好对应"5 个 arch 变体（v10…v14）各内联两份
  `kbase_cache_invalidate_range()` 的 `dc civac` 循环"这一类纯代码增长；
* **`.data` / `.bss` / `.rodata` 体积不变** ⇒ 没有新增全局状态、没有改数据布局；
* `readelf -h`：ELF64 / AArch64 / DYN，未变。

---

## 6. 编译证据

```
$ cd /root/zenithblue/build/android-v4
$ export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
$ ninja src/panfrost/vulkan/libvulkan_panfrost.so
NINJA_EXIT=0            （/root/research/25-build-v58.log 末尾：[12/12] Linking target …libvulkan_panfrost.so）
```

唯一告警（**改动前既有**，非本轮引入，仅行号位移）：

```
../../work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c:834:7: warning:
  label followed by a declaration is a C23 extension [-Wc23-extensions]
```

（`:834` 是 `kbase_wait_continue:` 标签后的 `int64_t now = os_time_get_nano();`，v56 在 `:825`，v58 因新增 9 行而上移。）

| 产物 | 路径 | size | sha256 | md5 |
|---|---|---|---|---|
| v56 基线 `.so` | `/root/v56-libvulkan_panfrost.so.bak-1791176223` | 20 010 648 | `d0476a0c155af5debbd9cf3a7eee4b8aaab1b6a70bb1947901c2a959c6a9dd1a` | `99b8f9872ff6bafeb411fbe1b9dd2052` |
| v57 `.so`（已证伪） | `/root/v57-libvulkan_panfrost.so` | 20 009 968 | `3a76cce8c1c4abf27b49d57582ca3096501d0e46c73a0e19f0748777b05e66d2` | `62258baa…` |
| **v58 新 `.so`** | `/root/v58-libvulkan_panfrost.so`（= 构建目录产物） | **20 011 416** | **`ac198f581a76669eca2688f6c1a723fc8505a1c1af66bf506d9c5f4ae5013c2d`** | **`2f77a52b3a76d67e3e624880be913c90`** |

---

## 7. v58 APK（打包配方 `/root/pack_v58.sh`，由 `pack_v57.sh` 逐字派生）

* **载荷**：新 `libvulkan_panfrost.so` → 改名 `lib/arm64-v8a/libvulkan_freedreno.so`；
  **v54 原物** `libMobileGL.so`（`72919c73…`）与 `classes.dex`（`6bd3abde…`）。
* **manifest**：v54 结构**逐字符一致**，只改 `versionCode 54→58`、`versionName → "5.8-fix1-csinvalidate"`。
  `pojavEnv` / `boatEnv` 调试串保持原样；**`MESA_VK_WSI_HEADLESS_SWAPCHAIN` 命中数 = 0**（未添加）。

### 7.1 校验（必查项，全部通过）

```
=== payload entries ===
 16956584  lib/arm64-v8a/libMobileGL.so
 20011416  lib/arm64-v8a/libvulkan_freedreno.so      ← 非空、尺寸正确
     1328  classes.dex
=== 内嵌驱动 sha256（权威校验）===
unzip -p /root/final/mgl-panvk-v58.apk lib/arm64-v8a/libvulkan_freedreno.so | sha256sum
  ac198f581a76669eca2688f6c1a723fc8505a1c1af66bf506d9c5f4ae5013c2d   == 新 .so ✔
  bytes = 20011416  （≠ 0 ✔）
=== 另两项载荷与 v54 原物逐字节相同 ✔（72919c73… / 6bd3abde…）
=== zip 完整性：ZIP_OK（unzip -t）
=== badging：versionCode='58'  versionName='5.8-fix1-csinvalidate'  minSdk 26 / targetSdk 34 ✔
=== manifest 与 v54 的 diff：只差 versionCode / versionName 两行 ✔
=== manifest 与 v57 的 diff：只差 versionCode / versionName 两行 ✔
=== apksigner verify：Signer #1 CN=DSH Mali Driver，SHA-256 eba50950… ✔
=== APK：10 191 407 B，sha256 9df71f79a4bb21111d903a6f03534d1b318af86e238a31182c119a7a976abe05
```

---

## 8. 备份与回滚命令

| 备份 | 内容 |
|---|---|
| `…/csf/panvk_vX_gpu_queue.c.bak-1791176223` | **v56 源码**（快路径 + 10 s，无失效），122 818 B，`f3bf4ea5…` |
| `…/csf/panvk_vX_gpu_queue.c.bak-v57-1791177010` | **v57 源码**（删快路径 + 120 s），`647e26c9…` |
| `…/csf/panvk_vX_gpu_queue.c.v58-csinv` | **v58 源码副本**（本次产物源码），`abe2b41a…` |
| `/root/v58-libvulkan_panfrost.so` | **v58 二进制**（20 011 416 B，`ac198f58…`） |
| `/root/v56-libvulkan_panfrost.so.bak-1791176223` | v56 二进制（20 010 648 B，`d0476a0c…`） |

```sh
# 1) 把 v58 回滚成 v56（快路径 + 10 s，无失效）并重编：
cp -f /root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c.bak-1791176223 \
      /root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c
cd /root/zenithblue/build/android-v4 && export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH && \
  ninja src/panfrost/vulkan/libvulkan_panfrost.so     # 产物 sha256 应为 d0476a0c…

# 2) 回滚成 v57（删快路径 + 120 s）：
cp -f /root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c.bak-v57-1791177010 \
      /root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c   # 再 ninja

# 3) 不做源码改动、直接拿回 v58 二进制：
cp -f /root/v58-libvulkan_panfrost.so \
      /root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so

# 4) 重新打包 v58：
bash /root/pack_v58.sh

# 5) 反向核对（现状 == v58 源码）：
sha256sum /root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c
#   = abe2b41a705a3e966d37b8d6c6fac0c31ffb2230fd7496ab1674c6205e34e70a
```

---

## 9. 上机判据（不在本报告执行范围内）

只上 v58，单独存 logcat。**这一次 10 s 是有效判据**（v57 的 120 s 已按要求退回）：

1. **修好**（报告 23 §3.1 假说成立）：**10 s 内不再出现**
   `kbase: timeout on subqueue N` / `kbase: timeout snapshot subqueue N` / `vk_queue_set_lost`，
   运行**越过 10 s**，且**不出现** `CSF group … fatal error … exception 0xc3`。
2. **未修好**（假说被否证）：仍在约 10 s 处打印 `timeout snapshot`（`extract` 停住、`insert > extract`）
   ⇒ "陈旧 `active`"不是本病的充分解释，**下一轮换 fix2**（`kbase_kmod.c:582` 的 `csi_handlers` → 0，
   报告 23 §5.2 A1 / §6 Fix 2）——**不要在同一棵树上叠加**。
3. 任何情况下**不应**再看到 `exception 0xc3`：v58 保留了快路径，不重演 v57 的强 KICK 路径。

---

## 10. 明确"没做的事"（复核命令）

| 未做 | 复核 | 结果 |
|---|---|---|
| 未删快路径 | `grep -n 'if (\*active)' …gpu_queue.c` | `:734` / `:738` 均在（v57 曾为 0 处） |
| fix2：`csi_handlers` 未动 | `grep -n csi_handlers src/panfrost/lib/kmod/kbase_kmod.c` | `:552` / `:582` 仍为 `BASE_CSF_TILER_OOM_EXCEPTION_FLAG` |
| fix3：未加诊断日志 | `grep -n 'goto kbase_wait_continue' …gpu_queue.c` | 仍在，`cell->error` 吞掉逻辑原样 |
| `tiler_work_estimate` | `grep -c tiler_work_estimate …gpu_queue.c` | 与 v56 相同（未增删） |
| 未用 git 破坏未提交改动 | `git status --porcelain \| wc -l` | `62`（v56 时 60；增量 = 本轮 2 个未跟踪备份文件，无 tracked 文件被 checkout/stash/reset） |
| 未动其它树 | — | 未写 `/root/mesa`、`/root/MobileGL`；`/root/panvk-mtk` 仅 `grep` 只读 |
| 未操作手机 | — | 本轮无任何 adb / 安装 / 三件套操作 |

---

## 11. 证据文件清单（均在 `/root/research/`）

`25-cacheinvalidate-kick-fix.md`（本文）、`25-fix.diff`、`25-build-v56-recheck.log`、`25-build-v58.log`、
`25-build-v58-recheck.log`、`25-sec-v56.txt`、`25-sec-v58.txt`、`25-v58-manifest.txt`；
打包脚本 `/root/pack_v58.sh`；产物 `/root/v58-libvulkan_panfrost.so`、`/root/final/mgl-panvk-v58.apk`。
