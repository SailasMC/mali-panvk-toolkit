# 31 · v65：kbase ring wrapper 不得抢占 PanVK 的 `SB_MASK_STREAM`

**任务**：读现场 → 判据链 → 找下一个「与内核/固件契约不符」的点 → 实施一处改动 → 出 v65 APK → 确定性对照。
**结论一句话**：v64 两次挂起的签名完全一致——**三个子队列的 ring extract 都精确停在各自最后一条 ring entry 的 `CALL` 指令上**，`CS_ACTIVE=0`、`cell->error=0`、无 CS fault、无 TILER_OOM、10 s 内 20 次 rekick 一字节未动。CPU 侧提交/kick/等待/通知链路被证据**排除**；机制是**流切换（CALL）处的 CS 状态被卡**。v65 = 把 kbase wrapper 在每个 ring entry 头部**重写 `SB_MASK_STREAM`** 的那 2 行删掉（该状态属于 PanVK 的流状态机，主线 panthor 内核的 ring entry 一条 `SET_STATE` 都不发）。

---

## 1. 现场签名（`/sdcard/MG/cap.txt`，v64 两次运行）

### 1.1 时间线

| 项 | Run 1 (PID 22794) | Run 2 (PID 25236) |
|---|---|---|
| 驱动加载（`W/MESA No gralloc hwmodule`） | 14:19:40.774 | 14:23:25.728 |
| MobileGL 初始化 → 交换链 2376x1080 count=3 | —（cap.txt 未含 mgl.log 行） | 14:23:25 / 14:23:30 |
| 帧流（`vkshim SurfaceCaps`） | 615 次，末帧 14:20:14.467 | 503 次，**末帧 14:23:47.334** |
| `tiler heap renewal` 次数（interval 32 ✓） | **23** 次：14:19:47.548 … 14:20:04.109 | **19** 次：14:23:31.662 … 14:23:46.472 |
| 看门狗超时 | 14:20:14.459 | 14:23:57.343 |
| 存活（驱动加载→超时） | **33.7 s** | **31.6 s**（其中渲染 17.3 s ≒ 29 fps） |
| 等待起点（超时 −10 s） | ≈14:20:04.4（= 末次续期后 13 次提交） | ≈14:23:47.35（= 末次续期后 29 次提交） |

> 两次运行都只活 ~32 s；`cap.txt` 里**没有**更长的运行记录（文件仅含这两次）。任务书里的「2 分 51 秒」不在本文件内，本轮无法复核。

### 1.2 挂起原文（Run 2，PID 25236）

```
14:23:57.343 E/MESA: kbase: timeout on subqueue 0: seqno 636, ls_copy 0, target 637,
  marks pre/post-call/post-wait 0x0/0x0/0x0, stream progress 0x0,
  insert 122752, extract 122672, active 0, error 0x0,
  ring[0..3] 0x1c00000000000003/0x1c00000900000008/0x1c00000100000000/0x1c00000200000001
14:23:57.343 E/MESA: kbase: last job on subqueue 0: ring offset 57024, entry 160/192 bytes,
  stream 0x5fec220000/6496, flush 36, extract offset 57136
14:23:57.343 E/MESA: kbase: last ring[0..15]
  0x1c00000000000003/0x1c00000900000008/0x1c00000100000000/0x1c00000200000001/   ← 5×SET_STATE_IMM32
  0x1c0000080000fff0/0x220000000000000c/0x17a005ff8af7380/0x17c006000000040/       ← SET_STATE / REQ_RES / MOVE48 / MOVE48
  0x30007c0000000000/0x27e000000000024/0x24007e0000000233/0x300000000010000/       ← HEAP_SET / MOVE32(=36) / FLUSH_CACHE2 / WAIT(1<<16)
  0x17c005fec220000/0x27e000000001960/0x20007c7e00000000/0x17c005ff8af5000         ← MOVE48 stream / MOVE32 size / ★CALL★ / MOVE48 seqno
14:23:57.343 E/MESA: kbase: extract line ring[0..7] @57136&~63
  0x30007c0000000000/0x27e000000000024/0x24007e0000000233/0x300000000010000/
  0x17c005fec220000/0x27e000000001960/0x20007c7e00000000/0x17c005ff8af5000
14:23:57.343/344 E/MESA: kbase: timeout snapshot subqueue 0/1/2: seqno 636, target 637,
  insert 122752, extract 122672/122672/122656, active 0, error 0x0, jobs 637
14:23:57.343 E/MESA: queue syncobj 0/1/2: seqno 6904/14942/6977, error 0x0
14:23:57.345 F/MobileGL: Present, vkQueuePresentKHR → VK_ERROR_DEVICE_LOST (-4)
                  (VulkanRenderer.cpp:13001，随后 vkAcquireNextImageKHR → :13067)
```

Run 1（PID 22794，14:20:14.459）**同形**：`seqno 748, target 749, lns_copy 0, marks 0x0/0x0/0x0, insert 144704, extract 144624/144624/144608, active 0, error 0x0`，last job `ring offset 13440, entry 160/192 (VT/FRAG)、144/192 (COMPUTE), flush 255`，`extract offset 13552`。

### 1.3 解码：extract 精确落在 `CALL`（两条独立证据）

* 发射序列共 20 条单字指令（`entry 160/192` 自证 160 B = 20×8；`192−160=32 B` 为 0 填充），
  顺序 = 5×SET_STATE_IMM32(idx0-4) → REQ_RESOURCE(5) → MOVE48 ctx reg(6) → MOVE48 heap ctx(7) → HEAP_SET(8) → MOVE32 flush_id(9) → FLUSH_CACHE2(10) → WAIT(IMM_FLUSH)(11) → MOVE48 stream_addr(12) → MOVE32 stream_size(13) → **CALL = idx14 = +112 B** → …（与 `18-ring-dump-and-missing-logs.md §4` 的独立解码逐条一致）。
* 三条算术全部闭合：
  * Run 2 sq0/sq1：`122672 − 65536 = 57136 = 57024 + 112` ✓（112 = 14×8 = CALL）
  * Run 2 sq2（COMPUTE）：`122656 − 65536 = 57120 = 57024 + 96` ✓（96 = 12×8：COMPUTE 少 MOVE48 heap + HEAP_SET 共 16 B）
  * Run 1 sq0/sq1：`144624 − 131072 = 13552 = 13440 + 112` ✓；sq2：`144608 − 131072 = 13536 = 13440 + 96` ✓
* `extract line ring[0..7]` 是从 `extract & ~63` 起的 64 B 快照，其第 7 个字正好是 `0x20007c7e00000000`(CALL)，且 `stream_size 0x1960=6496`、`flush_id 0x24=36` 与同一行的 `/6496`、`flush 36` 逐位对上 ⇒ **extract == CALL 的地址**，三队列各自如此。

### 1.4 与历史数据点对照

| 数据点 | 卡住 seqno | extract 位置 | 其它 |
|---|---|---|---|
| v54 时代（`23-*` 报告） | 432 | sq0 **已排空**（insert==extract==83584，seqno==target==433 → 完成）；sq1 `+112`=CALL、sq2 `+96`=CALL | `active 0`、`error 0` |
| v61（`29-*` §216） | 3~5 | 「停在 wrapper 的 CALL **之前**」 | 门铃/丢唤醒路径曾被怀疑 |
| v62env | 324 | — | **伴随 tiler heap OOM** |
| v63 | 576 / 无快照 | — | — |
| **v64（本轮）** | **748（run1）/ 636（run2）** | **三队列全部精确在 CALL** | `active 0`、`error 0`、无 OOM |

⇒ 「extract 停在最后一条 ring entry 的 CALL」**是从 v54 至今的稳态失败签名**（不是新形态）；C2 改变的是**发生的时刻**（432 → 576/636/748），没有改变类别。

---

## 2. 判据链（含排除）

1. **不是「驱动没 kick / 丢唤醒」——排除。**
   * 固件**确实**取走了该 ring entry 的前 14 条指令（extract 到了 CALL），说明 insert/doorbell/kick 生效；
   * 等待循环跑满 10 s：`kbase_subqueue_wait_seqno()` 每 ≤20 ms 一轮、每 500 ms 对 `rekick_mask` 内每个子队列 `kbase_kmod_csf_queue_kick()` ⇒ ≥20 次真 kick，logcat **无** `KBASE_IOCTL_CS_QUEUE_KICK failed`；
   * `base`/KCPU 路径健康（无 `kcpu queue disabled due to error`），`cell->seqno` 与 `extract` 10 s 内一字节未动 ⇒ 不是 CPU 侧唤醒问题。
2. **不是「完成通知契约问题」——排除（作为原因）。** 完成写 `SYNC_ADD64` 是 idx18/19，**在 CALL 之后**；636 次成功完成证明契约有效；本次它根本没被取到。
3. **`active 0` + `error 0` + 无 `CS fault`/无 `tiler heap OOM` 通知 ⇒ 不是 fault/OOM 路径**（OOM 计数 0 ✓，与任务书一致）。
4. **tiler heap 世代切换（P2）——降权，不作触发因。** 依据：(a) 两次运行 renewal 间隔 32 均生效；(b) 挂起前的最后作业分别距上次续期 **13 / 29** 次提交，其间 13、29 次提交正常渲染（run2 续期后仍有 0.86 s / 29 帧）；(c) 退役 context 只在**图形全 drain 后**销毁（`kbase_try_destroy_retired_heap()`），且本轮无 OOM/无 0xc3；(d) seqno 未回绕、无重放（若是 CS 被按 `CS_EXTRACT_INIT` 重启，extract 会回 0 而 seqno 会跳变，均未发生）。
5. **共同资源定位**：三个子队列**同时**停在各自的 CALL ⇒ 涉及 CSG 级共享资源。scoreboard 槽 0..15 是 **CSG 共享**的（PanVK 的跨队列同步 `wait_finish_tiling()` 用内存 syncobjs，而 iteration 槽 3..15 由三个子队列的 endpoint 工作共同 signal）；`finish_cs()` 与 wrapper 末尾都发 `WAIT(all)`，属上游/内核既有协议（见 §3.1 已逐位核对），**不是**本次改动对象。
6. **结论**：机制是**流切换点（CALL）被卡住**——要么固件从未派发该 CALL，要么已进入被调用流且永不返回；两者都被**同一个 CS 状态**门控：`SB_MASK_STREAM`。而我们的 wrapper 在**每条** ring entry 头部都用自己猜的值重写它（见 §3）。

---

## 3. 改动（一处变量，一个文件）

### 3.1 权威对照：主线 panthor 内核的 ring entry 里**没有任何 `SET_STATE`**

`drivers/gpu/drm/panthor/panthor_sched.c :: prepare_job_instrs()`（主线，本轮 web 取回）逐条为：

```
MOV32 rX+2, cs.latest_flush ; FLUSH_CACHE2.clean_inv_all.no_wait.signal(0) rX+2
MOV48 rX:rX+1, cs.start ; MOV32 rX+2, cs.size ; WAIT(1<<16) ; CALL rX:rX+1, rX+2
MOV48 rX:rX+1, sync_addr ; MOV48 rX+2, #1 ; WAIT(all)          ← waitall_mask = GENMASK(sb_slot_count-1,0)
SYNC_ADD64.system_scope.propagate_err.nowait ; ERROR_BARRIER
```
且该文件顶部明确：「the kernel … calls them from the queue ring-buffer by the kernel using a pre-defined sequence of command stream instructions」。我们 wrapper 的 FLUSH/WAIT(1<<16)/CALL/WAIT(all)/SYNC_ADD64/ERROR_BARRIER 与之**逐位一致**（`0x24007e0000000233`、`0x300000000010000`、`0x20007c7e00000000`），`cs_reg_count=128 → addr_reg=124=cs_reg_count−4=cs_reg_count−CSF_UNPRESERVED_REG_COUNT`（`panthor_fw.c` 硬编码 4）也一致 ✓。
差异只剩：wrapper 额外发 **5×SET_STATE + REQ_RESOURCE +（图形队列）MOVE48 ctx + HEAP_SET**；其中 `SB_SEL_OTHER=LS` / ctx 寄存器 / HEAP_SET 有 kbase 侧的必要性论证，而 **`SB_MASK_STREAM` 没有**。

### 3.2 这 5 条 `SET_STATE` 是「init stream 的一次性初始化」，不是 per-entry 状态

`panvk_vX_gpu_queue.c:1746-1756`（**上游代码**，`panvk_queue_init_contexts()` 里的 init stream）：
```c
   /* Intialize scoreboard slots used for asynchronous operations. */
   cs_set_state_imm32(..., SB_SEL_ENDPOINT, SB_ITER(0));
   cs_set_state_imm32(..., SB_MASK_WAIT, SB_WAIT_ITER(0));
   cs_set_state_imm32(..., SB_SEL_OTHER, SB_ID(LS));
   cs_set_state_imm32(..., SB_SEL_DEFERRED, SB_ID(DEFERRED_SYNC));
   cs_set_state_imm32(..., SB_MASK_STREAM, dev->csf.sb.all_iters_mask & ~SB_WAIT_ITER(0));   ← 保留，未动
```
wrapper 把同一组默认值**复制到每条 ring entry 头部**。问题在于这 4 个寄存器里有 3 个是 PanVK **流状态机自己会改**的状态：
* `cs_iter_sb_update_end()`（`csf/panvk_cmd_buffer.h:751-767`）**每次迭代**都写
  `SB_MASK_WAIT = BIT(next_sb)`、`SB_MASK_STREAM = all_iters_mask & ~BIT(next_sb)`，注释写明动机是「避免 wait(current)/signal(next) 冲突」；
* PanVK 还用 **indirect async op**（`cs_defer_indirect()`：`cmd_draw.c:4216 cs_vt_end`、`:4219 cs_finish_fragment`、`:4636 RUN_FRAGMENT`、`cmd_dispatch.c:54` compute dispatch），这些 op 的 **wait mask / signal slot 是执行时从 `SB_MASK_WAIT` / `SB_SEL_DEFERRED` 读的**。
因此「每条 entry 开头把 init 默认值重新写回去」= 在**上一条 entry 的 endpoint op 可能仍在飞**的时候改写它们的间接操作数 ⇒ 等待一个永远不会被 signal 的槽；而 `SB_MASK_STREAM` 恰好门控**流切换**，正是现场卡住的位置。

### 3.3 逐字 diff（只动 wrapper，`kbase_subqueue_emit_job()`）

文件：`src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c`（改动后 123 748 B；改前 122 818 B）
备份：`src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c.bak-1791182484`（= v64 源码逐字节）

```diff
@@ -513,8 +513,20 @@
    cs_set_state_imm32(&b, MALI_CS_SET_STATE_TYPE_SB_SEL_OTHER, SB_ID(LS));
    cs_set_state_imm32(&b, MALI_CS_SET_STATE_TYPE_SB_SEL_DEFERRED,
                       SB_ID(DEFERRED_SYNC));
-   cs_set_state_imm32(&b, MALI_CS_SET_STATE_TYPE_SB_MASK_STREAM,
-                      dev->csf.sb.all_iters_mask & ~SB_WAIT_ITER(0));
+   /* SB_MASK_STREAM (and SB_MASK_WAIT / SB_SEL_ENDPOINT / SB_SEL_DEFERRED)
+    * is *stream* state, not ring-entry state: ... (14 行说明) */
 #else
    cs_set_scoreboard_entry(&b, SB_ITER(0), SB_ID(LS));
 #endif
```
* 改动后 `grep -c "cs_set_state_imm32.*SB_MASK_STREAM"` = **1**（仅 init stream，:1753）；wrapper 区段 = **0** ✓
* 只删 1 条指令 ⇒ ring entry 变短：VT/FRAG **160→152 B（19 条，CALL 由 +112 移到 +104）**、COMPUTE **144→136 B（17 条，CALL +96→+88）**。**这是下一轮挂起时最廉价的验证标记**（`entry 152/192`、`extract offset = last+104`）。

---

## 4. 编译与确定性对照（全部 exit 0）

```
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
cd /root/zenithblue/build/android-v4 && ninja
```
| 步骤 | ninja exit | 产物 sha256 / 字节 |
|---|---|---|
| ① 改后编译（v65） | **0** | `b9952f750c6e452c71bb1b7b368f7c4909aff9f91717628cb89031c7b9d1eeb0` / 20 006 016 B |
| ② **撤改动**（`cp bak → 源文件`）重编 | **0** | **`c03f0e7b7e391b20edfcadcfc4b69068693dacdb785fb6ad0c6bf6e6fcdb1106` / 20 007 048 B = v64 驱动，逐位相等 ✓✓** |
| ③ 再应用改动重编 | **0** | `b9952f75…` 与 ① **逐位相等** ✓ |

唯一编译告警：`panvk_vX_gpu_queue.c:830: warning: label followed by a declaration is a C23 extension`（既有 `kbase_wait_continue:`，非本轮引入）。

---

## 5. APK `/root/final/mgl-panvk-v65.apk`

* versionCode **65**，versionName `6.5-no-wrapper-sbmaskstream`，包名 `com.dsh.plugin.driver.g720`（同 v64）
* 载荷 = **新驱动** + v63/v54 的 `libMobileGL.so` + `classes.dex`；打包脚本 `/root/pack_v65.sh`（基于 `pack_v63.sh`），日志 `31-work/pack-v65.log`

| 载荷 | APK 内字节 | APK 内 sha256 == 源 |
|---|---|---|
| `lib/arm64-v8a/libvulkan_freedreno.so` | 20 006 016 | `b9952f750c6e452c71bb1b7b368f7c4909aff9f91717628cb89031c7b9d1eeb0` == 构建产物 ✓ **非空** ✓ |
| `lib/arm64-v8a/libMobileGL.so` | 16 956 584 | `72919c73a7e07630f329bb4ad9605c606a9aac9e2fc6596683ee7b9f9c76848b` == `/root/v54/…` ✓ |
| `classes.dex` | 1 328 | `6bd3abde2c53506f1b54bb88a8069c3cc394c2fb08440e4ca475cbf8fd64f4ad` == `/root/v54/classes.dex` ✓ |

* `aapt2 dump xmltree` 与 `31-work/31-v64-manifest.txt` 对比 ⇒ **只有两行不同**（versionCode、versionName），其余（含 `line=` 行号、pojavEnv/boatEnv 原样）逐字符一致 ✓
* `PANVK_KBASE_HEAP_RENEW_INTERVAL` 在 env 中 **保留** ✓；`MESA_VK_WSI_HEADLESS_SWAPCHAIN` 计数 = 0 ✓
* APK：10 187 311 B，sha256 `d59b50705b81646362f36b28934f1955ac324e8d3af383e8f3bfc08b8699b854`
* APK 内驱动 sha256 对照：v63/v64 = `c03f0e7b…`，v65 = `b9952f75…` ✓（安装前可再核一次）

---

## 6. 回滚命令

```bash
# 退回 v64（唯一改动 = 恢复 wrapper 的 SB_MASK_STREAM 两行）
cd /root/zenithblue/work/mesa
cp -f src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c.bak-1791182484 \
      src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c        # 122818 B
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
cd /root/zenithblue/build/android-v4 && ninja                 # 期望 c03f0e7b…b1106，20007048 B（已实测 ✓）
# 或直接整包回退（无需重编）
#   /root/final/mgl-panvk-v64.apk  （驱动 c03f0e7b…，manifest 除 version* 外与 v65 相同）
```

---

## 7. 未验证 / 下一批候选（按优先级）

1. **同类「wrapper 抢占流状态」的另外两条**（同文件同区段，若 v65 有效但未根治，下一步单变量试）：
   `SB_MASK_WAIT`（:511）与 `SB_SEL_ENDPOINT`（:510）。二者同样由 `cs_iter_sb_update_end()`/`cs_select_endpoint_sb()` 按迭代维护，且是 `cs_defer_indirect()` 的直接操作数来源。
2. **`REQ_RESOURCE`（:522-525）**：主线内核的 ring entry **不发**它——kbase/panthor 的端点资源申报走 CSG 接口（`endpoint_req/endpoint_req2` + doorbell，见 `panthor_fw.c: panthor_fw_csg_endpoint_req_*`）。在 ring 里每 entry 申报，可能与内核在 group schedule 时写入的 endpoint_req 冲突。**风险中等**（可能变成端点不获授权），需单变量实验。
3. **`CS_EXTRACT_INIT`（input page +0x8，u64）从未被写**：`include/drm-uapi/mali_kbase_ioctl.h:394-399` 与 `mali_kbase_csf_registers.h:136-140` 都定义了该字段（"Initial extract offset for ring buffer"），我们的 driver 只写 `CS_INSERT`。**本现场已证明它不是本次挂起的原因**（若固件按该值重启 CS，extract 会回 0、seqno 会跳变，实测均无），但仍是队列 ABI 的缺口。
4. **诊断升级（零风险，建议与下一次 A/B 同时做）**：输出页镜像里有 `CS_STATUS`（含 `CS_STATUS_WAIT_SB_MASK`）、`CS_REQ/CS_ACK`、`CS_FAULT/CS_FAULT_INFO`（我们只读了 0x80/0x88）——把 `CS_STATUS` 与 `CS_STATUS_WAIT_*` 打进 timeout 快照，就能直接看到「这条 CS 到底在等哪个槽」；配合 `PANVK_DEBUG=kbase_diag` 可拿到 wrapper 的 `marks pre/post-call/post-wait` 与 PanVK 的 `PANVK_KBASE_PROGRESS_*` 位图，**一次性判定固件是否进过被调用流**。
5. **未验证项（如实标注）**：
   * `CS_EXTRACT` 在 `CALL` 执行期间的语义（「已派发/返回地址」vs「尚未取到」）——`23-*` §3.0 起就列为未知，本轮仍**未从权威文档确认**；本报告只断言「ring fetch 停在 CALL」，两种读法都支持同一处修因。
   * `CS_ACTIVE`（输出页 +0x8）到底是「初始 extract 偏移 + HW_ACTIVE 位」的实时镜像，还是仅在某次启动时写入的陈旧值——`active 0` 因此只能作为**辅证**，不能单独定性。
   * v64 的「2 分 51 秒」最长存活**无法复核**（`cap.txt` 仅含这两次 ~32 s 的运行）。
   * v65 的改动**尚未真机验证**（本轮明确禁止操作手机）；A/B 材料已就位：`/root/v65/lib/arm64-v8a/libvulkan_freedreno.so`（= `b9952f75…`）与 `/root/v64`、`/root/final/mgl-panvk-v64.apk` 可直接换装。
   * 判据「SB_MASK_STREAM 门控流切换/流进入」是**推断**（PanVK 的 `cs_iter_sb_update_end()` 注释 + 现场卡点 + 内核 ring entry 无 SET_STATE 三条旁证），**未从 Arm CSF 规范确认**；v65 的实测结果即是该推断的判定实验：若生效，应看到挂起 seqno 显著推后或消失；若无效，按 §7.1→§7.2 继续单变量推进。
