# 15 号报告 — 与「已跑通先例 `/root/panvk-mtk`」的彻底 diff

面向：OPPO PHZ110（MT6989 / Immortalis-G720 MC12 / Android 16，无 root）上自编 Mesa panvk
（kbase/CSF 后端）**真实绘制必崩**（CSF MMU `0x7dc002c3` / TRANSLATION_FAULT_3 + 先例报告 13 的
tiler heap 线）这一唯一拦路虎。

- **本次全部为只读操作**：`ssh` + `cat/grep/sed/diff/git`。未改 `/root/mesa`、`/root/zenithblue`、
  `/root/MobileGL`；未编译；未碰手机（无 adb、无安装、未触碰红线三件套）。本报告是唯一新增文件。
- 标注约定：**【已定论】**=有源码/行号直接支撑；**【推断】**=由已定论事实演绎，需一步实验确认；
  **【未验证】**=只提出假设。

---

## 0. 一页结论（TL;DR）

1. **先例的真实身份被澄清（本次最大收获）**：`/root/panvk-mtk` 只是一个**补丁仓库**
   （单 commit `594efbf`，内容 = `patches/panvk_mtk.patch` 903 行 + 构建脚本）。
   它的源码真身是 **`/root/mesa`**，即 `funnymdzz/mesa` @ `6598829`（KRAID fork，**kbase 后端已在树内**，
   且 `git status` 干净 = **未打补丁的原始态**）。所以「先例与我们」的正确对比是
   **`/root/mesa`（+ `panvk_mtk.patch`）** vs **`/root/zenithblue/work/mesa`**。
2. **两者同源同作者血脉**：我们的 `kbase_kmod.c`（2033 行 vs 先例 1953 行）、`panvk_vX_gpu_queue.c`
   （3243 vs 3120）、`panvk_physical_device.c`（2681 vs 2396）差异极小（unified diff 分别只有
   342 / 392 / 653 行），**注释措辞逐字相同**（"matching panfork's working CSF path"、
   "Pixel kbase off-slot heap-reclaim shrinker"、"ponytail:"）。⇒ 这是同一套 kbase 后端代码的
   **两代**，不是两份独立实现。**【已定论】**
3. **tiler heap 相关代码在两树之间几乎逐字相同**（见 §2）：`kbase_kmod_csf_tiler_heap_create/term`
   （`kbase_kmod.c` 约 1000-1046）、`TILER_HEAP` 描述符写入（`gpu_queue.c:1993-2001`）、
   `HEAP_SET`/`tiler_oom_ctx`/`renew`/`retire`（`gpu_queue.c:1561-1680, 2089-2230`）**全部一致**。
   ⇒ **先例的成功不能归因于任何一处「tiler 参数/workaround」的不同**；13 号报告 §0.5 与 §5#1 猜测的
   「本 fork 把 chunk_size 从 2 MiB 改成 1 MiB 是偏离」**不成立**（见 §4.1）。
4. **真正找出的可移植差异有 6 条**（§2/§5），其中**最可能解释「fatal tiler heap OOM」的是 H1**：
   我们的队列组创建**从来没有把 `csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG` 传给内核**
   —— 在 uAPI < 1.25 的设备上走的是**古老的 `_1_6` 布局（ioctl 42，结构体里根本没有 `csi_handlers`
   字段）**，而本机 uAPI 有 1.18 布局（ioctl 58，**有**该字段，`include/drm-uapi/mali_kbase_ioctl.h:433-454`）。
   后果（公开 kbase 源码直接支撑）：内核 `handle_oom_event()` 只在
   `csi_handlers & TILER_OOM_EXCEPTION_FLAG` 时才走**可恢复的 incremental render**；
   否则任何一次 tiler OOM 都会 `term_queue_group()` + `report_tiler_oom_error()` ——
   **正好打印我们抓到的那一行** "kbase: CSF group N tiler heap OOM notification"，随后 group 被杀 ⇒
   AS 上出现 CSF 侧的 MMU 故障。而 panvk 的 **app 侧 handler 早就写好了**
   （`gpu_queue.c:1619-1625` 写 `tiler_oom_ctx.ir_scratch_fbd_ptr`，注释自称
   "The tiler OOM exception handler is registered to the fragment queue"）——**host 侧准备好了，却没告诉内核**。
   ⚠️ 该结论需一个实验确认（先例代码注释自称该 flag "was tried and confirmed ineffective"）。
   **【推断，最高】**
5. 第二可移植差异（H2）：**我们的 `tiler_work_estimate` 生产者被整段删掉了** —— 字段在
   `panvk_cmd_buffer.h:600` 声明、在 `gpu_queue.c:2782-2801` 被消费，但
   `panvk_vX_cmd_draw.c`/`panvk_vX_cmd_buffer.c` 里**零次赋值**（先例有：`panvk_vX_cmd_draw.c:2935-2942,
   3583-3586` + `panvk_vX_cmd_buffer.c:1029-1034`）。后果：`submit->tiler_work_estimate` 恒为 0 ⇒
   `kbase_renew_tiler_heap()` **永远不执行** ⇒ 每个 queue 的 tiler heap 只会单向增长到
   `max_chunks=400`，**永不换新**。先例会每 128 次提交（或 65536 work）换一个全新的 10-chunk heap，
   所以先例在线性堆耗尽前就重置了，我们不会。**【已定论（代码差集）+ 推断（因果）】**
6. 其余 4 条差异（kick 竞态修复缺失 / AFBC body 4096 对齐缺失 / INVALID-modifier 回退策略相反 /
   `allowed_group_priorities_mask` 与 `MEM_ALLOC_EX ENOTTY` 回退缺失）逐条列在 §2，并给出可移植补丁草稿 §5。
7. **证据可用性提醒**：`/root/research/probe10/out-device/` 里**存档的 tri 日志并不含**
   "tiler heap OOM notification" 这一行（`l_tri.txt` 只有 CPU queue dump → 三次 0xc3 fatal → DEVICE_LOST；
   全服务器 grep 只有公开 kbase 源码 `pub-kbase/csf.c` 命中该字符串）。本报告把该行当作**父级现场实测**采信，
   但**任何后续复现都应把这一行一起存档**（S1 命令会顺带解决）。

---

## 1. 方法与先例身份（**已定论**）

| 项 | 值 | 出处 |
|---|---|---|
| 先例补丁仓库 | `/root/panvk-mtk`（git 1 个 commit `594efbf`，remote `BkaNeko/panvk-mtk-driver`） | `git -C /root/panvk-mtk log --oneline`；`README.md:1-20` |
| 先例源码真身 | `/root/mesa` = `funnymdzz/mesa` @ `6598829`，**未打补丁**（`git status` 干净；`KBASE_WAIT_TIMEOUT_NS` 仍是 10 s） | `git -C /root/mesa log/remote`；`/root/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c:61` |
| 补丁规模 | 903 行，18 个文件（`panvk_mtk.patch`） | `grep -n '^diff --git' panvk_mtk.patch` |
| 先例设备 | Redmi Note 11T Pro / 天玑8100 / **Mali-G610 MC6** / kernel 5.10.237 / **kbase r32p1, CSF uAPI 1.18** | `panvk-mtk/README.md:11-14` |
| 我们的设备 | MT6989 / **Immortalis-G720 MC12** / Android 16 / uAPI 1.21（代码注释），`pan_kmod_driver_version_at_least(...,1,25)` 才走新 ABI | `zb kbase_kmod.c:523`；13 号报告 §0 |
| **关键限制** | 先例的 kbase 分支**拒绝 arch 14**：`/root/mesa` 的 kbase `panvk_physical_device_init` 把 `case 14` 放进 "panvk is not well-tested on v14" 分支；我们的树把它移进放行组 | `/root/mesa/src/panfrost/vulkan/panvk_physical_device.c`（`case 6/7` 列表含 `14`）vs `zb` 同函数 `case 10/11/12/13/14: break;` |

⇒ **先例从未在 G720 / arch 14 上跑过**。它证明的是「kbase 管道（uAPI 1.18、队列组、tiler heap ioctl、
kick/present 时序）在 MTK 上能工作」，**不构成对 arch-14 描述符/CS 发射代码的验证**。这条是解读后面所有
差异的前提。

---

## 2. 差异清单（按对 OOM 的解释力排序）

> 「先例」列 = `/root/mesa` **原始态**；若该处修复来自 `panvk_mtk.patch`，标注 `[patch]`。

| # | 位置（我们树） | 先例 | 我们 | 解释 OOM/故障的可能性 | 定论度 |
|---|---|---|---|---|---|
| **H1** | `src/panfrost/lib/kmod/kbase_kmod.c:510-573`（`kbase_kmod_csf_group_create`） | 同（也没有设 `csi_handlers`） | **同**：1.25+ 用 112 B 新布局（只设 `cs_fault_report_enable=1`，**`csi_handlers` 留 0**）；否则回退 **`_1_6`（ioctl 42，结构体无 `csi_handlers` 字段）** | **最高**：tiler OOM 永远走内核的 fatal 分支 → 打印我们那行日志 + 杀 group | 代码【已定论】/ 因果【推断】 |
| **H2** | `src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c:3457`（`panvk_cmd_draw`）与 `panvk_vX_cmd_buffer.c` | 有 `account_tiler_work()`（`cmd_draw.c:2935-2942`）并在 `:3583-3586` 调用；二级→一级累加在 `cmd_buffer.c:1029-1034` | **零次赋值**（全树 grep 只有声明 `cmd_buffer.h:600` + 消费 `gpu_queue.c:2782-2801`）⇒ 恒 0 | **高**：`kbase_renew_tiler_heap()` 永不触发 ⇒ heap 单向涨到 `max_chunks` 后每次 OOM 都 fatal；先例会周期性换新 heap | 【已定论】 |
| **H3** | `src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c:697-726`（`kbase_subqueue_publish`）+ `:63` | `[patch]` 删掉 user doorbell 快速路径，**总是 kick 调度器**；`KBASE_WAIT_TIMEOUT_NS 10 s→120 s` | **仍有 doorbell 快速路径**（`:719-724` 若 `*active` 就直接 return）；timeout 仍 **10 s** | 中（先例 README#3 点名的已知坑）：active→idle 竞态丢 kick ⇒ GPU 空转、队列有活 ⇒ 超时 DEVICE_LOST。**不直接产生 OOM**，但会污染故障现场、并把「v14 掉 GPU」和「kick 竞态」混在一起 | 【已定论】 |
| **H4** | `src/panfrost/lib/pan_mod.c:165-166` + `src/panfrost/lib/pan_desc.c`(`get_afbc_att_mem_props`) | `[patch]` 对 `AFBC_FORMAT_MOD_SPLIT` 强制 **body 4096 对齐**（patches `pan_mod.c:164-168`、`pan_desc.c:278-283`） | **无该对齐**（grep `ALIGN_POT(body_offset` 无命中） | 中低：MTK gralloc 的 AFBC 布局错位（先例的「白屏根因」）。影响的是一致性/花屏，**不产生 CSF READ fault**；但若 tri 的 RT 是内部 AFBC 图，会污染 tile 写入位置 | 【已定论】 |
| **H5** | `src/vulkan/runtime/vk_android.c`（zb 的「线性自描述回退」，中文注释块 `:670-700`；`panvk_image.c`） | `[patch]` `DRM_FORMAT_MOD_INVALID` → **回退到 MTK 实际 AFBC modifier `0x0800000000000072`**（patch `panvk_image.c` hunk，patch 行 321-408） | zb 走**另一条相反策略**：只在 AHB 声明 CPU 可读写时**回退成 `DRM_FORMAT_MOD_LINEAR`** | 低（tri 用的是内部 `vkCreateImage`，非 AHB）/ **高（MGL / 窗口化 / 合成路径）** | 【未验证】 |
| **H6** | `src/panfrost/lib/kmod/kbase_kmod.c:376-378` | `[patch]` `allowed_group_priorities_mask` 加 **HIGH \| REALTIME** | 只有 `MEDIUM \| LOW` | 低（影响 HWUI 队列优先级协商；`priority=0` 本身已 = `BASE_QUEUE_GROUP_PRIORITY_HIGH`，见 `mali_base_csf_kernel.h:191-196`） | 【已定论】 |
| **H7** | `src/panfrost/lib/kmod/kbase_kmod.c:1632`（`MEM_ALLOC_EX`） | `[patch]` **`ENOTTY` → 回退经典 `MEM_ALLOC`**（MTK 特有：uAPI≥1.9 却不实现 `MEM_ALLOC_EX`） | **无回退** | 低（clear/copy 全通 ⇒ 分配本身没失败）；**但一旦 v14 上某条路径触发它就是硬失败**，值得白捡 | 【已定论】 |
| **H8** | `kbase_kmod.c:445-472`、`gpu_queue.c:494-499` | `cs_reg_count` **硬编码 96**，不设 `nr_kernel_registers` | 从 GLB iface `WORK_REGS` 推导（`:447 cs_reg_count=(stream_features&0xff)+1`），arch≥12 兜底 128；并设 `.nr_kernel_registers=MAX2(unpreserved_cs_reg_count,4)` | 中（**可能同时是 ±**）：影响 CS 寄存器文件的划分；若与 v14 固件不符，VT 作业的 `HEAP_SET` 等寄存器可能在 pre/post-call 间被破坏 → 固件拿着坏 heap 指针 ⇒ OOM + 高位 VA 故障。**但 clear/copy 也走同一 builder，故不能单独解释「只有 tri 崩」** | 【未验证】 |
| H9 | `gpu_queue.c:2439-2460, 2568-2612` 等 | 先例保留 `kbase_log_stream_prefix`；utrace clone 用单 root buffer | 注释掉 stream 解码；改 `panvk_utrace_clone_cs_ctx` 多 buffer | 无（诊断/utrace 基建） | 【已定论】 |
| H10 | 大量 `mesa_logi/mesa_loge/mesa_logw` 新增、`pandecode` 注入、`PANVK_KBASE_IMPORT_CLEAR/SET`、placed-map、coherency | — | 我们更多 | 无（诊断/新特性） | 【已定论】 |

**tiler 相关代码逐字一致的部分（明确排除嫌疑）【已定论】**：

- `kbase_kmod_csf_tiler_heap_create()` / `_destroy()`：两树**逐字节相同**（含 `MIN2(target_in_flight, UINT16_MAX)`、
  `group_id`、`KBASE_IOCTL_CS_TILER_HEAP_INIT/TERM`）。
- tiler 参数：`panvk_physical_device.c` 的 **kbase 分支 `chunk_size=1 MiB / initial=10 / max=400` 两树相同**；
  2 MiB/5/64 是 **panthor(DRM) 分支**，不是先例的 kbase 值。
- `TILER_HEAP` 描述符写入 `cfg.size/base/bottom(+64)/top(+chunk_size)`、`tiler_oom_ctx.ir_scratch_fbd_ptr`、
  `HEAP_SET`、`kbase_renew_tiler_heap()`、`kbase_try_destroy_retired_heap()`、`kbase_subqueue_wait_seqno` 的
  主体逻辑：**全部一致**。
- tiler heap 参数与 uAPI 结构体：`union kbase_ioctl_cs_tiler_heap_init` 的**字段布局两树完全相同**
  （`chunk_size/initial_chunks/max_chunks/__u16 target_in_flight/__u8 group_id/__u8 padding` vs
  `/root/mesa/include/drm-uapi/mali_kbase_ioctl.h:580-593`）。

---

## 3. 内核侧机制（用来判定「哪条差异能产生我们看到的日志」）**【已定论，公开 kbase】**

公开参考源码在 `/root/research/csf-work/pub-kbase/`（13 号报告已抓取）。

1. 固件要新 chunk ⇒ 置 `CS_REQ_TILER_OOM`。
   内核 `oom_event_worker()`/`process_cs_oom_event()` 处理：`csf.c:2040-2110`。
2. 它读固件 heap 统计（`CS_HEAP_VT_START/VT_END/FRAG_END`，走 **fw_io** 接口）：
   `csf.c:1920-1939` —— **`vt_end >= vt_start` 或 `frag_end > vt_end` 直接 `return -EINVAL`**
   （dev_warn "Invalid Heap statistics provided by firmware"）。
3. `th.c:895-925 validate_allocation_request()`：
   - `WARN_ON(!nr_in_flight)` ⇒ **-EINVAL**；
   - `nr_in_flight <= target_in_flight` 且 `chunk_count < max_chunks` ⇒ **0（允许分配）**；
   - 否则 `pending_frag_count>0` ⇒ -EBUSY；`pending_frag_count==0` ⇒ **-ENOMEM**。
4. `csf.c:1940-1956` —— **只有** `group->csi_handlers & BASE_CSF_TILER_OOM_EXCEPTION_FLAG`
   **且** `pending_frag_count==0` **且** `err∈{-ENOMEM,-EBUSY}` 才把它变成
   **incremental render（`new_chunk_ptr=0`，可恢复）**。
5. 否则 `err` 非零 ⇒ `csf.c:2100-2107`：`term_queue_group()` + `report_tiler_oom_error()`
   ⇒ `csf.c:1978-1997` 生成 `BASE_GPU_QUEUE_GROUP_ERROR_TILER_HEAP_OOM` 通知
   ⇒ **我们的 `kbase_kmod.c:655-657` 打印 "CSF group %u tiler heap OOM notification"**，group 被杀。

**推论【推断】**：
- H1 直接命中第 4 步的**前置条件**：我们从未注册该 flag ⇒ 第 4 步恒为假 ⇒ **任何** tiler OOM 都 fatal。
  （先例同样没注册，但先例的 uAPI 1.18 内核路径 + 有 heap 续期，使它在其测试负载内不触发 OOM。）
- 若 OOM 在**第一次 draw**就出现（父级现场），则更可能是第 2 步的 **-EINVAL**（fw 统计无效/为 0）
  或 `alloc_new_chunk()` 失败 ⇒ 这时 `csi_handlers` **也救不了**；此时唯一出路是**让 OOM 事件根本不发生**
  （更大 `initial_chunks`、或补 H2 的续期、或换 chunk_size）。**S1/S3 正是用来二分这两种情况的。**

---

## 4. 对 13 号报告的两处修正（重要）

### 4.1 「本 fork 把 chunk_size 改成 1 MiB 是偏离」——**不成立**【已定论】

13 号报告 §0.5/§5#1 把 `panvk_physical_device.c` 的
`1024*1024 / 10 / 400`（kbase 分支）与 `2*1024*1024 / 5 / 64`（panthor 分支）对比，推断这是
「本 fork 的偏离、没和内核编码约束对账」。**但先例（G610 上已跑通的那一份）在 kbase 分支用的是
完全相同的 1 MiB/10/400**：

- 先例 `/root/mesa/src/panfrost/vulkan/panvk_physical_device.c:1187-1195`（kbase 分支，注释同样是
  "kbase prefers order-9 huge pages …"）= `1 MiB / 10 / 400`。
- 我们 `zb/.../panvk_physical_device.c:1365-1373` = `1 MiB / 10 / 400`。
- `panvk_mtk.patch` **完全没有碰** tiler 参数（`grep chunk_size panvk_mtk.patch` 无命中）。

⇒ S3（改回 2 MiB 看故障 VA 是否移动）**仍是有效判据实验**，但它**不是「与先例的差异」**，
不能作为「先例做对了而我们做错了」的证据。

### 4.2 存档日志里没有 tiler heap OOM 行

`/root/research/probe10/out-device/l_tri.txt` 的实际时间线是：`vkQueueSubmit(draw) -> 0`（11:08:11.915）
→ `received CSF CPU queue dump notification`（12.575）→ 三次 `0xc3` fatal（13.106）→
`vkWaitForFences -> -4 (VK_ERROR_DEVICE_LOST)`（16.918）。**没有** OOM 行，
`o_tri.txt` 也没有。全服务器检索 "tiler heap OOM" 只命中公开源码 `pub-kbase/csf.c`。
⇒ 本报告按父级现场实测采信该行；**建议把它并入 S1 的存档**（否则后续无法回溯「OOM → 故障」的先后）。

---

## 5. 补丁草稿（**只写在报告里，未改任何构建树**）

> 目标树：`/root/zenithblue/work/mesa`。建议顺序 **P1 → P2 → P6 → P3 → P4 → P5**；
> 每步只改一处，单独上机，记 `o_*/l_*` 存档。
> **回滚方式统一为**：`git -C /root/zenithblue/work/mesa diff > /root/research/15-p<N>.rollback.patch`
> 先存证，再 `git checkout -- <file>`；`kbase_csf_uapi.h` 是 untracked 新文件，回滚用它自己的备份副本。

### P1（最高优先，H1）让内核知道我们要自己处理 tiler OOM

**文件**：`src/panfrost/lib/kmod/kbase_kmod.c`（`kbase_kmod_csf_group_create()`，`:510-573`）
**文件**：`include/drm-uapi/mali_kbase_ioctl.h:429`（flag 已有：`#define BASE_CSF_TILER_OOM_EXCEPTION_FLAG (1u << 0)`）

改法：**在 uAPI ∈ [1.18, 1.25) 时改用 1.18 布局（ioctl 58、结构体 40 B），并把 `csi_handlers` 置位**，
而不是掉到 `_1_6`（ioctl 42，**没有该字段**）。

```diff
--- a/src/panfrost/lib/kmod/kbase_kmod.c
+++ b/src/panfrost/lib/kmod/kbase_kmod.c
@@ -521,10 +521,32 @@ kbase_kmod_csf_group_create(struct pan_kmod_dev *dev, uint32_t cs_queue_count,
     * unhandled OOM regardless).  On uAPI 1.25+ use the current ioctl layout
     * to ask the kernel to report recoverable CS faults through read(). */
+   /* Experiment P1: ask the kernel for the recoverable tiler-OOM path
+    * (incremental render) by registering the CSI handler flag.  The flag can
+    * only be passed through the 1.18 layout (ioctl 58, 40-byte struct);
+    * the 1.6 fallback struct has no csi_handlers field at all. */
    if (pan_kmod_driver_version_at_least(&dev->driver, 1, 25)) {
       union kbase_ioctl_cs_queue_group_create req = {
          .in = {
@@ -536,6 +558,7 @@
             .cs_fault_report_enable = 1,
+            .csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG,
          },
       };
@@ -546,9 +569,31 @@
       mesa_logw("kbase: current CS_QUEUE_GROUP_CREATE failed: %s; "
                 "falling back to the 1.6 ABI",
                 strerror(errno));
    }
+
+   if (pan_kmod_driver_version_at_least(&dev->driver, 1, 18)) {
+      union kbase_ioctl_cs_queue_group_create_1_18 req18 = {
+         .in = {
+            .tiler_mask = 1,
+            .fragment_mask = ~0ull,
+            .compute_mask = ~0ull,
+            .cs_min = cs_queue_count,
+            .priority = 0,   /* BASE_QUEUE_GROUP_PRIORITY_HIGH */
+            .tiler_max = 1,
+            .fragment_max = 64,
+            .compute_max = 64,
+            .csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG,
+         },
+      };
+
+      if (ioctl(dev->fd, KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_18, &req18) == 0) {
+         *group_handle = req18.out.group_handle;
+         mesa_logi("kbase: created CSF group %u with TILER_OOM CSI handler",
+                   *group_handle);
+         return 0;
+      }
+
+      mesa_logw("kbase: 1.18 CS_QUEUE_GROUP_CREATE failed: %s; "
+                "falling back to the 1.6 ABI", strerror(errno));
+   }
 
    /* The legacy request remains the compatibility fallback ... */
```

**判据**：`l_tri.txt` 里 OOM 行是否变成
`kbase: created CSF group N with TILER_OOM CSI handler` + **不再** fatal；若仍 fatal 但**不再**打 OOM 行 ⇒
内核接受了 flag 并转入 incremental render，故障点后移（那就是 §3 第 2 步的 -EINVAL 路线，转做 P3）。
**风险**：`_1_18` 在本机 MTK kbase 上是否真的实现（ioctl 58 只认 112 B？）——【未验证】，
故保留 1.6 兜底；若无回退日志即代表成功。

### P2（H2）恢复 `tiler_work_estimate` 生产者，让 heap 能续期

**文件**：`src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c`（函数 `panvk_cmd_draw` 起始于 **:3457**）

```diff
+static void
+account_tiler_work(struct panvk_cmd_buffer *cmdbuf, uint64_t work)
+{
+   if (UINT64_MAX - cmdbuf->state.tiler_work_estimate < work)
+      cmdbuf->state.tiler_work_estimate = UINT64_MAX;
+   else
+      cmdbuf->state.tiler_work_estimate += work;
+}
+
 static void
 panvk_cmd_draw(struct panvk_cmd_buffer *cmdbuf, struct panvk_draw_info draw)
 {
    ...
    if (!vs || !panvk_priv_mem_check_alloc(vs->spd))
       return;
+
+   /* P2: restore the estimate that gates kbase tiler-heap renewal. */
+   if (draw.indirect.buffer_dev_addr)
+      account_tiler_work(cmdbuf, (uint64_t)draw.indirect.draw_count * 256);
+   else
+      account_tiler_work(cmdbuf,
+                         (uint64_t)draw.vertex.count * draw.instance.count);
}
```

（行号与内容照抄先例 `/root/mesa/src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c:2935-2942, 3583-3586`；
zb 里对应函数在 `:3457`，`draw` 是值参不是指针，其它同名。）

再加二级命令缓冲的累加（照抄先例 `panvk_vX_cmd_buffer.c:1029-1034`，插到「继承二级缓冲 state」那一处）。

**判据（零成本，先做这个）**：`PANVK_KBASE_HEAP_RENEW_INTERVAL=1`（`gpu_queue.c:84-97`，=0 是关闭、
>0 是阈值）⇒ 每次图形提交都续期。若 `tri` 因此通过 ⇒ **H2 就是 OOM 的成因**。
若 `tri` 仍第一次就崩 ⇒ OOM 发生在首个提交内，H2 只是长跑炸弹（转 P1/P3）。

### P3 把 heap create 的返回值打出来（S1/S3 的诊断底座）

**文件**：`src/panfrost/lib/kmod/kbase_kmod.c`（`kbase_kmod_csf_tiler_heap_create()` out 赋值处，约 **:1013**）

```c
   *heap_ctx_va = req.out.gpu_heap_va;
   *first_chunk_va = req.out.first_chunk_va;
+   mesa_logi("kbase: tiler heap create: chunk_size 0x%x initial %u max %u "
+             "-> ctx 0x%" PRIx64 ", first_chunk 0x%" PRIx64,
+             chunk_size, initial_chunks, max_chunks, *heap_ctx_va,
+             *first_chunk_va);
   return 0;
```

配 13 号报告 §4 S2 的 `gpu_queue.c:2106-2116` 改动（`mesa_logd` → 加 `ctx`/`chunk_size`）。

### P4（H3）kick 竞态 + 超时：**照搬 `panvk_mtk.patch` 第 172-249 行**

- `gpu_queue.c:711-724`：删掉 user doorbell 快速路径，**总是** `kbase_kmod_csf_queue_kick()`。
- `gpu_queue.c:63`：`KBASE_WAIT_TIMEOUT_NS (10ll*1e9)` → `(120ll*1e9)`。
- 判据：tri 的 `vkWaitForFences` 是否从 -4 变成 0（本项**不**直接修 OOM，但能把「kick 竞态」这条
  噪声从现场剔除，避免误判）。

### P5（H4）AFBC body 4096 对齐 + （可选）INVALID modifier 回退

- `src/panfrost/lib/pan_mod.c:165-166`：`body_offset_B = ALIGN_POT(body_offset_B, 4096)`
  （条件 `if (props->modifier & AFBC_FORMAT_MOD_SPLIT)`）。
- `src/panfrost/lib/pan_desc.c:get_afbc_att_mem_props()`：对 `*body_offset` 同样处理（patch 行 108-117）。
- 判据：`tri` 回读像素是否仍精确（`o_tri.txt` 里 `failures=`）；对 MGL/窗口化才是关键。
- ⚠️ H5（modifier 回退策略）与我们是**相反方向**（先例→AFBC，我们→LINEAR）。**先不要混改**，
  先用 `PANVK_GRALLOC_NO_FALLBACK=1`（zb 自己的开关，`vk_android.c:670-700`）做对照。

### P6（H6/H7）两条白捡移植

- `kbase_kmod.c:376-378`：`allowed_group_priorities_mask` 增加 `PAN_KMOD_GROUP_ALLOW_PRIORITY_HIGH | _REALTIME`
  （= `panvk_mtk.patch` 行 1-25 的改动，README 说是为了让 Android HWUI 的 HIGH 全局优先级能建队列）。
- `kbase_kmod.c:1632`：`MEM_ALLOC_EX` 失败且 `errno == ENOTTY` 时 `goto legacy_alloc;` 回退经典 `MEM_ALLOC`
  （= `panvk_mtk.patch` 行 57-104 附近；README 明确说这是 **MTK 特有**问题）。

---

## 6. S1 —— 零成本：把 tiler heap 的 VA 打出来（**确切命令**）

已在 fork 里、只是被 info 级过滤的日志：`zb/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c:2106-2116`
（`mesa_logd("kbase: tiler heap desc 0x%…: base 0x%…, bottom 0x%…, top 0x%…, geom 0x%…, oom_fbd 0x%…")`）
+ `:2104`（`kbase: GLB iface: version 0x%x, features 0x%x, %u groups, %u streams…`，`mesa_logd`）。

探针：`/root/research/probe10/panvk_wsi_probe` → 设备 `/data/local/tmp/panvk_wsi_probe`
（现场实测可从 **shell uid 2000** 在 `/data/local/tmp` 运行）；runner 模板见
`/root/research/probe10/runner.sh`（当前写死 `MESA_LOG_LEVEL=info`）。

```sh
# 在手机 shell（uid 2000）/ Shizuku 通道执行；无需 UI、无需安装任何 APK：
cd /data/local/tmp || exit 1
logcat -c 2>/dev/null
MESA_LOG_LEVEL=debug timeout 45 ./panvk_wsi_probe \
    --icd=/data/local/tmp/libvulkan_panfrost.so --mode=tri \
    > /data/local/tmp/o_tri_dbg.txt 2>&1
echo "PROBE_EXIT=$?"
logcat -d -s MESA:V P10PROBE:V '*:S' > /data/local/tmp/l_tri_dbg.txt 2>/dev/null
grep -E "tiler heap desc|tiler heap create|GLB iface|OOM|CS error|fatal error" \
     /data/local/tmp/l_tri_dbg.txt
```

- **不要用 `--mode=all`**（12 号报告已证会污染后续模式）；一次只跑 `tri`。
- 若 `MESA_LOG_LEVEL=debug` 对 `mesa_logd` 无效，等价开关是 Android 属性
  `debug.mesa.log.level=debug`（先例 `panvk-mtk/config/system.prop` 用的就是它）；探针从 shell 跑时
  环境变量优先。**【未验证】**两者在本机 `mesa_log` 实现下的优先级关系。

**判据**：
- `base`/`top` 区间**包含或紧邻** `0x5fffe1e000` ⇒ 故障地址在 **chunk 区**（13 号报告 H1）。
- `base` 明显更低、距 `0x5fffe1e000` 远超 `chunk_size` ⇒ 更像**固定结构**（heap context `gpu_heap_va`，需 S2 的 ctx 值）。
- 顺带确认 `GLB iface` 的 `version/features` → 反推本机 uAPI 版本（决定 P1 该走 1.18 还是 1.25 分支）。

---

## 7. S3 —— 判据性实验：扰动 heap 布局，看故障 VA 是否跟着动

**位置（1 行）**：`src/panfrost/vulkan/panvk_physical_device.c:1371`（kbase 分支内，`:1365-1373`）

```diff
-      device->csf.tiler.chunk_size = 1024 * 1024;
+      device->csf.tiler.chunk_size = 2 * 1024 * 1024;
```

（等价变体：`initial_chunks 10 → 40`，改变堆规模而不动 chunk 大小。）
内核侧约束（公开 kbase `th.c:666-694`）：`chunk_size != 0`、`chunk_size & ~CHUNK_SIZE_MASK == 0`、
`initial_chunks != 0`、`initial_chunks <= max_chunks`、`target_in_flight != 0`。
**`CHUNK_SIZE_MASK` 的确切位宽本次仍未取到**【未验证】；2 MiB 是上游 panthor 默认值，理应通过。

**判据**（重跑 `tri`，对比 sideband）：
- **地址变了** ⇒ 故障地址**由 heap 布局推导**（固件确在走 chunk 链）⇒ 重点查 chunk 链语义
  （`cfg.size/top` 与内核 `encode_chunk_ptr`/`link_chunk` 是否一致：`pub-kbase/th.c:62-75,140-150,1031`；
  以及「kbase 上没有 FRAGMENT_COMPLETED heap op ⇒ chunk 不归还」，`gpu_queue.c:62-68`）。
- **地址不变（仍 `0x5fffe1e000`）** ⇒ 是**固定结构地址** ⇒ 重点查 heap context 的映射/传递
  （`gpu_heap_va` 是否被 map 进该 kctx 页表；S2/P3 的 ctx 打印）。

**S3b（更便宜，且更贴合 H2）**：先做 P2，然后
```sh
PANVK_KBASE_HEAP_RENEW_INTERVAL=1 MESA_LOG_LEVEL=debug timeout 45 ./panvk_wsi_probe \
    --icd=/data/local/tmp/libvulkan_panfrost.so --mode=tri
```
判据：每次提交都换新 heap 后 tri 是否通过 ⇒ 直接判定「heap 耗尽」是真凶还是只是长跑炸弹。

---

## 8. 未验证清单（明确不装懂）

1. `BASE_CSF_TILER_OOM_EXCEPTION_FLAG` 在本机 MTK kbase 上**是否真的被接受**：先例代码注释自称
   "was tried and confirmed ineffective on this kernel"（`zb kbase_kmod.c:517-522`）——**这句注释在我们的树和
   先例里逐字相同**，无法判定它指的是哪台设备/哪个 uAPI。P1 就是为它设计的实验。
2. 本机 kbase 是否实现 `KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_18`（ioctl 58 / 40 B）还是只认 112 B 的
   1.25 布局；以及本机实际协商到的 `driver.version`（`kbase_kmod.c:1141` 的 `VERSION_CHECK_CSF` 结果未打印）。
3. 父级现场那条 "tiler heap OOM notification" 的**先后关系**（OOM 是否在第一次 draw 内、与 CPU queue dump
   的时序）：存档日志里没有该行（§4.2）。
4. `0x5fffe1e000` 属于 kbase 的 `CUSTOM_VA/JIT` 区：沿用 13 号报告的推断（未取到 zone 边界源码）。
5. H5 的两种相反策略（先例 AFBC vs 我们 LINEAR）哪个对 MT6989 的 gralloc 正确：需 AHB
   `l_ahb*.txt` 一路的 modifier 实测；**未验证**。
6. H8（`cs_reg_count`/`nr_kernel_registers` 分歧）对 v14 是正是负：需要一次「只改这一处」的对照实验。
7. 本次未做手机操作、未编译、未改任何受保护目录；只新增本报告
   `/root/research/15-panvk-mtk-diff.md`。

---

## 9. 出处速查

- 先例身份：`/root/panvk-mtk/README.md:1-20,155`；`/root/panvk-mtk/patches/panvk_mtk.patch`（903 行；
  `kbase_kmod.c` hunk 行 1-104、`panvk_vX_gpu_queue.c` hunk 行 172-249、AFBC hunk 行 104-168）；
  `/root/mesa`（`git remote=funnymdzz/mesa`，`HEAD=6598829`，`git status` 干净）。
- 我们树：`/root/zenithblue/work/mesa`（`HEAD=5a07217f034`，`git diff --stat` 35 文件 / +4553-141）。
- tiler OOM 的内核语义：`/root/research/csf-work/pub-kbase/csf.c:1920-1939,1943-1956,1978-1997,2040-2110`；
  `.../th.c:666-694,895-925,1031,1288`；`.../cq.c:35-80`。
- 我们树的落点：`kbase_kmod.c:376-378, 510-573, 654-658, 1000-1046, 1632`（+ `include/drm-uapi/mali_kbase_ioctl.h:407-484`）
  、`panvk_vX_gpu_queue.c:63, 697-726, 1561-1680, 1993-2010, 2104-2116, 2782-2801`
  、`panvk_vX_cmd_draw.c:3457`、`panvk_physical_device.c:1365-1373`、`pan_mod.c:165-166`、
  `vk_android.c:670-700`、`mali_base_csf_kernel.h:191-196`。
- 先例对应落点：`/root/mesa/src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c:2935-2942,3583-3586`、
  `panvk_vX_cmd_buffer.c:1029-1034`、`panvk_physical_device.c:1187-1195`。
- 现场证据：`/root/research/probe10/out-device/l_tri.txt`、`o_tri.txt`、`runner.sh`；
  `/root/research/13-csf-exception-c3.md`（对比基准）。
