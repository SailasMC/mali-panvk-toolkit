# 14 · tiler heap OOM 定案：为什么 grow 没发生，以及最小可行修法

> 对象：构建树 `/root/zenithblue/work/mesa`（= `build/android-v4` 的来源树），产物
> `dist/android-g720-v12-csf/libvulkan_panfrost.so`。
> 设备：OPPO PHZ110 / MT6989 / Immortalis-G720 MC12 / Android 16 / 无 root / kbase uAPI **1.21**。
> 本文所有行号都在**构建树**上逐条 `grep -n` / `sed -n` 核对过；内核侧引用的是 Arm kbase **r43p0**
> 与 Pixel `google-modules/gpu` 的**公开源码**（已下载留档，见 §9），凡未在设备上验证的一律标「未验证」。
> 本轮**未操作手机**、未改任何只读树；只在 `/root/research/tiler-work/` 新建了目录与文件。

---

## 0. TL;DR（五条）

1. **OOM 通知是「验尸报告」，不是「求救信号」。** kbase 在把这条通知送到用户态**之前**就已经
   `term_queue_group(group)` 把 CSG 杀掉了（r43p0 `mali_kbase_csf.c:2075-2092`）。Mesa 侧
   `kbase_kmod.c:655-658` 只打印一行，什么都做不了；而且这个 uAPI 里**根本没有**
   `KBASE_IOCTL_CS_TILER_HEAP_GROW`（48=INIT、49=TERM，**50 缺号**），grow 完全在内核里做。
2. **grow 之所以"没成功"，是因为堆被顶到了天花板且没有任何东西重置它**：panvk 用
   `initial_chunks=10`、`max_chunks=400`、`chunk_size=1 MiB`（`panvk_physical_device.c:1371-1373`）建堆，
   内核只在 `chunk_count < max_chunks` 时发新 chunk；Mesa 侧唯一的重置手段
   `kbase_renew_tiler_heap()`（`panvk_vX_gpu_queue.c:2192`）**是死代码**——它的触发条件要求
   `submit->tiler_work_estimate != 0`（`gpu_queue.c:2792`），而**该字段在全树没有任何写入点**
   （只有 `panvk_cmd_buffer.h:598-601` 的声明 + 复合字面量清零）。⇒ 堆单调涨到 400 MiB → `-ENOMEM` → 杀组。
3. **10 秒黑洞也对上了**：`OOM 11:25:37.423 → DEVICE_LOST 11:25:47.413` 正好 9.99 s，等于
   `KBASE_WAIT_TIMEOUT_NS`（`gpu_queue.c:63`，用于 :759 的 watchdog）。等待循环在 :937 拿到了
   notification 的 `error_type`（OOM = 4），却只塞进 `prev_error_type`（:970）**不作处理**，
   于是空转到 watchdog 才 `vk_queue_set_lost("kbase: timeout on subqueue")`。
4. **dma_heap 的 `O_RDONLY` 不是本案凶手**（与 `docs/09 §23.4/§23.2` 的口径一致，且本轮给出更强论据）：
   tiler heap chunk 是**内核**用 `kbase_mem_alloc()` 分配的，压根不经过 Mesa 的 dma_heap fd；
   而且若 `DMA_HEAP_IOCTL_ALLOC` 真失败，`kbase_kmod_bo_alloc_dmabuf()`（:1564-1580）会
   `mesa_loge` 并**直接返回 NULL（无回退）**→ 绝大多数 BO 分配都会失败 → `render/ahb/win` 不可能全绿。
5. **最小可行修法两处**：**(A)** 建 CSG 时走 **uAPI 1.18 布局**并置
   `csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG`（fork 的版本阶梯从 1.25 直接跳到 1.6，
   **漏了 1.18 这一级**；置位后内核会把 NULL chunk 交还固件、由 panvk **已经实现**的 TILER_OOM
   增量渲染 handler 接管，不再杀组）；**(B)** 接上 renew（最小 2 行：去掉 `tiler_work_estimate` 前置条件）。
   两处都给了 diff 草稿（§5），ABI 草案已用真头文件编译验证过（§6.0）。

---

## 1. 证据与口径

### 1.1 设备侧既有证据（本轮未新增）

| 时间 | 事件 | 出处 |
|---|---|---|
| 11:25:37.423 | `E/MESA: kbase: CSF group 0 tiler heap OOM notification` | 任务给出的真机 logcat |
| 11:25:47.413 | `F/MobileGL: Vulkan error VK_ERROR_DEVICE_LOST (-4)` | 同上 |
| 之前 | 主界面正常渲染约 10 s | 同上 |
| 对照 | clear / copy / AHB 导入 / 建交换链 / present 全正常 | `research/12` §2、`docs/09 §25` |

> 注意：`research/12 §25.6` 里 `tri` 模式那次崩溃是 **CSF exception `0xc3`（= TRANSLATION_FAULT_L3，
> 见 `src/panfrost/lib/kmod/mali_kbase_csf_registers.h:814`）**，logcat 里**没有** tiler heap OOM 行。
> ⇒ **`tri` 复现的是另一条病灶（绘制路径的地址翻译故障），不是本报告的 tiler 堆 OOM**。
> 本报告 §6 给出的 OOM 复现办法不改用 tri 的现成行为，而是靠"缩小天花板"或"加重几何"。

### 1.2 源码/产物指纹（本轮实测）

| 文件 | size | md5 |
|---|---|---|
| `src/panfrost/lib/kmod/kbase_kmod.c` | 72,941 | `e2e92db65be6b8f8fd87b9c7c8927c39` |
| `include/drm-uapi/mali_kbase_ioctl.h` | — | `1a32e9578ac1523d406b73f260f1c1a6` |
| `src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c` | — | `ac84fc4c2fb8477e8a2a329043fea3e7` |
| `src/panfrost/vulkan/panvk_physical_device.c` | — | `6d4ad3382653ed41d56e8c90407ba3a9` |
| `dist/.../libvulkan_panfrost.so`（**现行**，10-05 11:03） | 20,005,320 | `e08e07645c16d8ebaa11ca70a09884fd`（= §24 的 v50 WSI 补丁件） |
| `dist/.../libvulkan_panfrost.so.bak-1791169250`（**OOM 当轮件**，10-05 01:51） | 20,003,136 | `4417b369591fc2b3df27e22019ccf3a2` |

* 构建树 `git rev-parse HEAD` = `5a07217f034b3e50d8c7c7794f97a2df1742613b`（shallow clone）。
* **kbase 后端是未跟踪文件**（`git status` 里是 `??`）：`kbase_kmod.c/h`、`kbase_csf_uapi.h`、
  `kbase_uapi.h`、`mali_base_*_kernel.h`、`include/drm-uapi/mali_kbase_ioctl.h`。⇒ **回滚只能靠 `cp` 备份**，
  不能 `git checkout --`（与 `docs/09 §24` 的告诫一致）。
* 本轮结论对 `4417b369` 与 `e08e0764` **同时成立**：v50 补丁只动 WSI/u_gralloc 三个文件，不碰 tiler 路径。

### 1.3 内核侧参考实现（公开源码，已留档）

| 角色 | 来源 | 本地留档 |
|---|---|---|
| Arm kbase r43p0（与 uAPI 1.21 同代） | `nest-open-source…/mali-driver/+/0f8397ec…/bifrost/r43p0/…/csf/` | `/root/research/tiler-work/ref-r43p0-mali_kbase_csf.c`、`…_tiler_heap.c`、`…_defs.h` |
| Pixel kernel `google-modules/gpu`（更新一代） | `android.googlesource.com/kernel/google-modules/gpu/+/6fec92db…/mali_kbase/csf/mali_kbase_csf.c` | `ref-pixel-kbase_csf.c` |
| panthor 的 tiler OOM 增量渲染补丁（同一协议的设计依据） | dri-devel 2024-05 `[PATCH v2 1/4]` | URL 见 §9 |

> **未验证**：设备上 kbase 内核模块的真实 DDK 版本号（无 root、本轮禁操作手机）。依据
> `repo-verify/docs/02-kbase-bringup.md:54` 的实测 `uAPI 版本: 1.21` 推断为 r42–r44 一档。

---

## 2. 代码定位：创建 / 增长 / 通知 / 重置

### 2.1 初始堆大小由谁定

| 路径 | 代码 | 值 |
|---|---|---|
| **kbase（本机走这条）** | `panvk_physical_device.c:1371-1373` | `chunk_size = 1 MiB`，`initial_chunks = 10`，`max_chunks = 400` |
| DRM/panthor（本机不走） | `panvk_physical_device.c:1233-1235` | `2 MiB / 5 / 64` |

**全是硬编码常量，没有任何 `PANVK_*` 环境变量可调**。可用环境变量（`grep -rhoE '"PANVK_[A-Z0-9_]+"'`）只有：
`PANVK_KBASE_DMA_HEAP`、`PANVK_KBASE_DRI3`、`PANVK_KBASE_DVFS`、`PANVK_KBASE_HEAP_RENEW_INTERVAL`、
`PANVK_KBASE_HEAP_RENEW_WORK`、`PANVK_KBASE_IMPORT_CLEAR/SET`、`PANVK_KBASE_KCPU_SYNC`、
`PANVK_KBASE_USER_CACHE_SYNC`、`PANVK_CS/DESC_TRACEBUF_SIZE`、`PANVK_DEBUG`。**没有 chunk 尺寸/上下限开关**。

### 2.2 创建路径

```
panvk_vX_gpu_queue.c:2012 init_tiler()
  ├─ 2030  tiler_heap->chunk_size = phys_dev->csf.tiler.chunk_size      (=1 MiB)
  ├─ 2040-2046  oom_fbd（scratch FBD，OOM handler 用）
  ├─ 2053  tiler_heap_mem_group = 0                                    ← kbase 物理内存组
  ├─ 2057  tiler_heap_max_chunks = MAX2(phys_dev->csf.tiler.max_chunks, 200)  (=400)
  ├─ 2060-2065 kbase_kmod_csf_tiler_heap_create(dev, chunk_size,
  │              initial_chunks=10, max=400, target_in_flight=65535, group=0, ...)
  │      └─ kbase_kmod.c:1001-1030 kbase_kmod_csf_tiler_heap_create()
  │             └─ :1021 ioctl(KBASE_IOCTL_CS_TILER_HEAP_INIT)          （uapi 48）
  ├─ 2081-2090 panvk_priv_mem_write_desc(TILER_HEAP…)  base=first_chunk, top=first+1MiB
  └─ 2106-2116 mesa_logd("kbase: tiler heap desc 0x… base 0x… top 0x… geom 0x… oom_fbd 0x…")
```

* 内核侧（r43p0 `mali_kbase_csf_tiler_heap.c:675-830`）：**建堆时就一次性创建 10 个 chunk**，
  并把 `max_chunks` 记在堆结构里；`chunk_size` 被 `CHUNK_SIZE_MASK` 校验，**不会被强制成 2 MiB**。
* 内存组 0 + `KBASE_IOCTL_MEM_JIT_INIT(va_pages=1<<25, phys_pages=1<<25)`（`kbase_kmod.c:1186-1204`）：
  代码注释明确说"内核内部（tiler heap context/chunk）就从这个 zone 拿"。

### 2.3 增长（grow）路径 —— 只有内核侧

* **Mesa 侧没有 grow 调用点**：全树 `grep -rn 'TILER_HEAP_GROW\|heap_grow' src/` 无命中。
* 构建树用的 uapi 头里**也没有这个 ioctl**：
  `kbase_csf_uapi.h:386` = INIT(48)、`:399` = TERM(49)，**50 号缺失**（`include/drm-uapi/mali_kbase_ioctl.h:594,600` 同）。
* 增长点在内核（r43p0 `mali_kbase_csf.c:1896 handle_oom_event()`）：
  固件报 OOM → 内核读 `CS_HEAP_ADDRESS/VT_START/VT_END/FRAG_END` → 调
  `kbase_csf_tiler_heap_alloc_new_chunk()` → 把新 chunk 指针写回 **CS input page** 的
  `CS_TILER_HEAP_START/END` → ring doorbell。
* 什么时候**拒绝**增长（r43p0 `mali_kbase_csf_tiler_heap.c:903-940 validate_allocation_request`）：
  | 条件 | 返回 | 后果 |
  |---|---|---|
  | `nr_in_flight == 0` 或 `pending_frag_count > nr_in_flight` | `-EINVAL` | **杀组**（flag 也救不了） |
  | `nr_in_flight <= target_in_flight` 且 `chunk_count < max_chunks` | `0` | 正常发新 chunk |
  | 否则 `pending_frag_count > 0` | `-EBUSY` | 回 NULL chunk，固件等 fragment 收工 |
  | 否则（`chunk_count >= max_chunks` 且无 pending） | `-ENOMEM` | **杀组**（flag 置位时转为增量渲染） |
* 而 panvk 传的 `target_in_flight = 65535`（`:2063`）把第一档"渲染趟数在飞"闸门永久打开，
  于是只剩 `chunk_count >= max_chunks` 一条硬闸。

### 2.4 OOM notification 的处理逻辑

```
kbase_kmod.c:598 kbase_log_csf_notification()
  ├─ 621  p_atomic_set(&kbase_dev->csf_error, true)        ← 唯一的"状态"副作用
  ├─ 629-637  FATAL  → mesa_loge(...)
  ├─ 640-653  QUEUE_FATAL / TIMEOUT → mesa_loge(...)
  ├─ 655-658  BASE_GPU_QUEUE_GROUP_ERROR_TILER_HEAP_OOM
  │            → mesa_loge("kbase: CSF group %u tiler heap OOM notification")   ← 只有打印
  ├─ 660-670  QUEUE_ERROR_FAULT → kbase_log_csf_queue_error(...)
  └─ 673  返回 error_type(=error->error_type+1) 给调用者
```

* 消费点：`panvk_vX_gpu_queue.c:937 int error_type = kbase_kmod_csf_wait_event(...)` → **:970
  只赋给 `prev_error_type`，循环继续**；OOM(=4) 不作任何处理。
* `csf_error` 的消费点只有一处：`gpu_queue.c:3207`（`gpu_queue_check_status()`，且只在
  `PANVK_DEBUG(SYNC)` / force_sync 时被调用）⇒ 等待循环里**不看**这个 latch。
* 于是只有两条出路：CS 故障立即丢设备（`:794-813`，`tri` 走这条），或者 watchdog 到期
  `:849-853` → `vk_queue_set_lost("kbase: timeout on subqueue %u")`（**OOM 走这条，10 s**）。

### 2.5 重置（renew）路径 —— 死代码

```
gpu_queue.c:2160 kbase_try_destroy_retired_heap()      上一代堆 context 的延迟销毁
gpu_queue.c:2192 kbase_renew_tiler_heap()              destroy + 重新 create（回到 10 chunk）
gpu_queue.c:2760-2815 提交收尾：
   2782  if ((touched & graphics_mask) && submit->tiler_work_estimate) { … 计数 … }
   2792  if (submit->tiler_work_estimate &&
   2793      (queue->kbase_tiler_submit_count >= kbase_tiler_heap_renew_interval() ||
   2795       (renew_work && queue->kbase_tiler_work_count >= renew_work))) {
   2804     result = kbase_renew_tiler_heap(queue);          ← 唯一调用点
```

**`submit->tiler_work_estimate` 是 fork 新加的字段**（`panvk_cmd_buffer.h:598-601` 注释写着
"CPU-side estimate used by the kbase queue to choose a conservative tiler-heap renewal cadence"），
但：

```
$ grep -rn 'tiler_work' src/ | wc -l      → 10 行，全部是「读」或 counter 自增
  panvk_cmd_buffer.h:600            声明（唯一）
  panvk_vX_gpu_queue.c:2247         struct panvk_queue_submit 成员（唯一）
  panvk_vX_gpu_queue.c:2782,2784,2785,2786,2788,2792,2795   读
  panvk_queue.h:140                 counter
```
* `panvk_queue_submit_init()`（`:2281`）用复合字面量 `*submit = (struct panvk_queue_submit){…}` 整体初始化
  ⇒ 未列出的成员一律为 **0**。
* `panvk_queue_submit_init_cmdbufs()`（`:2521+`）遍历 cmdbuf 时**没有**把
  `cmdbuf->state.tiler_work_estimate` 汇总进去；`panvk_cmd_draw()`（`panvk_vX_cmd_draw.c:3457`）
  也**没有**往 `cmdbuf->state.tiler_work_estimate` 累积。

⇒ **`kbase_renew_tiler_heap()` 在出厂件里永不执行**（源码级确定：全树无写入点；未做运行期验证）。

---

## 3. OOM 的因果链（六步）

```
[1] 游戏连续绘制 → 每个"渲染趟"消耗 tiler 内存；内核侧 chunk_count 从 10 起单调上涨
[2] 没有任何重置：Mesa 的 renew 是死代码（§2.5）；内核只在
    (a) 内存压力 shrinker（kbase_csf_tiler_heap_scan_kctx_unused_pages，r43p0:1309）
    (b) 堆销毁 delete_all_chunks（r43p0:441-459）
    才会把 chunk 还回去 ⇒ 正常游戏里堆只涨不落（fork 自己在 gpu_queue.c:72-78 就是这么写的）
[3] chunk_count 触顶 max_chunks=400（≈400 MiB，1 MiB×400），且此刻 pending_frag_count==0
[4] r43p0 validate_allocation_request() 返回 -ENOMEM
[5] r43p0 kbase_queue_oom_event():
      err = handle_oom_event(...)                 ← 非 0 且非 -EBUSY
      if (err) { dev_warn("Queue group to be terminated…");
                 term_queue_group(group);          ← ★组在这里就死了
                 report_tiler_oom_error(group); }  ← 然后才发通知
[6] Mesa 收到通知（§2.4）只打印；等待循环空转 10 s（KBASE_WAIT_TIMEOUT_NS）→
    vk_queue_set_lost → VK_ERROR_DEVICE_LOST → MobileGL Fatal
```

时间线核对：`11:25:37.423`(通知) → `11:25:47.413`(-4) = **9.99 s ≈ 10 s watchdog**。
`target_in_flight=65535` 使 [3] 的 `pending_frag_count==0` 分支成为唯一出口：
一旦无在飞 fragment 就是 `-ENOMEM` 硬失败，而不是 `-EBUSY` 软等待。

### 3.1 为什么"增大上限"能缓解但不能根治

即使把 `max_chunks` 抬到 1024（1 GiB），堆仍然只涨不落，只是把爆点推后；而且 1 GiB 常驻 tiler 内存
在有 Android 全局 OOM killer 的机器上很危险（`gpu_queue.c:2054-2056` 的注释正是这个顾虑）。
必须"重置"或"优雅降级"二选一，见 §5 A/B。

---

## 4. "为什么 grow 没成功/没发生"——逐条排除

| 假设 | 判定 | 依据 |
|---|---|---|
| **H1 dma_heap 0444 + Mesa `O_RDONLY` ⇒ ALLOC 失败 ⇒ grow 失败** | **排除** | ①tiler chunk 由**内核** `kbase_mem_alloc()` 分配，Mesa 的 dma_heap fd 与此路径**完全无关**；②`kbase_kmod.c:1285` 确实用 `O_RDONLY`，但它只影响 `kbase_kmod_bo_alloc_dmabuf()`（:1564-1580）：该函数 ALLOC 失败就 `mesa_loge`+返回 NULL，而 `kbase_kmod_bo_alloc()`（:1588-1592）对绝大多数非 exec/alloc-on-fault BO **直接走这条路、没有回退** ⇒ 若 ALLOC 真失败，`render`/`ahb`/`win` 一个都不可能绿（`research/12` 实测全绿）⇒ ALLOC 实际成功（`docs/09 §23.2` 的 v49 反向对照也印证：关掉 dma-heap，`-4` 照旧）。 |
| **H2 400 MiB 天花板被真实几何打满，且没有重置** | **主因（与全部证据自洽）** | §3；fork 自己的注释 `gpu_queue.c:72-78`；r43p0 `validate_allocation_request` 的 `-ENOMEM` 分支 |
| **H3 物理 chunk 分配失败** | **可能是次因**（r43p0 有 upstream 缺陷） | r43p0 `tiler_heap.c:986-991` 在 `alloc_new_chunk()` 返回 NULL 时 `goto prelink_failure`，而 `err` 此时已被上层成功校验**置 0**，`:1057 prelink_failure: return err;` ⇒ **返回 0 且 `*new_chunk_ptr` 未初始化**。内核拿这个未初始化值写 `CS_TILER_HEAP_START/END` ⇒ 固件按垃圾指针走 chunk 链 ⇒ **翻译故障**（`0xc3`）。这条恰好能解释 `tri` 在**同一个** sideband `0x5fffe1e000` 上三个 CSG 同时 fatal（`research/12 §25.6`）——**未验证**，需 dmesg 里是否有 `Could not allocate chunk of size …`（`dev_err` 只进 dmesg，不进 logcat）。**注意：H3 若成立，内核连通知都不会发**（err==0），与本案 logcat 已经出现 OOM 通知**不符** ⇒ 本案更可能是 H2。 |
| **H4 固件统计非法（`nr_in_flight==0`）⇒ -EINVAL** | 排除（就本案而言） | r43p0 `handle_oom_event` 会先打印 `Invalid Heap statistics provided by firmware: vt_start…`；任务给的 logcat 里没有这一行。且该路径**置 flag 也救不了**。 |
| **H5 建堆时 initial=10 太小导致首帧就 OOM** | 排除 | 真机正常渲染了约 10 s 才 OOM。 |

**结论**：直接原因是 **H2**——kbase 只能"涨到 400 MiB"且 Mesa 侧唯一的重置路径是死代码，
顶到天花板后内核 `-ENOMEM` 直接杀组；通知只是事后通告。H3 是同一个子系统的**另一条**潜在缺陷
（且很可能是 `tri` 的 `0xc3` 的成因），工程上要用"少触碰这条内核路径"（少走 grow）来规避。

---

## 5. 可实施修法（按成本排序，全部只给草稿，**未改构建树**）

> 回滚统一方式：`kbase_kmod.c` 等是**未跟踪文件**，先
> `cp <file> /root/research/tiler-work/backup-<name>.$(date +%s)`，回滚即 `cp` 回来。

### Fix A（首选，结构正解，~20 行）：让内核把 tiler OOM 交给应用 handler（增量渲染）

**文件**：`src/panfrost/lib/kmod/kbase_kmod.c`，函数 `kbase_kmod_csf_group_create()`，插入点 **:523 之前**（1.25 分支之后、1.6 回退之前）。

**依据**：r43p0 `mali_kbase_csf.c:1926-1937`：

```c
	if ((group->csi_handlers & BASE_CSF_TILER_OOM_EXCEPTION_FLAG) &&
	    (pending_frag_count == 0) && (err == -ENOMEM || err == -EBUSY)) {
		/* The group allows incremental rendering, trigger it */
		new_chunk_ptr = 0;      /* ← 交 NULL chunk，固件去调 app 的 TILER_OOM 异常处理器 */
	} else if (err == -EBUSY) { new_chunk_ptr = 0; }
	else if (err) return err;   /* ← 没有 flag 就是这里：杀组 */
```

而 panvk **已经把那个 handler 实现并注册好了**（上游代码，fork 只改了一个函数名）：
`panvk_vX_cmd_draw.c:4500-4531`（每趟渲染 `cs_set_exception_handler(MALI_CS_EXCEPTION_TYPE_TILER_OOM,…)`
+ 收工后注销）、`panvk_vX_exception_handler.c:176-365`（handler 本体 = 增量渲染）、
`panvk_vX_device.c:625`（`init_tiler_oom()` 无条件初始化 handler BO）。
⇒ **只要把 flag 传进内核，增量渲染这条既有的救命通道就会接通。**

**为什么 fork 的注释说"flag 试过、无效"**（`kbase_kmod.c:517-521`）：它跳档了。版本阶梯只有
`>=1.25`（112 B 布局）与 `1.6`（32 B，**没有 csi_handlers 字段**）两档，本机 uAPI=1.21 只能落到 1.6 档
⇒ flag 从未真正送达内核。**1.18 这一档（40 B、带 `csi_handlers`）是缺的**（`include/drm-uapi/mali_kbase_ioctl.h:429-455`）。
本轮已用真头文件编译验证：40 B、`_IOWR(0x80,58,40)=0xc028803a`、`offsetof(in.csi_handlers)=29`、
flag=1（见 §6.0）。

**diff 草稿**：

```diff
--- a/src/panfrost/lib/kmod/kbase_kmod.c
+++ b/src/panfrost/lib/kmod/kbase_kmod.c
@@
    if (pan_kmod_driver_version_at_least(&dev->driver, 1, 25)) {
       union kbase_ioctl_cs_queue_group_create req = {
          .in = {
             ...
             .cs_fault_report_enable = 1,
+            /* Arm kbase only defers a tiler-heap OOM to the application's
+             * CSI exception handler when this flag was passed at group
+             * creation; otherwise the kernel terminates the group
+             * (mali_kbase_csf.c:handle_oom_event()).  panvk installs that
+             * handler in the CS stream, so ask for the deferral. */
+            .csi_handlers = getenv("PANVK_KBASE_TILER_OOM_HANDLER") &&
+                                    !strcmp(getenv("PANVK_KBASE_TILER_OOM_HANDLER"), "0")
+                               ? 0 : BASE_CSF_TILER_OOM_EXCEPTION_FLAG,
          },
       };
       ...
    }
+
+   /* uAPI 1.18..1.24: 40-byte group-create layout that already carries
+    * csi_handlers.  This rung was missing, so a uAPI 1.21 kernel (our
+    * device) always fell through to the 1.6 ABI, which has no such field,
+    * and every tiler-heap OOM killed the group. */
+   if (pan_kmod_driver_version_at_least(&dev->driver, 1, 18)) {
+      union kbase_ioctl_cs_queue_group_create_1_18 req = {
+         .in = {
+            .tiler_mask = 1,
+            .fragment_mask = ~0ull,
+            .compute_mask = ~0ull,
+            .cs_min = cs_queue_count,
+            .priority = 0,
+            .tiler_max = 1,
+            .fragment_max = 64,
+            .compute_max = 64,
+            .csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG,
+         },
+      };
+
+      if (ioctl(dev->fd, KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_18, &req) == 0) {
+         *group_handle = req.out.group_handle;
+         mesa_logi("kbase: created CSF group %u (uAPI 1.18 ABI, "
+                   "csi_handlers 0x%x)", *group_handle, req.in.csi_handlers);
+         return 0;
+      }
+
+      mesa_logw("kbase: CS_QUEUE_GROUP_CREATE_1_18 failed: %s; "
+                "falling back to the 1.6 ABI", strerror(errno));
+   }
```

* 附加开关（用于 A/B）：`PANVK_KBASE_TILER_OOM_HANDLER=0` 强制不带 flag（对照组）。
* 风险/前提：固件必须支持"NULL chunk ⇒ 触发 app 的 TILER_OOM handler"。panvk 的 handler 已存在，
  但**从未在这台设备上被激活过** ⇒ 首次上机必须留 `=0` 对照组（§6.3）。
* 回滚：`cp` 备份回去。

### Fix B（同样必修，消除 OOM 本身）：把 renew 接上

**B1（最小 2 行，无单位标定问题，推荐先上）**：`panvk_vX_gpu_queue.c:2792-2795`

```diff
-   if (submit->tiler_work_estimate &&
-       (queue->kbase_tiler_submit_count >=
-           kbase_tiler_heap_renew_interval() ||
-        (renew_work && queue->kbase_tiler_work_count >= renew_work))) {
+   /* NOTE: submit->tiler_work_estimate has no producer anywhere in this
+    * tree (panvk_cmd_buffer.h declares it, nothing writes it), so the old
+    * guard made this block unreachable: the heap could only grow, hit
+    * max_chunks and have kbase terminate the group on -ENOMEM.  Renew on
+    * the submit interval; keep the work threshold for when a producer
+    * exists. */
+   if (queue->kbase_tiler_submit_count >= kbase_tiler_heap_renew_interval() ||
+       (submit->tiler_work_estimate && renew_work &&
+        queue->kbase_tiler_work_count >= renew_work)) {
```

* 默认节奏：每 **128 次**含 tiler 工作的提交（`gpu_queue.c:79`）重置一次堆；`PANVK_KBASE_HEAP_RENEW_INTERVAL=0`
  表示关闭（映射到 `UINT32_MAX`，`gpu_queue.c:86-97`）；`PANVK_KBASE_HEAP_RENEW_WORK` 仍受 estimate 门控。
* 代价：每次 renew 要 `kbase_wait_graphics_targets()` 排空图形队列 + 建/销堆 context（见 `:2789-2807`）。

**B2（正统，需标定单位）**：
1. `panvk_vX_cmd_draw.c:3457 panvk_cmd_draw()` 开头加：

```c
#ifdef HAVE_PAN_KMOD_KBASE
   /* CPU-side tiler work estimate for the kbase heap-renewal cadence.
    * Unit is arbitrary: compare against PANVK_KBASE_HEAP_RENEW_WORK. */
   if (draw.indirect.buffer_dev_addr)
      cmdbuf->state.tiler_work_estimate += (uint64_t)draw.indirect.draw_count * 1024;
   else
      cmdbuf->state.tiler_work_estimate +=
         (uint64_t)MAX2(draw.vertex.count, draw.index.count) *
         MAX2((uint64_t)draw.instance.count, 1u);
#endif
```
2. `panvk_vX_gpu_queue.c:2521 panvk_queue_submit_init_cmdbufs()` 的 cmdbuf 循环里加：
```c
#ifdef HAVE_PAN_KMOD_KBASE
      submit->tiler_work_estimate += cmdbuf->state.tiler_work_estimate;
#endif
```
* 语义提醒：`PANVK_KBASE_HEAP_RENEW_WORK` 默认 65536，按上面"1 单位 = 1 顶点"的标定，
  相当于每 64 k 顶点重置一次堆——对现代游戏**过于频繁**，建议先只上 B1，B2 之后用
  `PANVK_KBASE_HEAP_RENEW_WORK` 在设备上标定（例如 2 M–16 M）。

### Fix C（调参 + 让天花板可二分，~10 行）

**文件**：`src/panfrost/vulkan/panvk_physical_device.c:1371-1373`（kbase 分支）

```diff
-      device->csf.tiler.chunk_size = 1024 * 1024;
-      device->csf.tiler.initial_chunks = 10;
-      device->csf.tiler.max_chunks = 400;
+      device->csf.tiler.chunk_size = 1024 * 1024;
+      /* Env-tunable so the object can be bisected on-device without a
+       * rebuild (PANVK_KBASE_TILER_INITIAL_CHUNKS=1 / MAX=2 reproduces the
+       * kbase tiler-heap OOM deterministically in seconds). */
+      device->csf.tiler.initial_chunks =
+         (unsigned)MAX2(debug_get_num_option("PANVK_KBASE_TILER_INITIAL_CHUNKS", 32), 1);
+      device->csf.tiler.max_chunks =
+         (unsigned)MAX2(debug_get_num_option("PANVK_KBASE_TILER_MAX_CHUNKS", 512),
+                        device->csf.tiler.initial_chunks);
```
* 32 MiB 起步 / 512 MiB 上限是**保守建议值**（配合 Fix B 的定期重置才安全；单独抬上限反而危险）。
* 需要 `#include "util/debug.h"`（该文件应已间接包含；编译前确认）。

### Fix D（体验 + 可观测，~10 行）

1. **10 s → 即时**：`panvk_vX_gpu_queue.c:937` 拿到 `error_type` 后立刻判死：

```c
         int error_type = kbase_kmod_csf_wait_event(dev->kmod.dev, remaining);

         /* 4 == BASE_GPU_QUEUE_GROUP_ERROR_TILER_HEAP_OOM + 1 (kbase_kmod.c:627).
          * The kernel has already terminated the group by the time this
          * notification is readable, so there is nothing to wait for. */
         if (error_type == 4)
            return vk_queue_set_lost(&queue->vk,
                                     "kbase: tiler heap OOM (group terminated)");
```
   （或者更通用：把 `kbase_kmod_csf_has_error()` 加进循环判断。）
2. **把 OOM 行变成可诊断行**：`kbase_kmod.c:655-658` 里补一句修法提示与 group handle；
   并把建堆参数从 `mesa_logd` 提升为 `mesa_logi`（`gpu_queue.c:2106`），确保不带 `MESA_DEBUG` 也能看到
   `chunk_size/initial/max` 与后继的每次 renew。

### 不可修（内核侧，必须在报告里如实标注）

* r43p0 `tiler_heap.c:986-991/1057` 的"分配失败返回 0 + 未初始化 chunk 指针"缺陷（H3）。
* `-EINVAL`（固件统计非法）必杀，与 flag 无关（H4）。
* 用户态没有 grow ioctl（uapi 50 缺号），无法"收到通知后再补一刀"。

---

## 6. 最小验证方案

### 6.0 已在服务器上完成的一步（ABI 验证，不动设备）

`/root/research/tiler-work/kbase_group_create_flag_check.c`（本轮新建，md5 `c377e52f59e084126b17b8c6913be457`）
直接 include 构建树真头文件，编译运行（`gcc -O0 -Wall -I/root/zenithblue/work/mesa/include`）：

```
sizeof(create_1_6)     = 32
sizeof(create_1_18)    = 40
sizeof(create_current) = 112
KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_18 = 0xc028803a
KBASE_IOCTL_CS_QUEUE_GROUP_CREATE      = 0xc070803a
offsetof(1_18, in.csi_handlers)        = 29
BASE_CSF_TILER_OOM_EXCEPTION_FLAG      = 0x1
req18.in.csi_handlers                  = 1
OK
```
⇒ Fix A 用的结构体/常量/ioctl 号在真头文件下**可编译、尺寸正确**（1.18 = 40 B，与 docs 里
"uAPI ≥1.18" 的注释一致）。**注意**：1.21 内核只认 40 B 那个 cmd；发 112 B 的 current 布局
（`0xc070803a`）会因 cmd 里带尺寸而被内核判为未知 ioctl。

### 6.1 复现 tiler 堆 OOM（**不要用 tri 当证据**）

`tri` 现在崩的是 `0xc3` 地址翻译故障（`research/12 §25.6`），**不是**本 OOM。

* **路 1（推荐，秒级、确定）：** 上 Fix C 后
  `PANVK_KBASE_TILER_INITIAL_CHUNKS=1 PANVK_KBASE_TILER_MAX_CHUNKS=2`，
  用**任意会画东西的模式**（`render` 里已经含 draw；或 `tri` 只要能过第一次 draw）画几帧
  ⇒ 必现 `E/MESA: kbase: CSF group N tiler heap OOM notification`。
  这一步同时是 Fix A/B 的对照基线。
* **路 2（不改天花板，加重几何）：** 在 `/root/research/probe10/panvk_wsi_probe.c` 里加一个模式
  （在 :1383 的 `want_*` 分派旁加 `want_heap`，实现放 `mode_render()` 同文件）：
  把 `:781 c->d.vkCmdDraw(cmd, 3, 1, 0, 0)` 换成一次大几何 + N 次重复提交，例如
  `vkCmdDraw(cmd, 0, 1, 0, 0)` 配 `vkCmdDrawIndexed(cmd, 300000, 1, 0, 0, 0)`，
  每帧后再 `vkQueueWaitIdle`，循环 60 帧；渲染目标 64×64 保持不动，只堆几何。
  （草案文本见 `/root/research/tiler-work/probe-heap-mode-draft.c.txt`，本轮只写草稿、**未编译进探针**。）
* ⚠️ **若 `tri` 的 `0xc3` 无法越过（第一次 draw 就崩），则整条探针路线都到不了 OOM**：
  此时唯一可行的小代价复现是「**真机游戏**（已被证明能连续绘制 ~10 s 不触发 `0xc3`）+
  Fix C 的 env 把天花板压到 2 chunk」——见 §6.3 判据表。探针侧要先解决 `0xc3`（那是
  `docs/09 §25.6/§25.7` 已经立项的另一条线），不应把两件事混在一起。
* 跑法沿用既有：`run-device.sh`（设备侧 `/data/local/tmp/`，**一次一模式**；`all` 会被首个崩的模式污染，
  `research/12 §1` 已给告诫）。

### 6.2 建议添加的打印（配合上面两条路）

| 位置 | 加什么 | 目的 |
|---|---|---|
| `gpu_queue.c:2106` | `mesa_logd → mesa_logi`，并补 `chunk_size / initial / max / mem_group` | 拿到"初始堆 10 MiB / 上限 400 MiB"的**运行期证据** |
| `gpu_queue.c:2192 kbase_renew_tiler_heap()` | 进入时 `mesa_logi("kbase: renewing tiler heap (submit %u, work %" PRIu64 ")")` | 直接判定 renew 是否在跑（Fix B 的判据） |
| `gpu_queue.c:937` | 拿到 `error_type` 后 `mesa_loge("kbase: CSF notification error_type=%d", error_type)` | 把 notification 与 `-4` 的时间戳钉在一起 |
| `kbase_kmod.c:655` | 补 `"… (group already terminated by the kernel)"` | 避免再次误读成"求救信号" |

对比 `tri` 的 `0xc3`：把 `kbase: tiler heap desc 0x… base 0x… geom 0x…`（:2106）与本轮
sideband `0x5fffe1e000` 对照，即可判定 `tri` 的故障地址是否落在 tiler 堆/几何缓冲 VA 上（**未验证**）。

### 6.3 A/B 判据表（每次只改一个自变量）

| 配置 | 期望 logcat | 结论 |
|---|---|---|
| 出厂件 + 触发 OOM | `tiler heap OOM notification` → 9.99 s → `-4` | 基线（已复现于真实游戏） |
| Fix A `PANVK_KBASE_TILER_OOM_HANDLER=0` | 同上 | 对照组 |
| Fix A 默认（带 flag） | **无** `OOM notification`；可能出现增量渲染的多趟 fragment 性能下降 | flag 送达内核且固件走了 handler |
| Fix B1（renew 生效） | 出现 `kbase: renewing tiler heap`，且 OOM 不再出现（若仍出现，说明 128 趟内的几何量就超过上限 ⇒ 需要调小 interval 或同时上 Fix C） | 天花板不再被顶到 |
| Fix C（initial=1,max=2） | **秒级**复现 OOM（用于验证 A/B 是否真的挡住了） | 复现装置 |

### 6.4 编译（供后续在有构建树流程时使用；本轮**未执行**）

```bash
# 1) 备份要改的未跟踪文件（不能 git checkout）
cd /root/zenithblue/work/mesa
mkdir -p /root/research/tiler-work/backup-$(date +%s)
cp src/panfrost/lib/kmod/kbase_kmod.c \
   src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c \
   src/panfrost/vulkan/panvk_physical_device.c \
   src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c \
   /root/research/tiler-work/backup-$(date +%s)/

# 2) 增量编译（沿用既有 android-v4 构建目录，见 repo-verify/docs/04）
ninja -C /root/zenithblue/build/android-v4 src/panfrost/vulkan/libvulkan_panfrost.so
# 3) 落 dist + 记 md5（run-device.sh 会校验 md5）
cp /root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so \
   /root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so
md5sum /root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so
```
（探针如需重编：`cd /root/research/probe10 && bash build.sh`，NDK r27c `/opt/android-ndk-r27c`，
`research/12 §1` 记录了带 `-DHAVE_TRIANGLE` 的既有做法。）

### 6.5 已知陷阱（勿踩）

* **`--mode=all` 不可用**：`tri` 的 CSF 故障会污染同进程后续步骤（`research/12 §1`）。
* `tri` 的 `-4` **不能**当作本 OOM 的证据（§6.1）。
* `PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent`（v49 的反向对照）与本案**无关**，不要重复消耗实验轮次。
* 一次只改一个自变量；`PANVK_KBASE_HEAP_RENEW_INTERVAL` 在 **Fix B 之前是死开关**（不会改变任何行为），
  这本身可以作为"renew 是否接通"的判据。

---

## 7. 上游 / 已知修复检索结果

| 项 | 结论 |
|---|---|
| panthor（上游 DRM 驱动）的同类问题 | 有正式修复序列：`[PATCH v2 1/4] drm/panthor: Fix tiler OOM handling to allow incremental rendering`（见 §9 URL）。语义与本报告 Fix A 完全一致：内核 grow 失败时**不能**直接判死，要把 NULL chunk 交回固件，让**用户态驱动**的 tiler OOM handler 做增量渲染。 |
| Arm kbase 的机制 | r43p0 `handle_oom_event()`（`csi_handlers & BASE_CSF_TILER_OOM_EXCEPTION_FLAG` 分支）+ `validate_allocation_request()`；`BASE_CSF_TILER_OOM_EXCEPTION_FLAG` 的定义与 `csi_handlers` 字段在 1.18 布局引入。 |
| Mesa 上游 | **不存在 kbase 后端**（上游 panvk 只走 panthor/DRM render node）。本机构建树里 `kbase_kmod.c` 等 8 个文件都是**未跟踪新文件**（`git status` 证据）⇒ 这是 fork 私有缺陷，**没有可 cherry-pick 的上游 MR**。 |
| 可借用的上游思路 | ①"内核放弃 grow 时交 NULL chunk"（panthor 补丁 / kbase 的 flag 分支）；②"用户态 handler 做增量渲染"（panvk 上游 `generate_tiler_oom_handler`，本机已有、未被启用）。 |

---

## 8. 未验证项 / 风险（如实）

1. **内核 DDK 版本**未知（无 root），r42–r44 同代为推断；r43p0 的行号在 MediaTek 件上可能有偏移。
2. **H3（r43p0 `err==0` 缺陷）**是否存在于本机内核未验证；若是 `tri` `0xc3` 的成因，则需要 dmesg
   （`Could not allocate chunk of size …`）才能定案。
3. **Fix A 的固件前提**：固件必须把内核回的 NULL chunk 解释为"触发 app TILER_OOM handler"。
   panvk 的 handler 与注册代码都在，但**从未在真机被激活过**；fork 的旧注释声称"试过无效"，
   本报告给出的是更可能的解释（版本阶梯跳档导致 flag 从未送达）——**仍需一次 A/B 上机确认**。
4. **Fix B1 的节奏风险**：默认 128 次提交/次重置，在某些游戏里可能依然不够（每次重置都有
   图形队列排空开销）；先用 `PANVK_KBASE_HEAP_RENEW_INTERVAL` 扫 8/32/128 找性价比。
5. `target_in_flight=65535` 是否应该改小（让内核更多地走 `-EBUSY` 软等待 + `REQ` 重发）
   **未评估**：kbase 的 chunk 只在内存压力下回收，软等待不会释放 chunk，故本轮不建议动它。
6. `kbase_kmod_csf_wait_cqs64()`（KCPU/fence 路径）可能让部分等待**绕过** `csf_wait_event()` 的
   notification 消费，从而影响"OOM 是否一定在 10 s 内被看到"；未逐一追踪（`gpu_queue.c:920-940`）。

---

## 9. 引用清单

**本机（构建树，只读）**
- `/root/zenithblue/work/mesa/src/panfrost/lib/kmod/kbase_kmod.c`
  :517-521（fork 关于"内核 OOM 路径/flag 无效"的注释）、:523（`>=1.25` 门）、:536（`cs_fault_report_enable`）、
  :569（1.6 回退）、:598-676（notification 处理，OOM 在 :655-658）、:621（`csf_error`）、
  :777-822（`csf_wait_event`）、:993-998（`csf_has_error`）、:1001-1030（HEAP_INIT）、:1033-1043（HEAP_TERM）、
  :1186-1204（JIT_INIT 注释：tiler heap 从这里取内存）、:1285（`open(dma_heap, O_RDONLY)`）、
  :1564-1580（dma-heap BO 分配，无回退）、:1581-1592（BO 分配分派）、:1612（growable region）
- `/root/zenithblue/work/mesa/include/drm-uapi/mali_kbase_ioctl.h`
  :429-431（`BASE_CSF_TILER_OOM_EXCEPTION_FLAG`）、:433-455（1.18 = 40 B，ioctl 58）、
  :458-484（current = 112 B）、:470（`cs_fault_report_enable`）
- `/root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c`
  :63（10 s watchdog）、:72-106（renew 常量与 env）、:759（deadline/watchdog）、:794-813（CS fault 立即 lost）、
  :920-970（notification 消费但不处理）、:2012-2090（`init_tiler`）、:2106-2116（tiler heap desc 打印）、
  :2160-2235（retired/renew）、:2760-2815（renew 触发条件，:2804 唯一调用）
- `/root/zenithblue/work/mesa/src/panfrost/vulkan/panvk_physical_device.c` :1233-1235、:1371-1373
- `/root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_cmd_buffer.h` :598-601
- `/root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c` :3457（`panvk_cmd_draw`）、:4500-4531（handler 注册/注销）
- `/root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_exception_handler.c` :176-365（handler 生成）、:367（`init_tiler_oom`）
- `/root/zenithblue/work/mesa/src/panfrost/vulkan/panvk_vX_device.c` :625（`init_tiler_oom` 调用）
- `/root/zenithblue/work/mesa/src/panfrost/lib/kmod/mali_kbase_csf_registers.h` :795-841（异常码表；`:814` `0xC3 = TRANSLATION_FAULT_L3`）
- `research/12-probe-run-results.md` §1、§3.2、§25（探针 8 模式总表；`tri` 的 `0xc3`）
- `repoR4/docs/09-mobilegl-integration.md` §23.2/§23.3/§23.4/§24/§25（dma_heap 口径更正 + `-4` 定位）
- `repo-verify/docs/02-kbase-bringup.md` §3（uAPI 1.21、`/dev/mali0` 0666、GET_GPUPROPS 等实测）

**内核侧公开源码（本轮下载，留档在 `/root/research/tiler-work/`）**
- Arm kbase r43p0 `csf/mali_kbase_csf_tiler_heap.c`
  `validate_allocation_request()`（903-940）、`alloc_new_chunk()`（265-300）、`create_chunk()`（394-420）、
  `kbase_csf_tiler_heap_alloc_new_chunk()`（940-1058，**:986-991 + :1057 的 err==0 缺陷**）、
  `kbase_csf_tiler_heap_init()`（675-830）、`delete_all_chunks()`（441-459）、
  `kbase_csf_tiler_heap_scan_kctx_unused_pages()`（1309，回收只在内存压力下发生）
  <https://nest-open-source.googlesource.com/manifest_repos/mali-driver/+/0f8397eced2de6bc649a9cc32d0fae77a1dc34dc/bifrost/r43p0/kernel/drivers/gpu/arm/midgard/csf/mali_kbase_csf_tiler_heap.c>
- Arm kbase r43p0 `csf/mali_kbase_csf.c`
  `handle_oom_event()`（1896-1952，**:1926 flag 分支**）、`report_tiler_oom_error()`（1958-1975）、
  `kbase_queue_oom_event()`（2021-2093，**:2090 杀组 → :2091 发通知**）、
  `create_queue_group()`（1204-1240，**:1234 `csi_handlers`**）、flags 校验（:1332-1335）
  <https://nest-open-source.googlesource.com/manifest_repos/mali-driver/+/0f8397eced2de6bc649a9cc32d0fae77a1dc34dc/bifrost/r43p0/kernel/drivers/gpu/arm/midgard/csf/mali_kbase_csf.c>
- Pixel kernel（更新一代，同形逻辑）`mali_kbase/csf/mali_kbase_csf.c`
  `handle_oom_event`（1941-1994）、`report_tiler_oom_error`（2001-2016）、`kbase_queue_oom_event`（2021-2095）
  <https://android.googlesource.com/kernel/google-modules/gpu/+/6fec92db20c3563da85bc99bde020a18b49f71b6/mali_kbase/csf/mali_kbase_csf.c>
- dri-devel `[PATCH v2 1/4] drm/panthor: Fix tiler OOM handling to allow incremental rendering`
  <https://lists.freedesktop.org/archives/dri-devel/2024-May/452146.html>

**本轮新建（仅 `/root/research/`）**
- `/root/research/tiler-work/kbase_group_create_flag_check.c`（+ `flagcheck` 可执行）：Fix A 的 ABI 验证
- `/root/research/tiler-work/ref-r43p0-*.c/h`、`ref-pixel-kbase_csf.c`：内核参考源码留档
- `/root/research/tiler-work/probe-heap-mode-draft.c.txt`：§6.1 路 2 的探针模式草稿
- 本报告 `/root/research/14-tiler-heap-oom.md`
