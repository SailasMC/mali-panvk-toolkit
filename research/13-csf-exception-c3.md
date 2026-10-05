# 13 号报告 — CSF `status 0x7dc002c3 / exception 0xc3` 定位报告

面向：自编 Mesa **panvk + kbase(CSF)** 后端在 OPPO PHZ110（MT6989 / Immortalis-G720 MC12 / Android 16）上
「clear/copy/AHB/交换链全通，一光栅化就掉 GPU」这一唯一拦路虎。

- 设备现场/驱动 md5/探针二进制的既有事实沿用 `12-probe-run-results.md`，本报告不重复。
- **本报告只做了只读操作**：`sed/grep/git show/git diff` + 从公网抓取 ARM kbase 参考源码（落在 `/root/research/csf-work/pub-kbase/`）。
  未改 `/root/mesa`、`/root/zenithblue`（含 `work/mesa`、`build/`）、`/root/MobileGL`，未编译，未碰手机。
- 标注约定：**【已定论】**=有源码/寄存器定义直接支撑；**【推断】**=由已定论事实演绎，需一步实验确认；**【未验证】**=只提出假设。

---

## 0. 一页结论（TL;DR）

1. **`0x7dc002c3` 是 GPU MMU 的 `AS_FAULTSTATUS` 寄存器原值**（不是 CS 指令异常），逐位解码（全部有出处）：
   | 位域 | 值 | 含义 |
   |---|---|---|
   | bits[7:0] EXCEPTION_TYPE | `0xC3` | **TRANSLATION_FAULT_3**（三级页表走表失败：L0→L1→L2 描述符有效，**最后一级 PTE 无效**） |
   | bits[9:8] ACCESS_TYPE | `0x2` | **READ**（不是写、不是取指、不是 atomic） |
   | bits[31:16] SOURCE_ID | `0x7DC0` | core_id=62 → **“csf”**（Command Stream Frontend，即 CSF 固件本体）；internal_requester=12 → **“lsu”**（CSF 的 load/store unit） |
2. ⇒ **是 CSF 固件自己的 LSU 去读了一个未映射的 GPU VA**；`sideband 0x0000005fffe1e000` **就是那个故障地址**
   （kbase 明确把 `fault->addr` 填进 sideband）。**不是** shader core 读贴图/属性（core_id 0..31），**也不是** tiler 单元（core_id 51）。
3. **“3 个 group 同时 fatal”不是 3 次故障**：kbase 对一个 kctx 的 MMU 故障会把**同一份 payload 复制给该 kctx 所有在位的 CSG**
   （`kbase_csf_ctx_report_page_fault_for_active_groups()`，公开 kbase `csf/mali_kbase_csf.c:1687-1713`）。所以全过程只有**一次**错误访问。
4. **触发面【已定论】**：`tri` 相比 `render/ahb/win` 唯一新增的机制是 **VERTEX_TILER 子队列的 tiler 作业**。
   panvk 的 clear/copy 走的是 `vk_meta` 的 **fragment-only fullscreen** 路径（`panvk_vX_cmd_draw.c:5020` → `cmd_draw_fullscreen`，
   `panvk_vX_cmd_meta.c:365-383`），**从不消费 tiler heap 的 chunk**；只有真正 `vkCmdDraw` 才会让固件去碰 tiler heap。
5. **最可疑点【推断，优先级最高】**：**tiler heap（kbase 内核侧创建的 heap context / chunk）**。
   理由：(a) 故障地址落在高位区间——kbase 只把**内核内部**分配（该 fork 自己的注释点名 “tiler heap contexts/chunks”）放在
   `CUSTOM_VA`/JIT zone，而 panvk 自己的 BO/import 都是低位 VA（实测 `va=0x41000`）；
   (b) `panvk_vX_cmd_draw.c` 相对上游**只改了 1 行**（`panthor_kmod_get_csif_props`→`panvk_get_csif_props`），
   说明 tiler 描述符生成逻辑是上游原样，问题在 kbase 平台管道而不在“描述符怎么填”。
6. **下一步最小验证（先免费、后重编）**：
   - **S1（零成本，不改代码）**：`tri` 重跑时加 `MESA_LOG_LEVEL=debug`，抓 `kbase: tiler heap desc 0x…: base 0x…` 这一行，
     拿 `base`（=kbase 给的 `first_chunk_va`）与 `0x5fffe1e000` 对位。**这一步就能把“故障地址是不是 tiler heap”钉死。**
   - **S2（2 行 logi 补丁）**：把 `gpu_heap_va`（heap context VA）也打出来（见 §4 补丁 A），一次重编同时拿到 ctx 与 chunk 两类 VA。
   - **S3（判据性实验）**：把 `kbase` 路径的 `chunk_size` 从 1 MiB 改回 2 MiB（`panvk_physical_device.c:1371`，1 行）后重跑，
     **看故障地址是否跟着变**：变 ⇒ 故障地址由 heap 布局推导出来（固件在走 chunk 链）；不变 ⇒ 是某个固定结构地址（heap context 等）。

---

## 1. 异常码含义（**已定论**，逐位可复算）

### 1.1 status 这个字段是怎么来的

- fork 打印它的位置：`/root/zenithblue/work/mesa/src/panfrost/lib/kmod/kbase_kmod.c:633`
  （`BASE_GPU_QUEUE_GROUP_ERROR_FATAL` 分支，`status & 0xff` 即 exception 字节）。
- 公开 kbase 里该 payload 的唯一生产者（对 kctx 级 MMU 故障）：
  `borr_mali_kbase/csf/mali_kbase_csf.c:1699-1710`
  ```c
  struct base_gpu_queue_group_error err_payload =
     (struct base_gpu_queue_group_error){ .error_type = BASE_GPU_QUEUE_GROUP_ERROR_FATAL,
        .payload = { .fatal_group = { .sideband = fault->addr,   /* ← sideband = 故障 GPU VA */
                                      .status  = fault->status } /* ← status  = MMU FAULTSTATUS */ } };
  ```
  紧接着 `for (csg_nr…) { … kbase_csf_add_group_fatal_error(group, &err_payload); }`：**遍历该 kctx 所有在位 group，填同一份 payload** ⇒ 解释了 “group 0/1/2 完全相同的 status+sideband”。
  同函数族 `kbase_csf_ctx_handle_fault()`（`csf.c:1722-1760`）走的是同一条路径。
- 字段使用方式与寄存器同名（`AS_FAULTSTATUS_*`），见 `mmu/mali_kbase_mmu.c:990,1300,1478`。

### 1.2 逐位解码（3 个来源互相印证）

| 项 | 出处 | 值 |
|---|---|---|
| `AS_FAULTSTATUS_EXCEPTION_TYPE_SHIFT=0 / MASK=0xFF`、`ACCESS_TYPE_SHIFT=8 / MASK=0x3`、`SOURCE_ID_SHIFT=16 / MASK=0xFFFF` | 公开 kbase `hw_access/mali_kbase_hw_access_regmap.h:85-110`（本地 `pub-kbase/rm.h:85`） | — |
| exception 名字表 | `hw_access/regmap/mali_kbase_regmap_csf_macros.h:166-170`：`TRANSLATION_FAULT_0..4 = 0xC0..0xC4` ⇒ **0xC3 = TRANSLATION_FAULT_3** | ✔ |
| 同码另一种命名（fork 自带头） | `work/mesa/src/panfrost/lib/kmod/mali_kbase_csf_registers.h:817`：`CS_FAULT_EXCEPTION_TYPE_TRANSLATION_FAULT_L3 0xC3` | ✔ |
| access type 值 | `rm.h:101-105`：`ATOMIC 0x0 / EXECUTE 0x1 / READ 0x2 / WRITE 0x3` ⇒ `(0x7dc002c3>>8)&3 = 2` = **READ** | ✔ |
| SOURCE_ID = `(status>>16)&0xffff` = **0x7DC0** | 同上 | ✔ |
| core_id = `(source_id & 0x7F<<9)>>9` = 62；arch-12 表 `lut_fault_source_core_id_t_core_type_major_12` 里 `{0xFFFF,62,"csf"}` | `mmu/mali_kbase_mmu_faults_decoder_luts.c:360-403`（本地 `pub-kbase/luts.c:360`） | **csf** |
| internal_requester `ir = (source_id>>4)&0xF` = 12（G720 的 product_model < 14.0，走 older 格式） | `mmu/mali_kbase_mmu_faults_decoder.c:36-43,56-70` | 12 |
| arch-12 的 **CSF READ** 表：`{12,"lsu"}` | `mmu/backend/mali_kbase_mmu_faults_decoder_luts_csf.c:71-76` | **lsu** |

**结论句**：`0x7dc002c3` = 「**CSF 的 LSU 发起的一次 READ 触发三级页表 TRANSLATION_FAULT**」。

### 1.3 为什么是 “L3” 而不是 L0/L1

Mali 用 ARMv8 MMU 语义：`TRANSLATION_FAULT_n` 表示**在第 n 级描述符上失败**。
L3 = 前三级表都有效、**最后一级 PTE 无效** ⇒ 故障 VA 落在「**上层的表已经为别的映射建立过、但这 4 KB 页没映射**」的区域里
（例如紧邻某个已映射区间、或某个 region 只 commit 了一部分）。**【推断】**（该语义取自 ARM MMU 惯例 + 上述命名，未见 ARM 原文引用）。
实践含义：故障地址不是“荒野地址”，而是**某个已存在映射区的邻居/空洞**——这正是 “buffer 末尾外一页”“chunk 边界算错”“context 被销毁但页表还在” 这类 bug 的典型指纹。

### 1.4 明确排除的两类解释

- **不是 CS 指令异常**（`CS_FATAL 0x40-0x68` 那一族）：fork 的等待循环本来会读队列 output page 的 `CS_FAULT(0x80)/CS_FAULT_INFO(0x88)` 并打
  `kbase: CS error 0x… on subqueue …`（`panvk_vX_gpu_queue.c:797-812`），**tri 日志里没有这一行** ⇒ CS 没自己报异常，是 MMU 报的。
- **不是 shader / tiler 单元**：core_id 62=csf（shader 是 0..31，tiler 是 51，L2 是 33..47，mmu 是 55）。
  ⇒ 别按 “shader 读贴图越界” 或 “tiler 写 tile 越界” 方向查。

---

## 2. panvk 侧为什么会触发（代码对照）

### 2.1 工作路径 vs 失败路径的**精确差集**

| | render / ahb（PASS） | tri（FAIL） |
|---|---|---|
| 命令内容 | `vkCmdClearColorImage` + `CopyImageToBuffer` | renderPass(LOAD_OP_CLEAR) + `vkCmdDraw(3)` + copy |
| clear 实现 | `vkCmdClearColorImage` → `vk_meta_clear_color_image()` (`panvk_vX_cmd_meta.c:383`) → `meta->cmd_draw_rects` = `panvk_per_arch(cmd_draw_rects)` (`panvk_vX_device.c:155`) → **`cmd_draw_fullscreen()` → `cmd_run_fullscreen()`** (`panvk_vX_cmd_draw.c:5020-5027`) | 用户自己的 graphics pipeline + 真几何 |
| 用到的子队列 | **仅 FRAGMENT**（fullscreen 直出，无几何） | **VERTEX_TILER + FRAGMENT** |
| tiler heap chunk | **从不消费** | **首次消费**（tiler 要写 tile、要按 chunk 链走） |
| 结果 | 像素精确回读 | `vkQueueSubmit=0` → `vkWaitForFences=-4` |

⇒ **唯一的“新机制”就是 VERTEX_TILER 子队列的 tiler 作业 + tiler heap**。而请求方是 **CSF 的 LSU**，
所以要看的是：**CS 在 tiler 作业里对“panvk 交给它的地址”做的那次 load**。

### 2.2 关键旁证：draw 侧代码是上游原样

`git diff --stat`（只读）显示：`csf/panvk_vX_cmd_draw.c` **只改了 1 行**，`panvk_vX_exception_handler.c`/`panvk_vX_utrace.c` 各 1 行，
且都是同一个改动：
```c
-   panthor_kmod_get_csif_props(dev->kmod.dev);
+   panvk_get_csif_props(dev);
```
`cmd_buffer.c` +47/-?、`dispatch.c` +17、`queue.h`/`cmd_buffer.h` 结构体扩展、`gpu_queue.c` **+1880**、
`panvk_physical_device.c` **+913**、`panvk_wsi.c` +498、`pan_kmod.c` +120、新增 `kbase_kmod.c`(2033 行)/`kbase_csf_uapi.h`/`mali_kbase_*` 头。
⇒ **tiler 描述符/TILER_HEAP 描述符的“填法”不是嫌疑点；kbase 平台管道（kmod + gpu_queue + physical_device 的 props）才是。**

### 2.3 嫌疑点排序（都在 kbase 平台侧）

**H1【推断，最高】tiler heap 的 chunk 链被固件走到未映射页。**
- 建立关系：`init_tiler()` 里 kbase 用 `KBASE_IOCTL_CS_TILER_HEAP_INIT` 让**内核**创建 heap context + `initial_chunks` 个 chunk，
  返回 `gpu_heap_va` / `first_chunk_va`（`panvk_vX_gpu_queue.c:2046-2072`，`kbase_kmod.c:1001-1030`）。
- panvk 把 chunk 起址写进 TILER_HEAP 描述符：`cfg.base=first_heap_chunk; cfg.bottom=base+64; cfg.top=base+chunk_size`（`gpu_queue.c:2096-2103`）。
- kbase 侧 chunk 链是**内核用 “编码后的下一个 chunk 指针” 写在每个 chunk 头部**的（`kid`：`csf/mali_kbase_csf_tiler_heap.c:62-75 encode_chunk_ptr`、`140-150 link_chunk`），
  并且 `first_chunk_va` 的语义是 “**指向 chunk 头部，而不是 chunk 内空闲区起点**”（fork 自带 uAPI 文档 `kbase_csf_uapi.h:367-369`）。
  ⇒ 只要 `cfg.size/top` 与内核实际 chunk 大小不一致，固件就会**在错误偏移处读 “下一个 chunk 指针”**，跟着垃圾指针走到未映射 VA —— 与 “READ + L3 + 高位地址” 完全吻合。
- **注意本 fork 的偏离**：kbase 路径把 chunk_size 从上游的 2 MiB 改成 **1 MiB**、initial 5→10、max 64→400
  （`panvk_physical_device.c:1231-1236` vs `1365-1373`，注释说是为了避开 order-9 大页分配失败）。**这个值是推出来的，没有和 kbase 的编码约束对账**（`chunk_size & ~CHUNK_SIZE_MASK` 校验在 `th.c:688`，我没有取到 `CHUNK_SIZE_MASK`/`CHUNK_HDR_NEXT_*` 常量 —— **【未验证】**）。
- 另一个偏离：注释明说 kbase 上**没有 FRAGMENT_COMPLETED 这类 heap op**（`gpu_queue.c:62-68`：“With firmware chunk recycling disarmed (no FRAGMENT_COMPLETED heap ops on kbase), the heap only grows between renewals”）。上游 panvk 依赖这些 CS heap op 与固件同步 chunk 生命周期；**少了它们，固件的归还未必发生，链状态可能与固件预期不一致**。**【推断】**

**H2【推断】固件读的其实是 heap context 本身（`gpu_heap_va`）而非 chunk。**
- CS wrapper 每次图形 CALL 前都 `cs_heap_set(ctx)`（`gpu_queue.c:557-568`）；`HEAP_SET` 只是写寄存器，**真正去读 context 的是随后的固件**。
- clear 路径（fragment-only）也会 `HEAP_SET`，但它不做 tiler 处理 ⇒ **heap context 的首次真实读取同样发生在 tri**。
- 若 kbase 返回的 `gpu_heap_va` 没有被映射进该 kctx 的 GPU 页表（版本差异/zone 问题），故障地址 = ctx VA。
  **【未验证】**——需要 S1/S2 的 VA 打印来区分 H1/H2。

**H3【推断，低】heap renew/retire/destroy 破坏了固件仍在引用的 context。**
- 代码：`kbase_renew_tiler_heap()`（`gpu_queue.c:2192-2230`，会重写描述符并 retire 旧 ctx）、
  `kbase_try_destroy_retired_heap()`（`:2172-2190`，靠 `emitted_jobs` 计数判断“固件不再可见”后 `KBASE_IOCTL_CS_TILER_HEAP_TERM`）。
- 作者自己在注释里记录过同类事故：“…the firmware then walks whatever now lives at the old chunk VAs as a chunk list and faults on a
  garbage pointer (**observed as an exception 0xc0 CSG fatal** at a wild sideband address during heavy allocation churn)”（`gpu_queue.c:2160-2170`）。
- **但 tri 是全新进程的第一次 draw**：renew 需要 `submit_count>=128` 或 `work_count>=65536`（`gpu_queue.c:2785-2800`），
  **本次不会触发**。⇒ H3 解释不了 tri 必现，但**一定会影响游戏长跑**，是独立的第二个 bug 候选。

**H4【推断，低】CS 读的既不是 heap 也不是别人给它的东西，而是它自己的 ring/stream。**
- 若 CS 取指失败应是 `EXECUTE(0x1)` 且大概率伴随 `CS_FAULT` 上报；本次是 `READ` + 无 CS_FAULT ⇒ 不太像。保留为兜底。

### 2.4 与 06 号报告「MTK 16L32 只在 Gallium」的关系 —— 结论：**不是本条的原因**

- 那说的是 **image modifier / tiling layout 的可接受集合**（`src/panfrost/lib/pan_format.h:24` 一类的注释），属 WSI/AHB/采样路径。
- 现在 clear / copy / AHB(fmt=0x1) / 真实交换链 / present 全 PASS，说明该差异**不构成本次 fault**；
  它仍然是 “MTK 上跑真实 App 时贴图/合成可能出问题” 的独立风险项，不该再拿它解释 `tri`。

---

## 3. 已知坑与上游状态

### 3.1 上游 Mesa：panvk 的 kbase 后端**不在上游**

- 上游 panvk 只面向 **panthor**（上游内核 DRM 驱动）；本仓库是「upstream Mesa 快照 + 自建 kbase 后端」
  （`kbase_kmod.c` / `kbase_csf_uapi.h` / `mali_kbase_*` 头都是 **untracked 新文件**；仓库是 `--depth 1` 浅克隆，HEAD 是一个 radv commit）。
- 因此 **“上游 panvk 有没有修过这个” 这个问法不成立**：这套 `panvk↔kbase` 组合没有上游 CI。
  我**没有**找到直接对应 “G720/MTK kbase + CSF TRANSLATION_FAULT_L3 on draw” 的上游 issue/MR（**【未验证】=“未找到”，不等于“不存在”**）。
- 可参考的上游/公开材料：
  - PanVK / panfrost XDC 2025 讲义：https://indico.freedesktop.org/event/10/contributions/413/attachments/280/370/xdc2025-panfrost.pdf
  - “What is Panthor?”：https://indico.freedesktop.org/event/10/contributions/407/attachments/242/325/XDC%202025%20final.pdf
  - ARM 官方 FAQ 条目「Rendering large amounts of geometry causes rendering artifacts or DEVICE_LOST」
    https://documentation-service.arm.com/static/67a6252a9c13d3639d30d264 —— 对应 tiler heap 容量/续期这一类问题，
    与 max_chunks 设定（本 fork 200/400）相关，但**症状是挂起/花屏而非 0xc3**，只能作背景。

### 3.2 最重要的先在事实：**MTK 上 panvk+kbase 是能跑的**（同项目先前成功）

`/root/panvk-mtk/`（Redmi Note 11T Pro / 天玑8100 / Mali-G610 / kernel 5.10 / kbase r32p1）是**已成功让系统 Vulkan 跑通的同类移植**
（基于 funnymdzz/mesa KRAID fork），`README.md` 已把 MTK 侧坑列清：

1. MTK gralloc 报 `DRM_FORMAT_MOD_INVALID` 但实际分配 **ARM AFBC(32x8|SPARSE|SPLIT|YTR)**，必须回退到 AFBC modifier `0x0800000000000072`；
   且 **AFBC body 必须 4096 对齐**（“白屏”根因）。
2. SurfaceFlinger RenderEngine `DEVICE_LOST`：kbase 无 DRM syncobj，`copy_sync_payloads` 要置 NULL 走 QueueSubmit2 回退。
3. **kick 竞态**：userspace doorbell 快速路径会在 active→idle 丢作业 → GPU 空闲而队列有活 → 超时 → DEVICE_LOST；
   修法：**总是 kick 调度器**。超时 10 s→120 s。
4. `MEM_ALLOC_EX` ENOTTY 回退 `MEM_ALLOC`；队列优先级 HIGH|REALTIME；`vk_android_find_dmabuf_fd` 取最大 size 的 fd；dmabuf size 用 fstat 兜底。

⇒ 建议把这 4 条拿来**逐条对照**当前 zenithblue 树：尤其是第 3 条（kick 竞态）与 kbase 版本相关的行为差异。
本仓库 `gpu_queue.c` 已经实现了 «每 500 ms re-kick» 的兜底（`gpu_queue.c:823-830`），说明作者已经知道这个坑。

### 3.3 公开 kbase 参考源码（本次抓取，已落盘）

`/root/research/csf-work/pub-kbase/`（来自 android.googlesource.com `kernel/google-modules/gpu`
commit `3372cb0ffe779efd9b7507174c61d0e48822b5e2` 的 `borr_mali_kbase/`，以及 commit
`c009f0a10bf01666ee94f7a3f96e8ad1f122a2ed` 的 `mali_kbase/`）：

| 本地文件 | 来源 | 用途 |
|---|---|---|
| `csf.c` | `borr_mali_kbase/csf/mali_kbase_csf.c` | §1.1 payload 生产者、CPU queue dump 等待、CS fault 上报 |
| `th.c` | `.../csf/mali_kbase_csf_tiler_heap.c` | chunk 编码/链/回收、init 校验 |
| `cq.c` | `.../csf/mali_kbase_csf_cpu_queue.c` | CPU queue dump 握手语义 |
| `mmu.c` | `mali_kbase/mmu/mali_kbase_mmu.c` | translation fault 分支、kill 路径 |
| `rm.h` / `mac.h` | `hw_access/mali_kbase_hw_access_regmap.h` / `.../regmap/mali_kbase_regmap_csf_macros.h` | 寄存器位域与码表 |
| `luts.c` / `lsc.c` / `dec.c` / `fd.h` | `mali_kbase/mmu/…decoder*` | SOURCE_ID → **csf/lsu** 的完整解码链 |

（rockchip 镜像里的 `gpu/backend/mali_kbase_gpu_fault_csf.c`（`kbase_gpu_exception_name()`）**没有** 0xC0-0xC4 条目，
正好印证 0xC0-0xC4 属于 **MMU AS fault** 命名空间而非 CS fault：https://gitcode.com/openeuler/rockchip-kernel/blob/openEuler-22.03-LTS-SP2/drivers/gpu/arm/bifrost/gpu/backend/mali_kbase_gpu_fault_csf.c ）

---

## 4. 可执行的规避/定位手段（按成本排序）

> 铁律：手机操作全部由你（父级）执行；下面命令均为「你执行、我写好的」形式。任何驱动试编请在 `/root/research/csf-work/` 自建目录，
> 或按你既有流程重建 `android-v4`（脚本在 `/root/zenithblue/scripts/build-android.sh`；我未执行、未改动该树）。

### S0 —— 零成本：先试内核侧日志（只读，最坏是权限被拒）

kbase 的 MMU 故障上报会打印**解码后**的故障（exception type / access type / source id / pid）。
无 root 时大概率读不到，但值得试（每条 10 秒）：
```sh
# 在手机上（shell uid 或 Shizuku 通道）
dmesg | tail -200 | grep -iE "mali|fault|TRANSLATION|AS[0-9]|page fault"
cat /proc/kmsg 2>/dev/null | tail -100        # 通常 Permission denied
logcat -b kernel -d 2>/dev/null | grep -iE "mali|fault" | tail -50
```
拿到 `Unhandled Page fault … / decoded fault status / source id / pid` 之类行，就等于拿到内核侧的完整判词（含 **AS 号**）。

### S1 —— 零成本：**把 tiler heap 的 VA 打出来**（决定性的一步）

fork 里**已经有**这行日志，只是 `mesa_logd` 在 `MESA_LOG_LEVEL=info` 下被过滤（`panvk_vX_gpu_queue.c:2106-2116`）。
```sh
# 改 runner.sh（或临时命令行）：info → debug
MESA_LOG_LEVEL=debug timeout 45 ./panvk_wsi_probe --icd=/data/local/tmp/libvulkan_panfrost.so --mode=tri
# 然后：
logcat -d -s MESA:V | grep -E "tiler heap desc|GLB iface|bound subqueue"
```
会得到形如：
```
kbase: tiler heap desc 0x<DESC_VA>: base 0x<FIRST_CHUNK_VA>, bottom 0x<…+64>, top 0x<…+chunk_size>, geom 0x<…>, oom_fbd 0x<…>
```
**判据**：
- `base`/`top` 区间**包含或紧邻** `0x5fffe1e000` ⇒ **H1 成立**（固件在走 chunk 链，故障地址就在 chunk 区）。
- `base` 在别处（例如明显低一截），`0x5fffe1e000` 离它 >chunk_size ⇒ 更像 **H2**（固定结构，最可能就是 heap context VA，需要 S2 的 ctx 值确认）。
> 顺便注意：`tri` 里**不要**用 `--mode=all`（12 号报告已证会污染后续模式）。

### S2 —— 2 行补丁：把 **heap context VA** 也打出来（推荐 logi，info 级别可见）

改动 A1（`panvk_vX_gpu_queue.c`，在已有的 tiler heap 日志里加 ctx 与 chunk_size）：
```diff
@@ -2106,12 +2106,14 @@
    if (gpu_queue_uses_kbase(dev)) {
       kbase_clean_priv_mem(tiler_heap->desc, 0, pan_size(TILER_HEAP));
       mesa_logd("kbase: tiler heap desc 0x%" PRIx64
-                ": base 0x%" PRIx64 ", bottom 0x%" PRIx64
+                ": ctx 0x%" PRIx64 ", chunk_size 0x%x"
+                ", base 0x%" PRIx64 ", bottom 0x%" PRIx64
                 ", top 0x%" PRIx64 ", geom 0x%" PRIx64
                 ", oom_fbd 0x%" PRIx64,
-                panvk_priv_mem_dev_addr(tiler_heap->desc), first_heap_chunk,
-                first_heap_chunk + 64,
+                panvk_priv_mem_dev_addr(tiler_heap->desc),
+                tiler_heap->context.dev_addr, tiler_heap->chunk_size,
+                first_heap_chunk, first_heap_chunk + 64,
                 first_heap_chunk + tiler_heap->chunk_size,
                 panvk_priv_mem_dev_addr(tiler_heap->desc) + 4096,
                 panvk_priv_mem_dev_addr(tiler_heap->oom_fbd));
```
改动 A2（`kbase_kmod.c`，紧跟 `kbase_kmod_csf_tiler_heap_create()` 的 out 赋值，用 **logi** 保证 info 级别可见）：
```diff
@@ -1026,6 +1026,12 @@
    *heap_ctx_va = req.out.gpu_heap_va;
    *first_chunk_va = req.out.first_chunk_va;
+   mesa_logi("kbase: tiler heap create: chunk_size 0x%x initial %u max %u -> "
+             "ctx 0x%" PRIx64 ", first_chunk 0x%" PRIx64,
+             chunk_size, initial_chunks, max_chunks, *heap_ctx_va,
+             *first_chunk_va);
    return 0;
```
改动 A3（可选，但很值；`panvk_priv_bo.c:20 panvk_priv_bo_create()` 里打一行 `logi`，输出 panvk 自己所有 BO 的 `dev_addr/size`）
——这一步直接验证「panvk 的 BO 都在低位、`0x5f…` 只可能来自内核内部 JIT/CUSTOM_VA 分配」这个前提：
```c
   mesa_logi("kbase: priv_bo dev 0x%" PRIx64 " size %" PRIu64 " flags 0x%x",
             priv_bo->dev_addr, size, flags);
```
**判据**：若 panvk 全部 BO 的 VA 都远低于 `0x5fffe1e000`（例如几十 MB ~ 几百 MB 级），则 §0.5 的「故障地址属于内核内部 JIT/CUSTOM_VA（tiler heap）」推断成立。

### S3 —— 判据性实验：扰动 heap 布局，看故障地址是否跟着动

1 行改动（`panvk_physical_device.c:1371`，kbase 分支），回到上游的 chunk_size：
```diff
-      device->csf.tiler.chunk_size = 1024 * 1024;
+      device->csf.tiler.chunk_size = 2 * 1024 * 1024;
```
（或只把 `initial_chunks` 10→40 改变堆规模，效果类似）
重跑 `tri`，记录 sideband：
- **地址变了** ⇒ 故障地址是**由 heap 布局推导出来的** ⇒ 固件确在走 chunk 链 ⇒ 重点查 chunk 链语义（H1：`cfg.size/top` 与内核编码是否一致、缺 FRAGMENT_COMPLETED heap op、chunk 被 shrink）。
- **地址不变（仍 0x5fffe1e000）** ⇒ 是**固定结构地址** ⇒ 重点查 heap context 的映射/传递（H2）。

### S4 —— 探针最小二分（只改探针，不改驱动）

`tri` 目前的顶点是 `gl_VertexIndex` 生成的（无 vertex buffer、无 descriptor）：
```glsl
// tri.vert
vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float((gl_VertexIndex & 2)));
gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
```
**改动 B（退化三角形：tiler 作业照旧发出，但没有有效图元 ⇒ 不产生 tile）**：
```glsl
#version 450
void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }   // 三顶点重合 → 零面积
```
重建 + 上机（`build.sh` 会自动重生成 SPV 头，需要 glslangValidator+python3，本机已有）：
```sh
cd /root/research/probe10 && bash build.sh            # 产出 panvk_wsi_probe（md5 会变）
# 你方现有上机流程：push 到 /data/local/tmp，再 ./runner.sh tri
```
**判据**：
- 退化三角形**也 fault** ⇒ 故障发生在**tiler 作业的固定 setup**（读 TILER_HEAP/heap context）⇒ H2 抬头。
- 退化三角形 **PASS** ⇒ 故障发生在**真正消费 chunk 的时候** ⇒ H1（chunk 链/大小/回收）几乎确定。
> 次级变体（同一目的、更省事）：`renderArea/scissor` 改成 1×1 或 0×0，看是否跳过 tiler 作业。

### S5 —— 把 fork 已有的「最后一条 stream」诊断在 DEVICE_LOST 路径上也打出来

现在只有**超时**路径才 dump ring/stream（`gpu_queue.c:832-880`：`timeout on subqueue …`、`last job on subqueue … stream 0x%…/%u`、`last ring[0..15]`），
而本次是 fence 报 DEVICE_LOST，**这些行一行都没打**（tri 日志可证）。
建议：在 `vkWaitForFences` 失败/检测到 `kbase_kmod_csf_has_error()` 时也调用同一段 dump（只读 `subq->kbase.last_stream_addr/last_stream_size/last_job_*`）。
另外 fork 里已有 CS 指令解码器 `kbase_stream_opcode()` / `kbase_log_stream_prefix()`（`gpu_queue.c:258-300`），
在 `gpu_queue.c:2508-2512` 是**注释掉的状态**（“Use csf decoding instead”）——打开它就能把 VT 子队列的 CS 流按 opcode 打出来，
用来定位“哪条 load 读了那个地址”。

### S6 —— 补上固件明确要的那次 dump（`KBASE_IOCTL_CS_CPU_QUEUE_DUMP`）

tri 时间线（`/root/research/probe10/out-device/l_tri.txt`）：
```
11:08:11.915 vkQueueSubmit(draw) -> 0
11:08:12.575 kbase: received CSF CPU queue dump notification     ← 早于 fatal 0.53 s
11:08:13.106 kbase: CSF group 2/0/1 fatal error … 0xc3
```
**固件/内核明确要求主机 dump CS 状态，本 fork 只打日志就丢了**（`kbase_kmod.c:604-610`）。
公开 kbase 侧语义（`pub-kbase/cq.c:35-45, 53-80`）：
- `kbase_csf_cpu_queue_read_dump_req()` 只在 `dump_req_status == ISSUED` 时产生该通知；
- ioctl 处理是 **`copy_from_user`**：**dump 内容由 userspace 生成后交给内核**（debugfs 再打印成字符串）。
  ⇒ “实现 ioctl” 本身不等于拿到数据，**驱动必须自己把 CS 状态（输出页里的 CS 寄存器 / PC / CS_FAULT / CS_FAULT_INFO / 各 subqueue 的 insert-extract）格式化进 buffer**。
- 内核侧还会等 dump 完成再注销 group（`csf.c:1466-1470` “wait_for_dump_complete_on_group_deschedule … whilst the dumping was going on for a fault”）。
  ⇒ **不服务它至少会让故障现场丢失**；是否直接导致 0.53 s 后的 group fatal **【未验证】**。
建议实现骨架（`kbase_kmod.c` 通知分支，约 40 行）：
```c
if (event->type == BASE_CSF_NOTIFICATION_CPU_QUEUE_DUMP) {
   /* 1) 用 kbase BO 分一块 4~8 KB、CPU 可写内存，把 CS 状态格式化进去
    *    （CS_USER_IO_OUTPUT_* / CS_FAULT@0x80 / CS_FAULT_INFO@0x88 / 各 ring 的 insert/extract）
    * 2) 交给内核： */
   struct kbase_ioctl_cs_cpu_queue_info info = {
      .buffer = (uint64_t)(uintptr_t)text,   /* userspace 地址，内核 copy_from_user */
      .size   = text_len,
   };
   if (ioctl(dev->fd, KBASE_IOCTL_CS_CPU_QUEUE_DUMP, &info))
      mesa_loge("kbase: CPU queue dump failed: %s", strerror(errno));
   return 0;
}
```
（ioctl 号 `_IOW(0x80,53,struct kbase_ioctl_cs_cpu_queue_info)`，见 `kbase_csf_uapi.h:448-457`。**buffer 的合法来源【未验证】**：公开实现直接 `copy_from_user`，因此**普通可写内存即可**；若 MTK 版实现不同需以设备实测为准。）

### S7 —— 环境变量级实验（零成本，用来排除 H3）

```sh
PANVK_KBASE_HEAP_RENEW_INTERVAL=0   # 关闭 tiler heap 续期（默认阈值 128 次提交 / 65536 work）
PANVK_DEBUG=kbase_diag              # 打开 CS 内 breadcrumb（seqno pre/post-call / stream progress）
```
对 `tri` 单次提交，renew 本来不会触发（`gpu_queue.c:2785-2800`），所以这两个变量**预计不改变 tri 结果**；
但它们是**游戏长跑（MGL/Minecraft）掉 GPU**那条线的低成本对照，值得和 S4 一起做。

### S8 —— 关于「退到非 CSF 路径」

- **G720 只有 CSF，没有 JM**（panvk 的 `jm/` 分支对 arch≥10 无效），所以“退回 job manager”不成立。
- panvk 里**没有**“不用 tiler / 立即模式”的开关：`PANVK_DEBUG` 全集为
  `startup/nir/trace/sync/no_afbc/linear/dump/cs/copy_gfx/force_simultaneous/implicit_others_inv/force_blackhole/wsi_afbc/no_wb_mmap/no_user_mmap_sync/cached_before_coherent/no_extended_va_range/hsr_prepass/no_crc/kbase_diag`
  （`panvk_instance.h:19-38`，名字表 `panvk_instance.c:63`）——**没有一个是关 tiler 的**。
- 因此：**图形渲染无法绕过 tiler heap**，不存在“软件回退”。可用的“规避”只有两条：
  1. 让 clear/copy 类工作继续可用（现状已可用）；
  2. **通过 S1/S3 把 tiler heap 这条线修对**（这也是 MGL 能跑的唯一出路）。
  （若只是要演示“驱动能出图”，可继续用 clear/`vk_meta` 路径；但那不是游戏。）

---

## 5. 若判定为驱动侧真 bug：最小可疑位置 + 补丁方向

| # | 位置 | 怀疑 | 补丁方向（先做判据实验，别直接改） |
|---|---|---|---|
| 1 | `panvk_physical_device.c:1365-1373`（kbase 分支硬编码 1 MiB/10/400） | chunk_size 与 kbase 的 chunk 头编码/固件预期不一致 ⇒ “下一个 chunk 指针”读错位 | 先做 S3（切回 2 MiB）看故障地址是否移动；若移动，再从 GPU props/CSIF 里**查询**真实 tiler chunk size，而不是沿用 panthor 的常量 |
| 2 | `panvk_vX_gpu_queue.c:2096-2103`（TILER_HEAP 描述符写入） | `size/base/bottom/top` 与内核 side 的实际 chunk 语义不匹配（`first_chunk_va` 指向 chunk 头，panvk 用 `base+64` 作为自由区起点） | 与 `th.c:140-150`（`link_chunk` 把编码指针写在 **prev chunk 头部偏移 0**）对照；必要时把 heap 描述符改为“每次 CALL 前随 ctx 一起刷新”（目前已做到 ctx 刷新，但 `cfg.base` 只在创建/续期时写） |
| 3 | `panvk_vX_gpu_queue.c:2172-2230`（renew/retire/destroy）、`kbase_kmod.c:1033-1046`（`TILER_HEAP_TERM`） | 固件仍在引用旧 heap ctx/chunk 时被销毁（作者注释里已观测到同类 0xc0 事故） | 最保守实验：**进程生命周期内永不 TERM**（把 `kbase_try_destroy_retired_heap` 改成恒 `return false` 或只 retire 不 destroy），看长跑是否还掉 GPU |
| 4 | `panvk_vX_gpu_queue.c:62-68`（“kbase 上没有 FRAGMENT_COMPLETED heap ops”） | 少了与固件的 chunk 生命周期握手 ⇒ 链状态不一致 | 对照上游 panvk 在 fragment job 末尾发的 heap op（`cs_heap_*`/`cs_heap_complete` 一类），在 kbase 上补发并观察 |
| 5 | `panvk_vX_gpu_queue.c:557-568`（每次图形 CALL 的 `HEAP_SET`） | 与固件“group suspend / heap 归属”的交互（MTK kbase 的 off-slot heap-reclaim shrinker，作者注释 `kbase_kmod.c:558-563` 提过） | 先按 S6 把固件要求的 dump 补上，确认固件是否在报 “heap 已被回收/不可用” |
| 6 | `kbase_kmod.c:604-610`（CPU queue dump 被丢弃） | 诊断缺失（是否**导致** fatal 未验证） | 按 S6 实现；至少把 CS 输出页状态打进 logcat |

**推荐实施顺序**：S0 → S1（免费）→ S2（2~3 行 log）→ S4（探针退化三角形）→ S3（chunk_size 扰动）→ S6/S5（诊断增强）→ 再动 H3/H4 的修复补丁。
前四步都不触碰 tiler 逻辑本身，却能一次性把「H1/H2」二分掉。

---

## 6. 未验证清单（明确不装懂）

1. `TRANSLATION_FAULT_3` = “前三级表有效、PTE 无效” 的 **ARM MMU 语义**：按 ARMv8 惯例解释，**未找到 ARM 原文逐字引用**。
2. `0x5fffe1e000` 属于 kbase **CUSTOM_VA/JIT zone**：由「fork 自己的注释说 tiler heap ctx/chunk 来自该 zone」+「实测 panvk import VA=0x41000（低位）」+「地址距离 0x6000000000 仅 0x1E2000」推断；
   **kbase 的 VA 空间上界/zone 边界我没有取到源码**（`kbase_region_tracker_init_jit_64` 所在文件未抓）。**S1/S2/S3 可证实或证伪。**
3. 故障地址是 **heap context** 还是 **chunk**：未区分，靠 S1/S2 的 VA 对位。
4. MTK kbase 的 **uAPI 版本与 struct 布局**（`KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_6` 回退路径是否真的用上、`target_in_flight=65535` 是否被限制）——**未验证**；但 `TILER_HEAP_INIT` 明确成功了（否则会有 `mesa_loge` 失败行）。
5. `KBASE_IOCTL_CS_CPU_QUEUE_DUMP` 在本机 MTK kbase 上的 buffer 语义与可用性：**未验证**（公开实现是 `copy_from_user`，普通内存即可）。
6. 上游是否存在同类 MR/issue：**未找到**（≠ 不存在）；panvk 上游不覆盖 kbase 后端。
7. 本次未做任何手机操作、未编译、未改任何受保护目录；仅新增 `/root/research/csf-work/pub-kbase/`（公开 kbase 参考源码）。

---

## 附：本次用到的关键引用速查

- payload 生产者（sideband=fault addr / status=fault status、3 组复制）：`pub-kbase/csf.c:1687-1713`（= `borr_mali_kbase/csf/mali_kbase_csf.c`）
- 寄存器位域：`pub-kbase/rm.h:85-110`；码表 `pub-kbase/mac.h:118-170`（MMU AS 码）/ `:368-397`（access type）
- 解码链：`pub-kbase/dec.c:36-70` + `pub-kbase/luts.c:360-403`（core_id 62 = “csf”）+ `pub-kbase/lsc.c:71-76`（ir 12 = “lsu”）
- tiler heap：`pub-kbase/th.c:62-75,140-150,666-700`；fork 侧 `kbase_kmod.c:1001-1046`、`panvk_vX_gpu_queue.c:2010-2135,2160-2230`
- CPU queue dump：`pub-kbase/cq.c:35-80`；fork 侧 `kbase_kmod.c:604-610`、uAPI `kbase_csf_uapi.h:448-457`
- panvk clear = fragment-only：`panvk_vX_cmd_meta.c:365-383`、`panvk_vX_device.c:155`、`panvk_vX_cmd_draw.c:5020-5027`
- 上游 tiler 参数 vs kbase 分支：`panvk_physical_device.c:1231-1236` vs `1365-1373`
- 现场证据：`/root/research/probe10/out-device/l_tri.txt`（时间线）、`/root/research/12-probe-run-results.md`
- MTK 成功先例：`/root/panvk-mtk/README.md`
