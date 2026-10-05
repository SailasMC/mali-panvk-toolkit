# 23 · 10 秒完成超时 → `VK_ERROR_DEVICE_LOST` 定案报告

> **对象**：真机 OPPO PHZ110 / MT6989 / Immortalis-G720 **MC12** / Android 16 / 无 root / kbase CSF uAPI **1.21**。
> 构建树 = `/root/zenithblue/work/mesa`（**只读**，本轮一行未改）。
> 现场 = 任务给出的 v54+/12:35:29 logcat（含周期性 `I/MESA: kbase: tiler heap renewal (uAPI 1.21, submits 128, renew interval 128)`）。
> **纪律**：未操作手机；未改 `/root/mesa`、`/root/zenithblue`、`/root/MobileGL`；未 `rm -rf`；
> 本轮新增写入仅 `/root/research/23-completion-timeout-crash.md`、`/root/research/23-work/**`、`/root/research/queue-work/**`。
> **标注**：【已定论】= 有 file:line 或命令输出支撑；【推断】= 由已定论事实演绎；【未验证】= 无直接证据。

---

## 0. 一页结论（六条，先读这段）

1. **这 10 秒不是"内核在等"，是 Mesa 用户态在等。** 打印超时的唯一代码是
   `panvk_vX_gpu_queue.c:840` 的 `mesa_loge("kbase: timeout on subqueue %u: …")`，它由
   `kbase_subqueue_wait_seqno()` 的看门狗 `watchdog = start + KBASE_WAIT_TIMEOUT_NS`（`:759`，
   常量在 **`:63` = 10 s**）触发。任务描述"内核等一个不会到来的完成信号"，
   在代码上应读作：**用户态等一个 GPU 写不进 seqno cell 的完成信号，而内核只是被轮询**。
   判据：现场那一族 `kbase: timeout snapshot subqueue N: …` 全部来自用户态
   `kbase_log_subqueue_state()`（`:321-377`），它打印的 `insert/extract/cell->seqno/last_job_offset`
   **只有用户态才知道**（`subq->kbase.*` 是 Mesa 的结构体成员）。【已定论】

2. **等的是哪个对象**：`queue->kbase_seqnos`（一个 4096 B 的 `PAN_KMOD_BO_FLAG_CSF_EVENT` BO，
   每个 subqueue 一个 `struct panvk_cs_sync64` cell）。唯一的成功条件是
   **`:789` `cell->seqno >= target_seqno`**。这个 cell 由发射进环里的 wrapper 末尾那条
   **`SYNC_ADD64`（`:629-631`）** +1。逐行路径见 §2。【已定论】

3. **为什么等不到**：把现场三条 dump 对齐后，**卡住的不是 subqueue 0**：

   | subqueue | cell->seqno | target(=emitted_jobs) | insert | extract | 余量 |
   |---|---|---|---|---|---|
   | 0 VERTEX_TILER | **433** | 433 | 83584 | 83584 | **0 —— 已消费完、已完成** |
   | 1 FRAGMENT | 432 | 433 | 83584 | 83504 | 80 B |
   | 2 COMPUTE | 432 | 433 | 83584 | 83488 | 96 B |

   `83584 − 192 = 83392` 是最后一条 wrapper 的起点；`112 = 14×8` 正好落在 **`CALL`**
   （发射序 `:511-631`：5×`SET_STATE` + `REQ_RES` + `MOVE64 ctx` + `MOVE64`/`HEAP_SET` +
   `MOVE32`/`FLUSH`/`WAIT` + `MOVE64 stream_addr` + `MOVE32 size` = idx0..13，**idx14 = `CALL`**）。
   COMPUTE 少 16 B（96 而不是 112）正好对应它**没有 `HEAP_SET`**（`:575-578` 的
   `if (subqueue != PANVK_SUBQUEUE_COMPUTE)`）。
   ⇒ **FRAGMENT / COMPUTE 的 CS 把最后一条环项取到 `CALL` 之前就停住，10 秒里一步没再前进。**
   归类：**(a) 驱动没 kick / 没把队列重新跑起来**。
   （`active 0` **不是**判据——已完成的 sq0 也打印 `active 0`；判据是
   `extract < insert` **且** 持续 10 s **且** `seqno` 没到 target。）【已定论】

4. **最可能的那一处**：`kbase_subqueue_publish()` 的"用户门铃快速路径"——
   **`src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c:716-725`**。
   已跑通的 panvk-mtk 先例**逐字删掉了这段**，并在补丁注释里点名了与我们现场**一字不差**的症状
   （原文见 §5.1）。`active` 的两次读取**都没有缓存失效**（`:716-725` 前后没有
   `kbase_cache_invalidate_range`，而等待循环在 `:788` 是会失效的；`kbase_gpu_wmb()` 只是
   `dsb sy`，见 `:148-157`），所以快速路径可能读到**陈旧的非 0 `active`** 而直接 `return`，
   把这次提交的唤醒丢掉。【推断，最高】

5. **`error_type == 0` 的正解：本机在协议上就拿不到完成/异常通知，而且驱动一行都没注册。**
   - 通知开关 `cs_fault_report_enable` **只存在于 1.25 布局**（`kbase_kmod.c:551`）；
     本机 uAPI 1.21 走的是 **1.18 rung**（`:570-594`），那个 40 B 结构里只有 `csi_handlers`
     （`include/drm-uapi/mali_kbase_ioctl.h:433-454`），**没有** `cs_fault_report_enable` 字段。
   - 头文件里的 `KBASE_IOCTL_CS_EVENT_SIGNAL`（`:493`）**全树零次调用**（`grep` 无命中）；
     `KBASE_IOCTL_KCPU_QUEUE_*` 只在 `PANVK_KBASE_KCPU_SYNC=1` 时才用
     （`kbase_kmod.c:1365-1371`，**默认关闭**）。
   - 而且等待循环**主动吞掉** `cell->error`：`:794-805`，当
     `(CS_FAULT & 0xff) == 0 && fault_lo == 0 && fault_hi == 0` 就 `goto kbase_wait_continue`
     （`:816`）——在 `error 0x0` 的现场，这条规则让驱动彻底失明。
   - ⇒ `error_type == 0` **不是**"固件没动"的证据，它就是"我们没请求通知、也没把 CS 输出块里
     能读到的东西当回事"的直接后果。最小改动草稿见 §6 Fix 3。【已定论】

6. **两个先例的对照结论**（§5）：**panvk-mtk 先例**（`BkaNeko/panvk-mtk-driver` → `/root/mesa`
   = `funnymdzz/mesa` @`6598829`，G610 / uAPI 1.18）**删掉了门铃快速路径并把超时抬到 120 s**；
   **wonderkast02 先例**（G720）**保留了**快速路径，但它（i）`csi_handlers` 从来没设过、
   （ii）`tiler_work_estimate` 生产者还在、（iii）等待循环里加了一条"状态一变就打印"的日志。
   我们这棵树 = wonderkast02 的发射/门铃/seqno 代码（**逐字相同**）+ 新加的 1.18 rung
   `csi_handlers` + 被删掉的 `tiler_work_estimate` 生产者 + 10 s（不是 120 s）超时。
   ⇒ **与"已跑通"状态相比，与本病灶相关的差异只有两处**：门铃快速路径（对手先例已删）
   和 `csi_handlers`（两个先例都没有）。

---

## 1. 口径、指纹与"哪棵树/哪份产物"

| 项 | 值 | 出处 |
|---|---|---|
| 构建树 | `/root/zenithblue/work/mesa`（`git rev-parse HEAD` = `5a07217f034b3e50d8c7c7794f97a2df1742613b`，shallow） | 报告 14 §1.2 |
| `panvk_vX_gpu_queue.c` | 3261 行 | `wc -l`（本轮） |
| `kbase_kmod.c` | 2085 行 | `wc -l`（本轮） |
| kbase 后端文件 | **未跟踪**（`??`）⇒ 回滚只能 `cp` 备份，不能 `git checkout --` | 报告 14 §1.2 |
| 先例 A（G720） | `wonderkast02/panvk-g720-kbase-csf`，`mesa-snapshot/`，commit `e81514c`；关键 commit `f651667 panvk/kbase: add working G720 Mesa changes` | 本轮 clone 到 `/root/research/23-work/precedent/` |
| 先例 B（G610） | `/root/panvk-mtk`（单 commit `594efbf`，remote `BkaNeko/panvk-mtk-driver`）+ 源码真身 `/root/mesa` = `funnymdzz/mesa` @`6598829` **未打补丁** | 报告 15 §1 |
| 先例 B 的设备 | Redmi Note 11T Pro / 天玑8100 / **Mali-G610 MC6** / **kbase r32p1, uAPI 1.18** | `/root/panvk-mtk/README.md:11-14` |

**本轮产生的对照物**（都在 `/root/research/`）：

| 文件 | 内容 |
|---|---|
| `/root/research/23-work/precedent/` | wonderkast02 先例全仓（shallow） |
| `/root/research/23-work/diff-gpu_queue.txt` | 先例 A `mesa-snapshot/…/panvk_vX_gpu_queue.c` vs 构建树（216 行） |
| `/root/research/23-work/diff-kmod.txt` | 先例 A `mesa-snapshot/…/kbase_kmod.c` vs 构建树（354 行） |
| `/root/research/queue-work/fix1-always-kick.patch` | 修法 1 草稿（对构建树可直接 `git apply`） |
| `/root/research/queue-work/fix2-revert-csi-handlers.patch` | 修法 2 草稿 |
| `/root/research/queue-work/fix3-diagnostics.patch` | 修法 3 草稿（**纯诊断**：error 日志 + 状态变化日志 + 唤醒路径打点） |
| `/root/research/queue-work/fix4-timeout-120s.patch` | 修法 4 草稿（**仅缓解**：10 s → 120 s） |
| `/root/research/queue-work/README.md` | 三份草稿的用法、回滚与 A/B 顺序 |

> **本报告不改任何只读树**。`fix*.patch` 是**草稿**，只放在 `/root/research/queue-work/`，
> 需要上机时由父代理在 `/root/research/queue-work/` 自建目录里 `git apply` 或手动改。

---

## 2. Q1 —— 这条 10 秒超时**逐行**在等什么

### 2.1 提交侧：一次 `vkQueueSubmit` 到环里的一条 wrapper

```
vkQueueSubmit
└─ panvk_per_arch(gpu_queue_submit)                       panvk_vX_gpu_queue.c:2860 附近
   └─ panvk_queue_submit_ioctl_kbase()                    :2702
      ├─ vk_sync_wait_many(...)   /* 入向 CPU 等待先解决 */ :2730-2736
      ├─ pan_kmod_flush_bo_map_syncs()                     :2740
      ├─ for each qsubmit: kbase_subqueue_emit_job()       :2751
      └─ u_foreach_bit(touched) kbase_subqueue_publish()   :2763
```

`kbase_subqueue_emit_job()`（`:465-698`）在**用户态自有的环**里写一条定长 wrapper
（panthor 的内核环由内核代劳；kbase 没有这个 ioctl，所以 Mesa 自己发，见 `:41-53` 的注释）：

| idx | 发射点 | 指令 |
|---|---|---|
| 0-4 | `:511-516` | 5×`SET_STATE_IMM32`（SB_SEL_ENDPOINT / MASK_WAIT / SEL_OTHER=LS / SEL_DEFERRED / MASK_STREAM） |
| 5 | `:526` | `REQ_RESOURCE`（`kbase_resource_mask()`，`:126-138`） |
| 6 | `:529-531` | `MOVE64 ctx_reg ← subq->context` |
| 7-8 | `:575-578` | **仅非 COMPUTE**：`MOVE64 heap_ctx ← tiler_heap.context.dev_addr` + `HEAP_SET` |
| 9-11 | `:601-605` | `MOVE32 flush_id` + `FLUSH_CACHE2(clean+inv / clean+inv / inv)` 延迟在 `IMM_FLUSH` + `WAIT(IMM_FLUSH)` |
| 12-13 | `:606-608` | `MOVE64 stream_addr` + `MOVE32 stream_size` |
| **14** | `:609` | **`CALL stream_addr, stream_size`** |
| 15-18 | `:622-631` | `MOVE64 addr ← seqno_addr`；**`WAIT(all_mask)`**；`MOVE64 val ← 1`；**`SYNC_ADD64`**（延迟在 `DEFERRED_SYNC`） |
| 19 | `:634` | `ERROR_BARRIER` |

- `seqno_addr = kbase_subqueue_seqno_dev_addr(queue, subqueue)`（`:557`），
  `target_seqno = emitted_jobs + 1`（`:558`），末尾 `emitted_jobs++`（`:671`）。
- 环写完立刻 `kbase_cache_clean_range(ring + offset, padded)`（`:662-663`）——
  **这一句只 clean 环，不 clean input page**。

### 2.2 唤醒侧：`kbase_subqueue_publish()`（`:697-727`）

```c
697  kbase_subqueue_publish(struct panvk_gpu_queue *queue, uint32_t subqueue)
...
707     *(volatile uint64_t *)(input_page + CS_USER_IO_INPUT_CS_INSERT) = subq->kbase.insert;
709     kbase_gpu_wmb();                                   /* 只是 dsb sy，:148-157 */
710-713 uint8_t *output_page = user_io + 8192; volatile uint32_t *active = output_page + CS_USER_IO_OUTPUT_CS_ACTIVE;
719     if (*active) {                                     /* ← 读到"队列正在跑" */
721        *(volatile uint32_t *)subq->kbase.user_io = 1;  /* ← page 0 = 硬件门铃页 */
722        kbase_gpu_wmb();
722        if (*active)                                   /* ← 再读一次，仍然没有失效缓存 */
723           return;                                     /* ←★ 直接返回，不调 KICK ★ */
724     }
726     kbase_kmod_csf_queue_kick(dev->kmod.dev, subq->kbase.ringbuf_dev);
```

USER_IO 三页布局（`include/drm-uapi/mali_kbase_ioctl.h:392-400`，权威）：
**page0 = 门铃**、page1 = input（`CS_INSERT @0x0`）、page2 = output（`CS_EXTRACT @0x0`，
`CS_ACTIVE @0x8`）；`BASEP_QUEUE_NR_MMAP_USER_PAGES = 3`；mmap 在
`kbase_kmod.c:775-777`。

### 2.3 等待侧：`kbase_subqueue_wait_seqno()`（`:730-971`）

```
:749   cell        = kbase_subqueue_seqno_cell(queue, subqueue)      /* queue->kbase_seqnos.cpu + idx*ALIGN(sizeof(sync64),64) */
:750   ls_copy     = cell + 16      (:67)
:751   mark_pre_call  = cell + 24   (:68)
:753   mark_post_call = cell + 32   (:69)
:755   mark_post_wait = cell + 40   (:70)
:757   stream_progress= cell + 48   (:71)
:756   target_insert = subq->kbase.insert                /* 进函数那一刻的 insert */
:757   seqno_addr    = kbase_subqueue_seqno_dev_addr(...)
:759   watchdog      = start + KBASE_WAIT_TIMEOUT_NS     /* ★ 10 s，常量 :63 */
:760   deadline      = MIN2(abs_timeout_ns, watchdog)
:779   while (true) {
:780      insert  = *(input_page  + CS_USER_IO_INPUT_CS_INSERT)
:781      extract = *(output_page + CS_USER_IO_OUTPUT_CS_EXTRACT)
:782      active  = *(output_page + CS_USER_IO_OUTPUT_CS_ACTIVE)
:784-788  变化时才记录 prev_extract/prev_seqno（**没有打印**；先例 A 在这里加了日志，见 §5.2）
:788      kbase_cache_invalidate_range(cell, stride)      /* dc civac ×N + dsb sy，:177-191 */
:789      if (cell->seqno >= target_seqno ||                                   ← ★唯一的成功条件
:790          (PANVK_DEBUG(KBASE_DIAG) && *ls_copy >= target_seqno) ||
:791          (allow_ring_drain && extract >= target_insert && !active))
:792         break;
:794      if (cell->error) {  … :805 goto kbase_wait_continue  /* exc==0 时吞掉 */ }
:816   kbase_wait_continue:
:819      now = os_time_get_nano();
:822      if (now - last_kick > 500 ms) { u_foreach_bit(i,rekick_mask) kbase_kmod_csf_queue_kick(...); }   ← 重踢
:833      if ((uint64_t)now >= deadline) {
:837         if (deadline < watchdog) return VK_TIMEOUT;
:840         mesa_loge("kbase: timeout on subqueue %u: …")        ← ★现场那一行
:893         kbase_log_queue_syncobjs(queue)                       ← ★queue syncobj 0/1/2
:895-896     for i: kbase_log_subqueue_state(queue, i, "timeout snapshot")  ← ★现场那一族
:917         return vk_queue_set_lost(&queue->vk, "kbase: timeout on subqueue %u", subqueue);
:933-937     remaining = MIN2(deadline-now, 20 ms);
              cqs_ret = target_seqno ? kbase_kmod_csf_wait_cqs64(dev, seqno_addr, target_seqno-1, remaining) : -1;
:937         if (cqs_ret < 0) { int error_type = kbase_kmod_csf_wait_event(dev->kmod.dev, remaining); … }
:956         prev_error_type = error_type;    /* ★拿到就扔，不作任何处理 */
```

**所以"等的是什么"的准确答案**：
> 等 **`queue->kbase_seqnos[subqueue].seqno` 这个 64 位同步单元**被 GPU 用 wrapper 末尾那条
> `SYNC_ADD64` 加到 `target_seqno`。它**不是** CSG progress、**不是** drm syncobj/fence、
> **不是** KCPU queue，也**不是** `CS_EXTRACT`（`CS_EXTRACT` 只作为 `allow_ring_drain`
> 兜底，而 `kbase_wait_sync_targets`（`:2653-2676`）与 `kbase_queue_wait_current`（`:974-999`）
> 都传 `allow_ring_drain = false`）。
> KCPU CQS wait（`:933`）和 CSF notification（`:937`）**都只是"顺手让内核跑一下"的旁路**，
> 不是完成信号的来源：前者默认关闭（`kbase_kmod.c:1365-1371` 要求 `PANVK_KBASE_KCPU_SYNC=1`），
> 后者只读内核**主动**推来的 `struct base_csf_notification`（`kbase_kmod.c:829-876`）。

### 2.4 为什么不用 `CS_EXTRACT` 判完成（代码自己的理由）

`:777-779` 的注释：
> "CS_EXTRACT only reports how far firmware has fetched the command stream; asynchronous GPU jobs
> issued by those commands may still be running. Only accept the completion writes emitted after the
> all-scoreboard wait."

这条设计是对的（见 §3.2 与 panthor 内核发射序列一致），**但它同时把"环被取空"变成不可信信号**，
于是当 wrapper 自己卡住时，用户态除了 10 秒看门狗没有第二条路。

---

## 3. Q2 —— 为什么等不到：四条候选逐条判定

### 3.0 先把现场解出来

```
kbase: queue syncobj 0: seqno 5313, error 0x0, pad 0x0     ← kbase_log_queue_syncobjs() :245-262
kbase: queue syncobj 1: seqno 10555, error 0x0, pad 0x0        打印的是 queue->syncobjs[]
kbase: queue syncobj 2: seqno 5624,  error 0x0, pad 0x0        (= panvk_cs_subqueue_context.syncobjs，
                                                                由 PanVK 的 CS 流自己写；:1639/:1870-1893)
kbase: timeout snapshot subqueue 0: seqno 433, ls_copy 0, target 433, marks 0/0/0,
       insert 83584, extract 83584, active 0, error 0x0, jobs 433      ← kbase_log_subqueue_state() :354-361
kbase: timeout snapshot subqueue 0 stream progress 0x0                 ← :362-363
kbase: timeout snapshot subqueue 0 last job: ring offset 17856, entry 160/192 bytes,
       stream 0x5ffa2f8000/3048, flush 793                             ← :365-370
kbase: subqueue 0 {extract,last-job} ring[0..7] @{83584,17856} …        ← :372-376 → kbase_log_ring_line :301-318
kbase: timeout snapshot subqueue 1: seqno 432, target 433, insert 83584, extract 83504, active 0, error 0x0
kbase: timeout snapshot subqueue 2: seqno 432, target 433, insert 83584, extract 83488, active 0, error 0x0
```

**解出来的事实**（全部可算）：

1. `insert = 83584`、`extract = 83584` → sq0 **环被取空**；`seqno 433 == target 433` → **sq0 完成**。
   ⇒ **超时的不是 sq0**，是 sq1 和/或 sq2（`kbase_log_subqueue_state` 会把**三个都**打印，
   `:895-896`）。这也是"sq0 的 `seqno == target` 却仍报超时"这个表面矛盾的唯一解释。
2. `83584 − 192 = 83392` = 最后一条 wrapper 的**绝对**起点（`entry 160/192 bytes` ⇒ 补齐 192）。
   `last_job_offset = 17856 = 83392 % 65536` ⇒ 这个字段是**环内相对偏移**，
   `insert/extract` 是**不回绕的绝对偏移**（`:440`、`:477`）。
3. sq1：`83504 = 83392 + 112` → 取到 **idx14 = `CALL`** 之前；sq2：`83488 = 83392 + 96` →
   也是 `CALL` 之前（96 = 112 − 16，因为 COMPUTE 没有 `HEAP_SET`）。
   ⇒ **两个队列的 CS 都停在同一条 `CALL` 前面，10 秒里一步没动。**
4. `error 0x0`：`cell`（即 CS output block 里的 seqno 单元）的 error 字段为 0，`CS_FAULT` 也是 0
   ⇒ **没有任何故障位被置起来**。
5. `marks 0x0/0x0/0x0`、`ls_copy 0`、`stream progress 0x0`：这四个是
   `PANVK_DEBUG(KBASE_DIAG)` 才写的面包屑（`:583-597`、`:613-620`、`:623-628`），
   **没开就恒为 0**，与"固件有没有动"无关（报告 18 已说明）。本轮不再据此推断。
6. `active 0` 对三个 subqueue 都是 0 —— 包括**已经完成**的 sq0 ⇒ **`active` 不能用来判定卡死**。

**结论（候选归类）**：`extract < insert` 且持续 10 s 且 `seqno` 未达 target
⇒ **队列里有未被消费的活，而 CS 没有在跑** ⇒ 属于任务清单里的 **(a) 驱动没 kick / 没 flush**。

### 3.1 候选 (a)：提交的唤醒被丢掉 —— **最可能**（【推断，最高】）

**机制（代码级）**：`kbase_subqueue_publish()`（`:697-727`）在 `*active != 0` 时**只写硬件门铃页**
（`user_io + 0`，`:721`），再读一次 `*active`，若仍非 0 就 **`return`，不调
`KBASE_IOCTL_CS_QUEUE_KICK`**（`:722-723`）。而：

* **硬件门铃只能唤醒"已经在硬件上/已被调度"的 CS**；CS 一旦被固件挂起（off-slot），
  往门铃页写 1 不会让内核调度器把它重新放上硬件——**那正是 `KBASE_IOCTL_CS_QUEUE_KICK` 的职责**。
  这就是 `:712-714` 注释自己承认的语义（"Active queues can consume a userspace doorbell without
  an ioctl… if a suspend raced the write, the scheduler kick below safely resumes the group"）。
* **`active` 的两次读取都没有缓存失效**：`:710-722` 之间没有 `kbase_cache_invalidate_range()`。
  对比等待循环 `:788` 是**有**的。而 `kbase_gpu_wmb()` 只是 `dsb sy`（`:148-157`），
  它保证**写**的顺序，**不会**让一次普通读拿到最新值。
  ⇒ 第二次读 `*active` 完全可能读到**陈旧的非 0**，于是即使内核已经把队列挂起（真实 `active == 0`），
  代码也会走 `return` 分支，**这次提交的唤醒就永久丢了**。
* **为什么恰好是 sq1/sq2 丢、sq0 不丢**：三者是**同一次提交**（`insert` 都是 83584）。
  在一帧的末尾提交时，**顶点/分块队列通常已经空闲**（上一帧的 VT 早就收工），
  而**片元/计算队列往往还在跑上一帧**。⇒ sq0 读到 `active == 0` → 走 ioctl kick → 跑起来；
  sq1/sq2 读到非 0 → 走门铃快路径 → 丢。**这与现场的不对称完全吻合，且不需要任何额外假设。**
* **先例的直接证据**：panvk-mtk 先例（G610/uAPI1.18，MTK，**已跑通**）在
  `/root/panvk-mtk/patches/panvk_mtk.patch:189-207` **把这段整块删掉**，并把注释改成：

  ```c
  +   /* Always kick the scheduler.  The userspace doorbell fast path can race
  +    * an active->idle transition and drop the newly inserted job, leaving the
  +    * GPU idle with pending work until the wait watchdog gives up. */
      kbase_kmod_csf_queue_kick(dev->kmod.dev, subq->kbase.ringbuf_dev);
  ```

  **"leaving the GPU idle with pending work until the wait watchdog gives up"**
  ＝ 我们的 `active 0` + `extract < insert` + 10 s 后 `vk_queue_set_lost`。逐字对上。

**必须承认的反证（这一条我标"未验证"）**：等待循环 `:822-830` 每 **500 ms** 会
用 `rekick_mask` 重踢一次，而 `kbase_wait_sync_targets()`（`:2653-2670`）与
`kbase_queue_wait_current()`（`:982-988`）传的 `rekick_mask` **包含所有有活的 subqueue**。
10 秒≈19 次重踢，理论上应该把丢掉的唤醒补回来。所以：

* 若 **删掉快速路径后 10 秒超时消失** ⇒ 结论 (a) 成立，且说明"重踢也补不回来"
  （可能因为 `KBASE_IOCTL_CS_QUEUE_KICK` 对**已 off-slot 且未被 tick 的 CSG** 与
  首次 kick 语义不同，或者重踢的 `buffer_gpu_addr` 路径没有真正 arm 队列）。
* 若 **删掉后照旧** ⇒ (a) 只是"必要但不充分"，请转 §3.3 (c) 与 §6 Fix 2。

**这也是为什么 Fix 3 里要加一条"两条唤醒路径分别打点"的日志**（§6），
一次上机就能把这条不确定性彻底消掉。

### 3.2 候选 (b)：等待对象选错 —— **排除**（【已定论】）

把 Mesa 的 wrapper 尾部和 **panthor 内核**的发射序列逐条对照
（权威源码 `drivers/gpu/drm/panthor/panthor_sched.c`，`prepare_job_instrs()`，
本轮 curl 到 `/root/research/23-work/panthor_sched.c`）：

| panthor 内核 | 编码 | Mesa kbase wrapper | 是否一致 |
|---|---|---|---|
| `MOV48 rX:rX+1, cs.start` / `MOV32 rX+2, cs.size` | op 1 / op 2 | `:606-608` | ✅ |
| `WAIT(0)`（等 FLUSH） | `(3<<56)\|(1<<16)` | `:605` `cs_wait_slot(IMM_FLUSH)` | ✅ |
| `CALL rX:rX+1, rX+2` | `(32<<56)\|(addr<<40)\|(val<<32)` | `:609` | ✅ |
| `MOV48 addr, sync_addr` / `MOV48 val, #1` | op 1 | `:622-628` | ✅ |
| **`WAIT(all)`** | `(3<<56)\|(waitall_mask<<16)`，`waitall_mask = GENMASK(sb_slot_count-1,0)` | `:629` `cs_wait_slots(dev->csf.sb.all_mask)`，`all_mask = BITFIELD_MASK(scoreboard_slot_count)`（`panvk_vX_device.c:536`） | ✅ |
| **`SYNC_ADD64.system_scope.prop.nowait`** | `(51<<56)\|(0<<48)\|(addr<<40)\|(val<<32)\|(0<<16)\|1` | `:630-631` `cs_sync64_add(b, true, MALI_CS_SYNC_SCOPE_SYSTEM, val64, addr64, cs_defer(0, SB_ID(DEFERRED_SYNC)))` | ✅ **逐位相同** |
| `ERROR_BARRIER` | `(47<<56)` | `:634` | ✅ |

`SYNC_ADD64` 的位域（`src/panfrost/genxml/v12.xml:1004-1015`）：
`Error Propagate[0]=1`、`Scope[2:1]=SYSTEM`、`Wait Mask[31:16]=0`、`Data[39:32]=val_reg`、
`Address[47:40]=addr_reg`、`Signal slot[51:48]=0`、`Defer Mode[52]=0`、`Opcode[63:56]=51`。
`Defer Mode[52]=0` 就是枚举里的 `Defer Immediate`（`v12.xml:577-580`），
和 panthor 写的 `0` **同一个值**；Mesa 传的 `SB_ID(DEFERRED_SYNC)=1`
（`panvk_cmd_buffer.h:280` `PANVK_SB_DEFERRED_SYNC = 1`）经 `cs_apply_async`
（`cs_builder.h:880-891`）会被 `signal_slot = cs_instr_is_asynchronous(SYNC_ADD64,0) ? … : 0`
改写成 **0**，因为 `SYNC_ADD64` 不在 `cs_instr_is_asynchronous()` 的异步表里
（`cs_builder.h:815-833`，PAN_ARCH 12 走 `default: return false`）。
⇒ **`cs_defer(0, SB_ID(DEFERRED_SYNC))` 对这条 `SYNC_ADD64` 实际上等价于"wait_mask=0 + signal 0"，
与 panthor 内核完全一致。**

**更强的正面证据**：同一棵树、同一套机制下 **sq0 的 `cell->seqno` 达到了 `433 == target`**
（现场 dump 自己打印的）⇒ **"SYNC_ADD64 → cell → `:789` 判定"这条链在真机上是通的**，
不存在"等错 seqno / 等错 cell"的结构性错误。

⇒ 候选 (b) **排除**。

### 3.3 候选 (c)：固件没发完成回调，而我们没请求通知 —— **次可能，且是新回归**（【推断，中高】）

**(c-1) 协议层：本机确实无法启用完成/故障通知**（这一条是【已定论】，见 Q3/§4）。

**(c-2) 语义层（真正的嫌疑）：v54+ 新加的 `csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG`
把"tiler OOM"从"可见的致命事件"变成了"静默交给应用 handler 的事件"。**

* 改动位置：`kbase_kmod.c:552`（1.25 rung）与 **`:582`（1.18 rung，本机 uAPI 1.21 走这条）**。
  **两个先例都没有这个字段**（§5.1 / §5.2 逐字对照）。
* 内核语义（公开 kbase r43p0 `mali_kbase_csf.c:1926-1937`，见报告 14 §5 Fix A 引用）：
  ```c
  if ((group->csi_handlers & BASE_CSF_TILER_OOM_EXCEPTION_FLAG) &&
      (pending_frag_count == 0) && (err == -ENOMEM || err == -EBUSY)) {
          new_chunk_ptr = 0;      /* 交给应用的 CSI TILER_OOM handler，内核不再杀组 */
  } else if (err == -EBUSY) { new_chunk_ptr = 0; }
  else if (err) return err;       /* ← 没有 flag 时：term_queue_group + report_tiler_oom_error */
  ```
  ⇒ **置位之后内核不再发 `BASE_GPU_QUEUE_GROUP_ERROR_TILER_HEAP_OOM` 通知**
  ⇒ **没有通知 = 没有 error_type = `error 0x0`**，而组还活着、队列却因为固件进入/返回
  app handler 而卡住。
* **这和现场的自洽性很强**：报告 14 的 OOM 现场是"有 OOM 通知 → 10 s → `-4`"；
  现在是"**没有** OOM 通知 → 10 s → `-4`"，而且 `tiler heap renewal` 周期性出现。
  唯一的解释就是 OOM 仍然在发生，但被 `csi_handlers` **静默转走**了。
* **而且"renew 消灭了 OOM"这个前提并不牢固**：`kbase_renew_tiler_heap()`
  （`:2192-2247`）用 `phys_dev->csf.tiler.initial_chunks` **重建**堆，而那是 **10**（10 MiB，
  `panvk_physical_device.c:1371-1373`），renew 间隔是 **128 次图形提交**（`:2815`、
  `kbase_tiler_heap_renew_interval()` `:77-88`）。**每 128 次提交就把 400 MiB 的预算打回 10 MiB**，
  中间任何一次几何膨胀都可能再次 OOM。先例的节奏**不是**这样：先例把 renew 门控在
  `submit->tiler_work_estimate != 0` 上，而**我们这棵树把 `tiler_work_estimate` 的生产者删掉了**
  （§5.2），于是变成了"每 128 次无条件重建、每次只给 10 chunk"。
* **报告 14 自己已经警告过**（§5 Fix A 的"风险/前提"）：
  > "固件必须支持'NULL chunk ⇒ 触发 app 的 TILER_OOM handler'。panvk 的 handler 已存在，
  > 但**从未在这台设备上被激活过** ⇒ 首次上机必须留 `=0` 对照组。"

  报告 14 的 §6.3 要求 A/B 对照，**但从 v54 的现场看，这个对照组显然没有做**（或缺档）。

**(c-2) 的反证**：stuck 的是 **FRAGMENT（sq1）和 COMPUTE（sq2）**，
而 TILER_OOM handler 只注册在 fragment 队列上（`panvk_vX_gpu_queue.c:1660-1668`：
"`The tiler OOM exception handler is registered to the fragment queue`"）。
一个 tiler OOM 不容易直接解释 COMPUTE 也停在 `CALL` 前——除非 COMPUTE 的流在等一个
只有 fragment/VT 会 signal 的跨队列 scoreboard（**未验证**）。
⇒ 因此 (c-2) 我排在 (a) 之后，但它的**修法成本更低（一行）**，所以 §6 把它列为**并行 A/B 的第一条**。

### 3.4 候选 (d)：其它

| 假设 | 判定 | 依据 |
|---|---|---|
| `KBASE_WAIT_TIMEOUT_NS` 10 s 太短，作业本来就要跑更久 | **排除**（作解释），**保留**（作缓解） | 现场是 `extract` **完全不动** 10 s（不是"跑得慢"）；但先例把它抬到 **120 s**（`panvk_mtk.patch:180-181`），说明 MTK kbase 上确实存在"需要更久"的合法场景。见 Fix 3 |
| `cs_reg_count` 推导错误导致 wrapper 用了不存在的寄存器（`reg = cs_reg_count-4`，`:566`） | **可能，但会影响全部三个 subqueue，与 sq0 成功矛盾** | 树里已有钳制（`kbase_kmod.c:1294-1302`，只钳"不合理的 96/128"）；先例 A 是**硬编码 96 且不设 `nr_kernel_registers`**（`diff-kmod.txt` hunk `@@ -445,14 +438,12 @@`），我们是从 GLB iface 推导。**未验证** |
| BO 缓存一致性：`cpu_gpu_coherent` 分支让我们**跳过 `KBASE_IOCTL_MEM_SYNC`**（`diff-kmod.txt` `@@ -1908,23 +1796,16 @@`），先例**从不忘 sync** | **未验证，但值得单独一条实验** | 环被取空说明环的 clean 是有效的；但**callee 流（`stream 0x5ffa2f8000/3048`）的可见性**没有独立证据。若流没同步，固件会取到垃圾——通常会 fault（`error 0x0` 不支持），但"取到垃圾后卡在某个 WAIT"也可能不 fault |
| `dma_heap` 用 `O_RDONLY`（`diff-kmod.txt` `@@ -1334,7 +1275,7 @@`，先例是 `O_RDWR`） | **与本案无关**（采纳报告 14 §4 H1 的论据） | BO 分配路径不走 dma_heap 的 tiler chunk；且 clear/copy/AHB 全绿 |
| tiler heap 的 renew/retire 时序把固件手里的 heap context 换掉 | **未验证** | `kbase_try_destroy_retired_heap()`（`:2160-2190`）与 `kbase_renew_tiler_heap()`（`:2192-2247`）都做了"先 drain 图形队列再换"的保护；但 renew 是**每 128 次无条件**触发，而先例不是 |
| 几何/描述符错误让 FRAGMENT 等一个永不到来的 tiler 完成信号 | **可能是"上游原因"**（任务说另有人处理几何） | 若 fragment 卡在 callee 内部等 tiling，`extract` 停在 `CALL` 前也说得通（`CALL` 前的 `extract` 只表示"还没取到 CALL"，而**取到 CALL 之后**的 `extract` 会跳到 callee 地址，所以"停在这里"其实意味着**连 CALL 都没取**，即队列根本没被跑起来）。⇒ 支持 (a)/(c) 而不是"卡在 callee 里"（**这一条我标【未验证】**：CS 的 `EXTRACT` 在 `CALL` 期间的确切语义未从权威文档确认） |

---

## 4. Q3 —— `error_type == 0` ⇒ 驱动**没有**注册/enable 任何完成或异常通知【核实 + 最小改动】

### 4.1 核实（全部【已定论】）

| 问题 | 答案 | 证据 |
|---|---|---|
| `cs_fault_report_enable` 注册了吗？ | **本机根本没这个字段可用**。它只在 **1.25 布局**里（`kbase_kmod.c:551`）。本机 uAPI 1.21 走 **1.18 rung**（`:570-594`），40 B 结构里字段是 `tiler_mask/fragment_mask/compute_mask/cs_min/priority/tiler_max/fragment_max/compute_max/csi_handlers/padding[2]/dvs_buf` —— **没有** `cs_fault_report_enable` | `include/drm-uapi/mali_kbase_ioctl.h:433-454` |
| `csi_handlers` 注册了吗？ | **注册了**，但只注册了 **`BASE_CSF_TILER_OOM_EXCEPTION_FLAG = 1u<<0`**（tiler OOM 专用），**与完成/故障通知无关** | `:582`；`include/drm-uapi/mali_kbase_ioctl.h:429-431`、`mali_base_csf_kernel.h:133-134` |
| `KBASE_IOCTL_CS_EVENT_*` 用了吗？ | **零次**。头里有 `KBASE_IOCTL_CS_EVENT_SIGNAL`（nr 44，`:493`），全树无调用点 | `grep -rn 'KBASE_IOCTL_CS_EVENT' src/` 无命中 |
| KCPU queue（CQS wait / fence signal）启用了吗？ | **默认关闭**，必须 `PANVK_KBASE_KCPU_SYNC=1` | `kbase_kmod.c:1365-1371` |
| 那 `read(dev->fd)` 收到的是什么？ | 内核**主动**推的 `struct base_csf_notification`（64 B，`:653`），只在 FATAL / QUEUE_FATAL / TIMEOUT / TILER_HEAP_OOM / QUEUE_ERROR_FAULT 时才有 | `kbase_kmod.c:650-676`、`:829-876` |
| 拿到 `error_type` 之后怎么办？ | **只存进 `prev_error_type` 就丢掉**（`:956`），循环继续；`csf_error` 这个 latch 只在 `gpu_queue_check_status()`（`:3207` 附近）被读，而等待循环**不看**它 | `:937-956`、报告 14 §2.4 |
| `cell->error` 呢？ | 等待循环在 `:794-805` **主动吞掉** `exc==0 && info==0` 的情况（`goto kbase_wait_continue`，`:816`） | `:794-816` |

**结论**：`error_type == 0` 是**预期行为**，不是"固件没动"。
本机在协议层拿不到"可恢复 CS 故障 / 完成"通知，驱动也没有用
`KBASE_IOCTL_CS_EVENT_SIGNAL` 或 KCPU queue 去主动建立任何通知通道。
⇒ **一旦停住，用户态唯一的信息来源就是 `CS_EXTRACT/CS_ACTIVE/CS_FAULT` 那几个寄存器镜像，
而它把 `CS_FAULT` 的 `exc==0` 当噪声丢掉了。**

### 4.2 最小改动草稿（诊断用，不是修法本体）

见 `fix3-diagnostics.patch`。三处，全部只加日志（自检：APPLIES-CLEAN）：

**改法 3a —— 不要静默吞掉 `cell->error`（`panvk_vX_gpu_queue.c:794-805`）**：

```diff
       if (cell->error) {
          uint32_t cs_fault = *(volatile uint32_t *)(output_page + 0x80);
          uint32_t cs_fault_lo = *(volatile uint32_t *)(output_page + 0x88);
          uint32_t cs_fault_hi = *(volatile uint32_t *)(output_page + 0x8c);
          uint32_t exc = cs_fault & 0xffu;
-         if (exc == 0 && cs_fault_lo == 0 && cs_fault_hi == 0)
-            goto kbase_wait_continue;
+         if (exc == 0 && cs_fault_lo == 0 && cs_fault_hi == 0) {
+            /* Do not go blind: the CS output block is the ONLY fault channel
+             * on kbase uAPI < 1.25 (no cs_fault_report_enable in the 1.18
+             * layout, no KBASE_IOCTL_CS_EVENT_* registration anywhere in this
+             * backend).  Log it once per wait so a wedged queue leaves a
+             * trace even when the exception type reads 0. */
+            static bool logged;
+            if (!logged) {
+               logged = true;
+               mesa_logw("kbase: CS error flag 0x%x on subqueue %u with "
+                         "exception 0x%x / info 0x%08x%08x (no fault info; "
+                         "kbase uAPI < 1.25 cannot enable CS fault reporting)",
+                         cell->error, subqueue, exc, cs_fault_hi, cs_fault_lo);
+            }
+            goto kbase_wait_continue;
+         }
```

**改法 3b —— 在等待循环里加"一变就打印"（`:784-788`，这是先例 A 唯一在等待循环里的改动，
逐字见 §5.2）**：

```diff
       if (prev_extract != extract || prev_seqno != cell->seqno) {
-         (void)active;
+         mesa_logi("kbase: running subqueue=%u, insert=%" PRIu64
+                   ", extract=%" PRIu64 ", active=%u, target_insert=%" PRIu64
+                   ", ls_copy=%" PRIu64 ", cell->seqno=%" PRIu64
+                   ", target_seqno=%" PRIu64 ", progress=0x%x",
+                   subqueue, insert, extract, active, target_insert,
+                   *ls_copy, (uint64_t)cell->seqno, target_seqno,
+                   *stream_progress);
          prev_extract = extract;
          prev_seqno = cell->seqno;
       }
```

> 这一条是**最小验证的关键**：它把"CS 到底动没动、动到哪"变成 logcat 里可见的。
> 先例 A 正是加了这个才把 hang 判出来的。

**改法 3c —— 把两条唤醒路径分别打点（`:719-726`）**：

```diff
    if (*active) {
       *(volatile uint32_t *)subq->kbase.user_io = 1;
       kbase_gpu_wmb();
-      if (*active)
+      if (*active) {
+         mesa_logd("kbase: publish subqueue %u via hardware doorbell only "
+                   "(insert %" PRIu64 ")", subqueue, subq->kbase.insert);
          return;
+      }
+      mesa_logd("kbase: publish subqueue %u: busied doorbell then kicked "
+                "(active transitioned 1->0)", subqueue);
    }
    kbase_kmod_csf_queue_kick(dev->kmod.dev, subq->kbase.ringbuf_dev);
```

**回滚**：三个文件都是普通 tracked 文件（`gpu_queue.c` 是 tracked），
`git -C /root/zenithblue/work/mesa diff > /root/research/queue-work/fix3.rollback.patch`
留证后 `git checkout -- src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c`。

---

## 5. Q5 —— 先例**逐文件**对照：可能就是要找的那处差异

### 5.1 先例 B（panvk-mtk / `BkaNeko/panvk-mtk-driver` → `/root/mesa`，G610 / uAPI 1.18，MTK，**已跑通**）

补丁 `/root/panvk-mtk/patches/panvk_mtk.patch`，命中我们两个焦点：
**门铃快速路径**与**超时常量**。补丁原文（`:180-207`，逐字）：

```diff
 #define KBASE_RING_JOB_MAX_SIZE 512
 /* Generous timeout for the synchronous submission model. */
-#define KBASE_WAIT_TIMEOUT_NS  (10ll * 1000000000ll)
+#define KBASE_WAIT_TIMEOUT_NS  (120ll * 1000000000ll)
@@ -691,21 +691,9 @@ kbase_subqueue_publish(struct panvk_gpu_queue *queue, uint32_t subqueue)
    *(volatile uint64_t *)(input_page + CS_USER_IO_INPUT_CS_INSERT) =
       subq->kbase.insert;
 
-   /* Active queues can consume a userspace doorbell without an ioctl.  Check
-    * CS_ACTIVE again after ringing it; if a suspend raced the write, the
-    * scheduler kick below safely resumes the group. */
-   kbase_gpu_wmb();
-   uint8_t *output_page = (uint8_t *)subq->kbase.user_io + 8192;
-   volatile uint32_t *active =
-      (volatile uint32_t *)(output_page + CS_USER_IO_OUTPUT_CS_ACTIVE);
-
-   if (*active) {
-      *(volatile uint32_t *)subq->kbase.user_io = 1;
-      kbase_gpu_wmb();
-      if (*active)
-         return;
-   }
-
+   /* Always kick the scheduler.  The userspace doorbell fast path can race
+    * an active->idle transition and drop the newly inserted job, leaving the
+    * GPU idle with pending work until the wait watchdog gives up. */
    kbase_kmod_csf_queue_kick(dev->kmod.dev, subq->kbase.ringbuf_dev);
 }
```

**`/root/mesa` 原始态**（未打补丁）的 `:680-710` 与我们构建树的 `:697-727`
**逐字相同**（本轮 `sed -n '680,710p'` 复核）⇒ **这正是"先例做对了、我们没做"的那一处**：
它的注释把我们的现场症状（`active 0` + `extract < insert` + 看门狗放弃）一字不差地写了出来。

**同补丁其它 hunk（与本病灶无关，但属于"先例 vs 我们"的完整差异）**：
AFBC body 4096 对齐（`pan_mod.c` / `pan_desc.c`，先例的"白屏根因"）、
`panvk_image.c` 的 `DRM_FORMAT_MOD_INVALID` → MTK modifier `0x0800000000000072` 回退
（我们走的是相反的 `DRM_FORMAT_MOD_LINEAR` 回退）——
这两条影响的是**画面/一致性**，不是完成信号（与报告 15 的 H4/H5 一致）。

### 5.2 先例 A（wonderkast02 / `mesa-snapshot/`，G720，**已跑通**）：`diff-gpu_queue.txt` + `diff-kmod.txt`

**先强调结论：在"发射 / 门铃 / seqno / 等待"这四个焦点上，先例 A 与我们的构建树
逐字相同。** `diff-gpu_queue.txt` 的 hunk 只落在这些行：
`1`(版权/`#include <stdint.h>`)、`781`(等待循环日志)、`792`(error 判定)、`1483`(注释)、
`2303`(**tiler_work_estimate 累加**)、`2568`/`2587`(utrace clone 实现)、`2672`(日志)、
`2777`/`2795`/`2810`(**renew 门控 + 日志**)、`2994`(pandecode)。
**`:465-698`（emit_job）、`:697-727`（publish）、`:379-423`（init_seqnos）、
`:730-971`（wait_seqno 主体）里一个 hunk 都没有。**

逐条列出**确实是差异**的（按对本病灶的解释力排序）：

| # | 位置（我们树） | 先例 A | 我们 | 解释力 | 定论度 |
|---|---|---|---|---|---|
| **A1** | `kbase_kmod.c:552` / **`:582`** | **完全没有 `csi_handlers`**；也没有 1.18 rung（uAPI<1.25 时直接落 `_1_6`） | 1.25 rung **和**新加的 1.18 rung 都置 `csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG` | **高**：静默转走 OOM ⇒ 无通知、无 error_type，见 §3.3 (c-2) | 【已定论】 |
| **A2** | `panvk_vX_gpu_queue.c:716-725` | **保留**快速路径（与我们逐字相同） | 保留快速路径 | **高（但先例 A 没证伪它）**：先例 B 明确删掉了它，见 §5.1 | 【已定论】 |
| **A3** | `panvk_vX_gpu_queue.c:2303` 附近（`panvk_queue_submit_init_cmdbufs`） | **有**：`submit->tiler_work_estimate += cmdbuf->state.tiler_work_estimate`（带 `UINT64_MAX` 防溢） | **没有**该累加；且 `panvk_cmd_draw` 里也没有往 `cmdbuf->state.tiler_work_estimate` 写（报告 15 §2 H2） | **中高**：renew 门控的输入恒 0 ⇒ 先例的节奏失效；我们先改成"每 128 次无条件 renew 且打回 10 chunk"，见 §3.3(c-2) | 【已定论】 |
| **A4** | `panvk_vX_gpu_queue.c:63` | 10 s（先例 A 未改） | 10 s | 低（先例 A 也用 10 s 且跑通；先例 B 抬到 120 s） | 【已定论】 |
| **A5** | `panvk_vX_gpu_queue.c:784-788` | **有** `mesa_logi("kbase: running subqueue=%u, insert=%lu, extract=%lu, active=%u, target_insert=%lu, ls_copy=%lu, cell->seqno=%lu, target_seqno=%lu, progress=%x", …)`，在 `prev_extract != extract \|\| prev_seqno != cell->seqno` 时打印 | `(void)active;` **什么都不打** | **诊断力极高**（这正说明先例作者也在同一处 debug hang） | 【已定论】 |
| **A6** | `kbase_kmod.c:1294-1302` | 无 `cs_reg_count` 钳制（先例注释说"panthor 硬编码 96 个 work register + 4 个非保留"） | 从 GLB iface `WORK_REGS` 推导 + 钳到 96/128 | 低（但见 §3.4 末行） | 【已定论】 |
| **A7** | `kbase_kmod.c:1285` 附近 `dma_heap` fd | `open(..., O_RDWR)` | `open(..., O_RDONLY)` | 低（采纳报告 14 §4 H1） | 【已定论】 |
| **A8** | `kbase_kmod.c:794-805` | **任何** `cell->error` 立即 `vk_queue_set_lost` | 加了 `exc==0 && info==0` 的"这不是故障"过滤，于是**咽掉** | 中：让我们在 `error 0x0` 时彻底失明 | 【已定论】 |
| **A9** | `kbase_kmod.c:1451-1462` | 无 `PANVK_KBASE_IMPORT_CLEAR/SET` | 有 | 无 | 【已定论】 |
| **A10** | `kbase_kmod.c:1470-1480` 等 | 无 `cpu_gpu_coherent`（**从不忘 `MEM_SYNC`**） | 有：对 `BASE_MEM_COHERENT_SYSTEM` **跳过 `KBASE_IOCTL_MEM_SYNC`** | **未验证**（见 §3.4） | 【已定论（代码）+ 未验证（因果）】 |
| **A11** | `panvk_vX_gpu_queue.c:2568-2612` | utrace clone 用单 root buffer | 多 buffer + 溢出保护 | 无（诊断基建） | 【已定论】 |
| **A12** | `panvk_vX_gpu_queue.c:2303` 之外的 `panvk_vX_cmd_draw.c` | 有 `account_tiler_work()`（`cmd_draw.c:2935-2942`、`:3583-3586`）与二级→一级累加（`cmd_buffer.c:1029-1034`） | **零次赋值** | 同 A3 | 【已定论】 |

### 5.3 "可能就是我们要的那处差异"——我的排序

1. **`kbase_subqueue_publish()` 的门铃快速路径**（`panvk_vX_gpu_queue.c:716-725`）：
   唯一有"已跑通先例把它当作 hang 根因删掉"的**直接文本证据**，且注释与现场症状逐字吻合。
   **先例 A 保留了它 ⇒ 说明它在某些 MTK kbase 固件上不致命；先例 B 删了它 ⇒ 说明在另一些上致命。**
   我们这台是第三种 MTK kbase（uAPI 1.21，比先例 B 的 1.18 新、比先例 A 的 G720 MC8 更接近我们）。
2. **`csi_handlers`（`kbase_kmod.c:582`）**：唯一"v54 新加、两个先例都没有"的**协议层**改动，
   它直接把一个原本**可见**的事件变成**不可见**，与"OOM 通知消失而崩溃仍在"高度自洽。
3. **A3/A12（`tiler_work_estimate` 生产者缺失）**：把 renew 从"按工作量"变成
   "每 128 次无条件打回 10 chunk"，显著抬高 OOM 概率，是 (2) 的**上游扳机**。

---

## 6. Q4 —— 可实施修法（按成本排序）+ 回滚 + 最小验证

> 草稿位置：`/root/research/queue-work/{fix1-always-kick,fix2-revert-csi-handlers,fix3-diagnostics,fix4-timeout-120s}.patch`
> 四份草稿已在 `/root/research/queue-work/verify/` 的副本上 `patch -p1 --dry-run` 验证 **全部 APPLIES-CLEAN**；
> fix1+fix2 可同树共存；**fix1 与 fix3 的 hunk#1 改同一段，不要同时打**。用法与回滚见 `/root/research/queue-work/README.md`。
> **所有草稿都只放在 `/root/research/`**；不要在 `/root/mesa`、`/root/zenithblue`、`/root/MobileGL` 里试编译。
> 要试编译/试打补丁，请在 `/root/research/queue-work/` 里自建目录（例如把构建树 `cp -a` 一份过去）。

### Fix 0（零成本，先做）：加诊断，把不确定性变确定

应用 `fix3-diagnostics.patch` 的 **3b + 3c**（两条 `mesa_logi`/`mesa_logd`）。
上机一次，判据：
* 日志里出现 `kbase: running subqueue=N, …, extract=…, active=…`（先例 A 的那条）——
  ⇒ 说明代码真的走到了"状态变化"分支；
* **extract 停在某个值之后不再变化** ⇒ 队列确实没被跑起来（(a)/(c) 二者之一）；
* 若出现 `kbase: publish subqueue N via hardware doorbell only` 而之后 extract 不动
  ⇒ **(a) 直接成立**（丢唤醒）。

### Fix 1（最高性价比，~7 行删除）：**总是 kick 调度器**

**文件:行号**：`src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c:716-725`
（函数 `kbase_subqueue_publish()`，`:697`）
**改法**：删掉整个 `if (*active) { … return; }` 块，无条件走到 `:726` 的
`kbase_kmod_csf_queue_kick()`。草稿 = `fix1-always-kick.patch`（与先例 B 的补丁逐字一致，
含它的注释原文）。

```diff
@@ -708,19 +708,11 @@ kbase_subqueue_publish(struct panvk_gpu_queue *queue, uint32_t subqueue)
    *(volatile uint64_t *)(input_page + CS_USER_IO_INPUT_CS_INSERT) =
       subq->kbase.insert;
 
-   /* Active queues can consume a userspace doorbell without an ioctl.  Check
-    * CS_ACTIVE again after ringing it; if a suspend raced the write, the
-    * scheduler kick below safely resumes the group. */
-   kbase_gpu_wmb();
-   uint8_t *output_page = (uint8_t *)subq->kbase.user_io + 8192;
-   volatile uint32_t *active =
-      (volatile uint32_t *)(output_page + CS_USER_IO_OUTPUT_CS_ACTIVE);
-
-   if (*active) {
-      *(volatile uint32_t *)subq->kbase.user_io = 1;
-      kbase_gpu_wmb();
-      if (*active)
-         return;
-   }
-
+   /* Always kick the scheduler.  The userspace doorbell fast path can race
+    * an active->idle transition and drop the newly inserted job, leaving the
+    * GPU idle with pending work until the wait watchdog gives up.
+    * (Precedent: BkaNeko/panvk-mtk-driver, patches/panvk_mtk.patch:197-201.)
+    * Note the fast path also read CS_ACTIVE twice without a cache invalidate
+    * (kbase_gpu_wmb() is dsb sy only), so the second read could be stale. */
    kbase_kmod_csf_queue_kick(dev->kmod.dev, subq->kbase.ringbuf_dev);
```

* **代价**：每次提交多一次 `KBASE_IOCTL_CS_QUEUE_KICK`（一个 ioctl）。丢弃的是"省一次
  ioctl"的微优化，换来"不丢唤醒"。先例 B 就是这么取舍的。
* **风险**：极低。`kbase_kmod_csf_queue_kick()` 只在 ioctl 失败时 `mesa_loge`（`:815-822`）。
* **回滚**：`git -C /root/zenithblue/work/mesa checkout -- src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c`
  （改前先 `git diff > …/fix1.rollback.patch`）。

### Fix 2（成本同样低，一行）：**把 `csi_handlers` 退回 0，做报告 14 一直没做的对照组**

**文件:行号**：`src/panfrost/lib/kmod/kbase_kmod.c:552`（1.25 rung）与 **`:582`（1.18 rung）**
**改法**：把两处 `csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG` 改成 `0`
（或加环境变量开关，默认 0 以便 A/B）。草稿 = `fix2-revert-csi-handlers.patch`。

```diff
@@ kbase_kmod.c
-            .csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG,
+            /* A/B: report 14 §6.3 required a control group.  With the flag set
+             * the kernel silently hands a NULL chunk to the application's CSI
+             * handler instead of sending BASE_GPU_QUEUE_GROUP_ERROR_TILER_HEAP_OOM
+             * (mali_kbase_csf.c handle_oom_event()).  That turns a visible fatal
+             * event into an invisible wedge: error_type stays 0 and the fragment
+             * queue can stop mid-stream.  Default OFF; set
+             * PANVK_KBASE_TILER_OOM_HANDLER=1 to re-enable the experiment. */
+            .csi_handlers =
+               (getenv("PANVK_KBASE_TILER_OOM_HANDLER") &&
+                !strcmp(getenv("PANVK_KBASE_TILER_OOM_HANDLER"), "1"))
+                  ? BASE_CSF_TILER_OOM_EXCEPTION_FLAG : 0,
```
（`:582` 同样改。）

* **依据**：两个"已跑通"先例都**没有**这个字段；报告 14 §5 Fix A 自己写了
  "从未在这台设备上被激活过 ⇒ 首次上机必须留 `=0` 对照组"。
* **代价**：退回"tiler OOM 会杀组"的老行为。所以**必须同时做 Fix 2b 让 OOM 不轻易发生**。
* **回滚**：`kbase_kmod.c` 是 **untracked** 文件（报告 14 §1.2）⇒
  **先 `cp src/panfrost/lib/kmod/kbase_kmod.c /root/research/queue-work/kbase_kmod.c.bak`**，
  回滚即 `cp` 回来。

### Fix 2b（中成本，~15 行）：**让 renew 不把堆打回 10 chunk + 恢复 `tiler_work_estimate` 生产者**

**问题**：`:2815-2823` 现在**每 128 次图形提交无条件 renew**，而
`kbase_renew_tiler_heap()`（`:2192-2247`）用 `phys_dev->csf.tiler.initial_chunks`（= **10**，
`panvk_physical_device.c:1371-1373`）重建 ⇒ 每 128 次提交把预算打回 10 MiB。
先例 A 的节奏是 `submit->tiler_work_estimate != 0 && (submits >= interval || work >= renew_work)`。

**改法（两条一起）**：
1. 恢复生产者（先例 A 的位置，`diff-gpu_queue.txt` hunk `@@ -2303,6 +2288,12 @@`）：
   `panvk_queue_submit_init_cmdbufs()` 里加
   ```c
   if (UINT64_MAX - submit->tiler_work_estimate < cmdbuf->state.tiler_work_estimate)
      submit->tiler_work_estimate = UINT64_MAX;
   else
      submit->tiler_work_estimate += cmdbuf->state.tiler_work_estimate;
   ```
   并恢复 `panvk_cmd_draw.c` / `panvk_vX_cmd_buffer.c` 的赋值（报告 15 §5 P2 给了逐行 diff）。
2. 把 renew 门控改回"有工作量才 renew"（`:2790-2800`），并把 renew 后的堆起步值调大
   （例如 `initial_chunks` 用 32；报告 14 Fix C 已给 `PANVK_KBASE_TILER_INITIAL_CHUNKS` /
   `PANVK_KBASE_TILER_MAX_CHUNKS` 的 env 草稿，便于二分）。
* **代价**：需要一次上机标定 `PANVK_KBASE_HEAP_RENEW_WORK`（先例默认 65536）。
* **回滚**：`gpu_queue.c`/`cmd_draw.c`/`cmd_buffer.c` 是 tracked ⇒ `git checkout --`；
  `panvk_physical_device.c` 同理。

### Fix 3（缓解，2 行）：**超时 10 s → 120 s**

**文件:行号**：`src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c:63`
```diff
-#define KBASE_WAIT_TIMEOUT_NS  (10ll * 1000000000ll)
+#define KBASE_WAIT_TIMEOUT_NS  (120ll * 1000000000ll)
```
* **依据**：先例 B 的补丁 `panvk_mtk.patch:180-181` 做了**完全相同**的改动。
* **注意**：这**只是缓解**。它把"10 秒后 `-4` 崩"变成"可能自愈或 120 秒后才崩"，
  **不解决**完成信号为什么不来。**不要单独上这一条**，否则会把现场证据也拉长 12 倍。
* **回滚**：`git checkout --`。

### 上机顺序（A/B，每步单独存档 logcat）

1. **Fix 0（仅诊断）** → 一次上机，拿到 `running subqueue=…` 与 `publish … via hardware doorbell only` 的分布。
2. **Fix 1（总是 kick）单独上** → 判据见下。**若崩溃消失 ⇒ 定案 (a)。**
3. 若第 2 步无效：**撤销 Fix 1**（或保留，无害），**上 Fix 2（`csi_handlers=0`）** → 判据见下。
   **若"OOM 通知行重新出现 + 崩溃消失" ⇒ 定案 (c-2)**（并把 Fix 2b 一起上，避免 OOM 真的发生）。
4. 两项都上了仍崩：上 **Fix 2b**，再上 **Fix 3**（仅用于延长观察窗口）。

### 最小验证：怎么判定修好了

**工具（现有探针，不用新写）**：`/root/research/probe10/panvk_wsi_probe`
（build: `/root/research/probe10/build.sh`，NDK r27c；设备侧原话见源码头注释 `:38-39`）：

```
adb push panvk_wsi_probe libvulkan_panfrost.so runner.sh /data/local/tmp/
adb shell /data/local/tmp/panvk_wsi_probe --icd=/data/local/tmp/libvulkan_panfrost.so --mode=win
adb shell /data/local/tmp/panvk_wsi_probe --icd=/data/local/tmp/libvulkan_panfrost.so --mode=tri
```

* `--mode=win`（`:1108-1173`）走 AImageReader → ANativeWindow → Android surface → swapchain →
  acquire → clear → present，**这正是会走 `vkAcquireNextImageKHR`/`vkQueuePresentKHR` 的路径**
  （现场崩溃就是在这两个调用上变成 `VK_ERROR_DEVICE_LOST`）。
* `--mode=tri`（`:634-…`）是最强的"驱动真的光栅化了"证据（中心像素红、角像素为清屏色）。

**判据（成功 = 全部满足）**：
1. **不再出现** `E/MESA: kbase: timeout on subqueue N`（`:840`）
   与 `F/MobileGL: Vulkan error VK_ERROR_DEVICE_LOST (-4)`；
2. 探针要求的所有 `VkResult` 全为 `VK_SUCCESS`，`--mode=tri` 的中心像素为红、
   角像素为清屏色；`--mode=win` 的 ImageReader 取回的像素正确；
3. 新加的 `kbase: running subqueue=…, cell->seqno=…, target_seqno=…` 里，
   **三个 subqueue 的 `cell->seqno` 都能在远小于 10 s 内达到 `target_seqno`**，
   且 `extract` **单调前进直到 `insert`**；
4. **真机游戏**（父级现场用的那条路径）连续运行 ≥ 5 分钟不出现 `-4`；
   期间 `I/MESA: kbase: tiler heap renewal (…)` 周期性出现是**正常**的；
5. 若上了 Fix 2，则 `I/MESA: kbase: created CSF group …` 应打印 **1.18 rung** 那条
   （`kbase_kmod.c:589-590`），且**没有** `1.18 CS_QUEUE_GROUP_CREATE failed` 回退行。

**反判据（说明没修好 / 修错了）**：
* 仍出现 `timeout on subqueue`，但**出现**了 `kbase: publish subqueue N via hardware doorbell only`
  且其后 `extract` 不动 ⇒ Fix 1 的方向对但不够，转 Fix 2 / Fix 2b；
* 出现 `kbase: CS error flag 0x… with exception 0x…` ⇒ 那是**真故障**，
  回到报告 13/15 的 `0xc3` 线（TRANSLATION_FAULT），与本案的"无故障静默卡住"不是一回事；
* 出现 `kbase: 1.18 CS_QUEUE_GROUP_CREATE failed: ENOTTY` ⇒ 本机 MTK kbase
  **不认 40 B 的 ioctl 58**，那么 `csi_handlers` 从来没送进内核，
  §3.3(c-2) 直接排除，回到 (a)。

---

## 7. 未验证清单（请父代理/后续报告补齐）

1. **真机日志里是否出现过** `kbase: created CSF group N with TILER_OOM CSI handler (1.18 layout, ioctl 58)`
   或 `… falling back to the 1.6 ABI`。报告 18 §2.2 说 v54 的 12:20 现场**三条 group-create 日志一条都没有**，
   于是推断"跑的不是 v54 二进制"。**因此"本机到底走的是 1.18 还是 1.6 rung"目前没有设备侧证据**
   —— 这直接决定 §3.3(c-2) 是否成立。**【最关键的一条未验证】**
2. `KBASE_IOCTL_CS_QUEUE_KICK`（ioctl 37）在"CSG 已 off-slot"时是否真能 arm 队列，
   以及 500 ms 重踢（`:822-830`）为什么没有补回丢掉的那次唤醒。**这条决定 Fix 1 是"必要"还是"充分"。**
3. `CS_EXTRACT` 在 `CALL` 执行期间的确切语义（是跳到 callee 还是停在 `CALL`）。
   §3.0 的第 3/4 点据此推断"CS 连 CALL 都没取"，请以 Arm CSF 文档或实测确认。
4. BO 缓存一致性：`cpu_gpu_coherent` 跳过 `MEM_SYNC`（`diff-kmod.txt` `@@ -1908,23 +1796,16 @@`）
   是否让 callee 流（`stream 0x5ffa2f8000/3048`）对固件不可见。**未验证。**
5. `cs_reg_count` 推导 vs 先例 A 的硬编码 96（`diff-kmod.txt` `@@ -445,14 +438,12 @@`）：
   若真机 GLB iface 的 `WORK_REGS` 不是 127，wrapper 用的 `reg = cs_reg_count-4`（`:566`）
   会落到不存在的寄存器上。**未验证**（且与 sq0 成功相矛盾，故优先级低）。
6. `queue->syncobjs`（`K/MESA: queue syncobj 0/1/2: seqno 5313/10555/5624`）在 10 秒里
   有没有继续增长。**这是区分 (a) 与 (c) 的第二个独立判据**：若 syncobj 在增长而 kbase
   seqno cell 不增长，说明 **callee 流在跑、是 wrapper 尾部卡住**；若 syncobj 也不动，
   说明**整个队列没被跑起来**（更支持 (a)）。现场只给了单点快照，**未验证**。
