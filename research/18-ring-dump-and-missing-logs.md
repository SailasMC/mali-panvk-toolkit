# 18 · Ring dump 逐条解码 + v54 三条新日志为何没出（问题 A / B）

> 对象：真机 OPPO PHZ110 / MT6989 / Immortalis-G720 MC12 / Android 16 / 无 root 的 v54 实测日志
> （12:20:51–12:21:09，`E/MESA` + `I/MESA`）。
> 只读：未改 `/root/mesa`、`/root/zenithblue`、`/root/MobileGL`，未碰手机；本报告只写 `/root/research/`。
> 构建树 = `/root/zenithblue/work/mesa`（v54 的编辑在这里，**不是** `/root/mesa`）。
> 所有结论给 file:line；推断一律标 **推断/未验证**。

---

## 0. TL;DR

**问题 A（为什么新增日志没出）**：
1. `kbase_kmod_csf_group_create()` 全树**只有一个**调用点：`panvk_vX_gpu_queue.c:1039`（`kbase_create_group()`）← `:3119`（`panvk_per_arch(create_gpu_queue)`，`#ifdef HAVE_PAN_KMOD_KBASE` + `if (uses_kbase)`）。
   **没有重载、没有第二个实现、没有被 `#if` 包住、日志之前没有任何 `return`**。
2. v54 那条 `negotiated kernel uAPI`（`kbase_kmod.c:518-519`）是该函数的**第一条可执行语句**；
   `llvm-objdump` 实测：函数入口 `f6ed48–f6ed5c` 就是 `mov w0,#2; bl mesa_log`（MESA_LOG_INFO=2），
   **在任何版本比较之前**。同级的 `mesa_logi` 在真机上确实出得来（同文件 `kbase_kmod.c:1588` 的
   `kbase_kmod_bo_alloc_dmabuf: succeeded` 与 `vk_android.c:802` 的 `AHB layout fallback` 都打出来了）。
3. 现场**确实走到了 kbase 队列路径**（否则不会有 `kbase: timeout on subqueue …`／`last job`／`last ring`：
   这三行在 `gpu_queue.c:840/853/860`，且在函数里解引用 `subq->kbase.user_io`/`ringbuf_cpu`/`last_job_*`
   ——这些字段只在 `kbase_create_group()` + kbase 写环路径里赋值）。
   ⇒ 组创建**必然**执行过 ⇒ 若跑的是 v54 二进制，`kbase_kmod.c:518` **必然**打印。
4. **它没打印 ⇒ 那次实测加载的驱动不是 v54 构建**（或抓的日志窗口不含启动早期）。
   最可能是"设备上跑的 .so 不是 v54 这一份"：见 §2.5 的三种机制（含 vkshim 静默回退 `/data/local/tmp` 的坑）。
   **这一条只能在设备侧用 sha256/md5 定案**，我（禁操作手机）没有验证。
5. ③`tiler heap renewal`（`gpu_queue.c:2815`）**另有独立的、合法的缺席理由**：它只在
   `kbase_tiler_submit_count >= 128` 且图形队列 drain 成功后才打（`:2822`、`:2810`）。
   所以**第 3 条不出现不能当作"v54 没跑"的证据**；只有前两条（518 / 623）是判据。

**问题 B（ring dump 解码）**：现场只给出 6 个 64 位字（日志被截断成 `0x17…`）。
按树内权威解码器，opcode 在 **bits[63:56]**（`v12_pack.h:1794` `__gen_unpack_uint(opcode,…,56,63)`）：

| idx | 值 | 解码 |
|---|---|---|
| 0 | `0x1c00000000000003` | `SET_STATE_IMM32(state=SB_SEL_ENDPOINT=0, value=SB_ITER(0)=3)` |
| 1 | `0x1c00000900000008` | `SET_STATE_IMM32(state=SB_MASK_WAIT=9, value=SB_WAIT_ITER(0)=8)` |
| 2 | `0x1c00000100000000` | `SET_STATE_IMM32(state=SB_SEL_OTHER=1, value=SB_ID(LS)=0)` |
| 3 | `0x1c00000200000001` | `SET_STATE_IMM32(state=SB_SEL_DEFERRED=2, value=SB_ID(DEFERRED_SYNC)=1)` |
| 4 | `0x1c0000080000fff0` | `SET_STATE_IMM32(state=SB_MASK_STREAM=8, value=all_iters_mask & ~SB_WAIT_ITER(0)=0xfff0)` |
| 5 | `0x220000000000000c` | `REQ_RESOURCE(tiler=1, idvs=1, fragment=0, compute=0)` = VT 子队列的资源申报 |

这 6 个字与发射端 `gpu_queue.c:511-516`（5×`cs_set_state_imm32`）+ `:526`（`cs_req_res`）**逐条吻合**，
且 `entry_size=160B=20 条指令` 与 §4.3 列出的完整 20 条发射序列**长度精确相等** ⇒ 解码自证成立。
**卡住的 `entry 160/192 bytes` 不是 tiler 命令，是 kbase 的 ring wrapper（"用户态 ring 入口/蹦床"）**：
它 20 条指令（160 B，补齐 192 B）之后 `CALL` 进 8208 B 的 PanVK 流（`stream 0x5ff72ec000`）。
固件的 extract 指针 `22768 = 22656+112` **正好落在第 15 条（idx14）= 那条 `CALL`**（`gpu_queue.c:597`）
⇒ wrapper 自己跑完了，卡点在**被 CALL 的 PanVK 流里**（或该 CALL 未被满足）。
与 tiler heap OOM 的因果**不能从 wrapper 判定**（**未验证**）；两者时间上因果顺序关系见 §4.7。

---

## 1. 先定"哪棵树、哪份产物"（否则行号会骗人）

| 事实 | 证据 |
|---|---|
| `/root/mesa` **不是**本次构建树 | 其 `kbase_kmod.c` 的 `kbase_kmod_csf_group_create` 在 `:500` 且**无** v54 日志；`gpu_queue.c` 的 `kbase_renew_tiler_heap` 在 `:2089`、调用点在 `:2681` |
| 构建树 = `/root/zenithblue/work/mesa` | v54 三处编辑实测在位：`kbase_kmod.c:518-519`、`:620-625`（字符串在 `:624`）、`gpu_queue.c:2815-2820` |
| v54 产物 | `/root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so`，size 20006408，sha256 `a9cba64afa935370906e1a8533550816c6a018f4d6808cb8951dd4e1099333f1`（本轮复核） |
| APK 载荷一致 | `unzip -p /root/final/mgl-panvk-v54.apk lib/arm64-v8a/libvulkan_freedreno.so \| sha256sum` = 同上（本轮复核） |
| v53 产物（对照） | `/root/v53/lib/arm64-v8a/libvulkan_freedreno.so` sha256 `58ef996f…bec4`，md5 `7f3a0e8f8a2ec70d31f11fa6fe3ab404`（后者见 `research/16-p1-p2-implementation.md` §2.2）；**不含** `negotiated kernel uAPI`，**含** `1.6 ABI`/`TILER_OOM CSI handler`/`last ring[0..15]`/`P0A-V19` |

**设备端"驱动是谁"的入口（关键坑）**：`libMobileGL.so` 内嵌了 vkshim，字符串里同时有
`%s/libvulkan_freedreno.so`、`libvulkan_freedreno.so`、`/data/local/tmp/libvulkan_freedreno.so`；
其源码 `/root/mgl_icd/vkshim_mgl.c:42-46`：

```c
char selfdir[512]={0}; ... dladdr(&shim_init,&info) ... /* libMobileGL.so 所在目录 */
snprintf(cand0,...,"%s/libvulkan_freedreno.so",selfdir);
const char* c[]={cand0,"libvulkan_freedreno.so","/data/local/tmp/libvulkan_freedreno.so",0};
for(int i=0;c[i];i++){ g_icd=dlopen(c[i],RTLD_NOW|RTLD_LOCAL); if(g_icd) break; }
```

**它只 `FLOG` 指针（`:52` `[vkshim] icd=%p gipa=%p gdpa=%p`），从不打印选中的路径** ⇒
"设备实际加载了哪一份 `.so`"在 logcat 里**不可见**。正常路径下 FCL/ZL2 `dlopen(<插件目录>/libMobileGL.so)`
（`research/08-zl2-surface.md:197`，GameLauncher.kt:208-224）⇒ 候选 0 命中插件 `nativeLibraryDir` 的兄弟 `.so`
（= APK 里那份新驱动）；但若"插件目录"里只有 `libMobileGL.so`（没有兄弟），就会**静默**落到
裸名/`/data/local/tmp` 上的**旧件**。

---

## 2. 问题 A-1：CSF queue group 创建的真实代码路径

### 2.1 全树调用链（无第二条路）

```
panvk_per_arch(create_gpu_queue)                      vulkan/csf/panvk_vX_gpu_queue.c:3081
  #ifdef HAVE_PAN_KMOD_KBASE
  const bool uses_kbase = gpu_queue_uses_kbase(dev);   :3108 ; 定义 :119-123
                                                       uses_kbase == (phys_dev->kbase_node_path[0] != 0)
  if (uses_kbase) {
      result = kbase_create_group(queue);              :3119
      ...
  } else { ... create_group(queue,...) /* panthor 路径 */ }
```

```
kbase_create_group()                                   vulkan/csf/panvk_vX_gpu_queue.c:1025-1026
  for (i = 0; i < PANVK_SUBQUEUE_COUNT; i++)           ; 3 个 subqueue：VT/FRAG/COMPUTE (panvk_queue.h:22-25)
      kbase_kmod_csf_group_create(dev->kmod.dev, 1,      :1039
                                  &subq->kbase.group_handle)
```

```
kbase_kmod_csf_group_create()                          lib/kmod/kbase_kmod.c:509-510   ← v54 日志在 :518
  if (pan_kmod_driver_version_at_least(&dev->driver,1,25)) { ioctl(_CREATE)      :556
        mesa_logd("... CS fault reporting")           /* DEBUG，默认不出 */ }
  if (pan_kmod_driver_version_at_least(&dev->driver,1,18)) { ioctl(..._1_18)     :586
        mesa_logi("... TILER_OOM CSI handler (1.18 layout, ioctl 58)")           :589-590
        mesa_logw("... 1.18 ... failed ... falling back to the 1.6 ABI")         :593-594 }
  ioctl(KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_6)                                   :614
        mesa_logi("... created CSF group %u with the 1.6 ABI (no csi_handlers…)") :620-625
```

**全树仅此一个 `kbase_kmod_csf_group_create` 调用点**（`grep -rn 'csf_group_create' src/` 只有
`kbase_kmod.h:41` 声明、`kbase_kmod.c:510` 定义、`gpu_queue.c:1039` 调用）。
`panvk_vX_device.c` / `panvk_physical_device.c` **没有**任何 group-create 调用者，它们只负责"选 kbase 并开设备"：

* `panvk_physical_device.c:370-392`：探测到 kbase 节点 → `pan_kmod_dev_create_with_driver(fd, …, "kbase", …)`，
  然后 `snprintf(device->kbase_node_path,…,path)`（`:389`）。日志 `Found kbase device '%s'.`（`:373-374`，PANVK_DEBUG=STARTUP）。
* `panvk_vX_device.c:312`（`uses_kbase` 判定）、`:472-517`（`open(physical_device->kbase_node_path)`，`/dev/mali0`）。
* `kbase_resource_mask()`（`gpu_queue.c:126-138`）等只有 kbase 队列路径会用到。

### 2.2 v54 三条日志的确切位置与"必然可达"证明

| # | 位置（构建树） | 级别 | 位置性质 |
|---|---|---|---|
| ① | `kbase_kmod.c:518-519` `negotiated kernel uAPI %u.%u` | `mesa_logi`=INFO | **函数第一条可执行语句**，无条件 |
| ② | `kbase_kmod.c:620-625` `created CSF group %u with the 1.6 ABI (no csi_handlers…)` | INFO | 1.6 兜底成功之后，无条件（v54 新增） |
| ③ | `gpu_queue.c:2815-2820` `tiler heap renewal (uAPI %u.%u, submits %u, renew interval %u)` | INFO | 在 renew 门槛内（见 §3） |

**① 的机器码证据**（`llvm-objdump -d --disassemble-symbols=kbase_kmod_csf_group_create`，
v54 的 `.so`）：

```
0000000000f6ed24 <kbase_kmod_csf_group_create>:
  f6ed24: sub  sp,sp,#0xa0 ; … 存 x19/x20/x21 …
  f6ed38: ldp  w3,w4,[x0,#0x8]        ← varargs = dev->driver.version.{major,minor}
  f6ed48: adrp x1,… ; add x1,…        ← 字符串/MESA tag
  f6ed54: add  x2,…                   ← 格式串
  f6ed58: mov  w0,#0x2                ← MESA_LOG_INFO(=2)
  f6ed5c: bl   0xdc6374 <mesa_log>    ← ① 日志调用本身
  f6ed60: ldr  w8,[x20,#0x8]          ← 之后才开始版本比较（1.25 / 1.18 …）
```

* 级别常量：`src/util/log.h:41-43` `MESA_LOG_ERROR=0, WARN=1, INFO=2, DEBUG=3`；
  `mesa_logi` = `mesa_log(MESA_LOG_INFO, …)`（`log.h:78`）⇒ ①与"能出得来"的 INFO 同级。
* INFO 通道在真机可用：设备日志里 `I/MESA: kbase_kmod_bo_alloc_dmabuf: succeeded …` 来自
  `kbase_kmod.c:1588`（`mesa_logi`，**同一个文件**）；`I/MESA: AHB layout fallback: …` 来自
  `vk_android.c:802`（`mesa_logi`）。
* `MESA_LOG_LEVEL` 未设置（v54 manifest 的 env 里没有）；`MESA_DEFAULT_LOG_LEVEL` 至少是 INFO
  （`util/log.c:140-143`），不会滤掉 INFO。

⇒ **排除**：①不是"插进了未被调用的重载/分支"（全树单一实现、单一调用点）；不是被 `#if` 排除
（`:3081` 那个 `#ifdef HAVE_PAN_KMOD_KBASE` 决定的是"要不要调 `kbase_create_group`"，而现场
**已经证明**走的是 kbase 分支，见 §2.3）；不是"走了 legacy panthor 路径"；不是"panic 前就 return 了"
（①之前没有语句）；也不是 `mesa_logi` 不输出（同文件 :1588 出了）。

### 2.3 现场确实走了 kbase 队列路径（否则那些超时 dump 不可能出现）

设备打印的这三行**只有 kbase 队列路径能产生**：

| 设备行 | 出处（构建树） | 为什么只可能来自 kbase 路径 |
|---|---|---|
| `kbase: timeout on subqueue 0: seqno …, insert …, extract …` | `gpu_queue.c:840-851` | 在 `kbase_subqueue_wait_seqno()`（`:729-730`）内；调用者 `:449/:987/:1407/:2669/:2692` 全在 `#ifdef HAVE_PAN_KMOD_KBASE` 的 kbase 分支 |
| `kbase: last job on subqueue 0: ring offset …, entry …` | `gpu_queue.c:853-859` | 同上，且打印 `subq->kbase.last_job_*` |
| `kbase: last ring[0..15] …` | `gpu_queue.c:860-880` | `kbase_ring_qword()`（`:225-230`）读 `subq->kbase.ringbuf_cpu` |

这些字段只在 kbase 路径被写：
`kbase_create_group()`（`:1025-1100`，分配/映射/绑定 `ringbuf_cpu`、`user_io`）与
`kbase_subqueue_emit_job()`（`:655-661`：`last_job_offset/last_job_entry_size/last_job_size/
last_stream_addr/last_stream_size/last_flush_id`）。panthor 路径下 `user_io` 为 NULL，
`:341` 的 `subq->kbase.user_io + 8192` 会直接崩，不可能打出这些行。
（另外 `E/MESA: kbase: CSF group 0 tiler heap OOM notification` 出自 `kbase_kmod.c:708`，
经 `kbase_kmod.c:872` 的通知读取路径。）

### 2.4 结论 ①（问题 A-1）

> `kbase_kmod_csf_group_create()` 在真机被调用过，①（`kbase_kmod.c:518`）在 v54 二进制里是该函数
> 的第一条语句、INFO 级、通道可用 ⇒ **只要跑的是 v54 的 `.so`，①必然出现在 logcat 里**。
> 现场 ① 与 ②（1.6 兜底那条）**都没出现**，而"没出现"不可能由 v54 的源码造成
> ⇒ **那次实测里运行的驱动二进制不是 v54 这份**（或抓取的日志窗口不覆盖启动早期）。

**按可能性排序的三种机制（全部需设备侧验证；我未操作手机，均标未验证）**：

1. **设备上被加载的 `.so` 不是 v54**：
   * APK 没真正覆盖安装 / 装的还是 v53 或更早；或
   * `libMobileGL.so` 所在目录里没有兄弟 `libvulkan_freedreno.so` ⇒ vkshim 静默回退到
     **`/data/local/tmp/libvulkan_freedreno.so`**（`/root/mgl_icd/vkshim_mgl.c:44-46`）——那是探针工作流
     （`research/10-verify-probe.md:1608`、`12-probe-run-results.md:322`）留下的手工 push 件，极易过期；
   * 或游戏进程未重启（旧 `.so` 仍映射）。
2. **日志窗口不含启动早期**：①在 `vkCreateDevice`/队列创建时打印（`Found kbase device` 更早），
   父级给的片段从 12:20:51 开始，中间只有 12:20:51→12:20:56 的空档 ⇒ 需要确认抓的是**全量** logcat
   而不是尾部窗口。
3. （已被 §2.3 排除）"函数没被调用"。

**设备侧定案命令（父级执行，我不碰手机）**：

```bash
# 期望 v54 md5 = 5917807d13b52c593dd7f87b7b153244 ; v53 md5 = 7f3a0e8f8a2ec70d31f11fa6fe3ab404
adb shell 'md5sum /data/local/tmp/libvulkan_freedreno.so /data/local/tmp/libvulkan_panfrost.so 2>&1'
adb shell pm path com.dsh.plugin.driver.g720
adb shell 'ls -l /data/app/*/com.dsh.plugin.driver.g720*/lib/arm64/'
adb shell 'md5sum /data/app/*/com.dsh.plugin.driver.g720*/lib/arm64/libvulkan_freedreno.so'
adb shell 'tail -20 /sdcard/MG/vkshim.log'      # vkshim 的 FLOG 落盘（只有指针，没有路径）
adb logcat -d | grep -n 'group_create\|negotiated kernel uAPI\|1.6 ABI\|Found kbase device'
```

**修复建议（下一轮，避免再次瞎猜）**：
* 给驱动加一条**无条件、最早**的构建指纹日志（例如 `.so` 的 `__attribute__((constructor))` 里、
  或 `pan_kmod_dev_create()` 里打 `mesa_logi("panvk build %s", git_sha1)`）——`src/git_sha1.h` 已在构建时生成
  （`research/16-p1-build.log` 第 [1/19] 步）；
* 或让 vkshim 把 `c[i]` **路径**打进 `/sdcard/MG/vkshim.log`（一行改动，定位"到底加载了谁"）；
* 或者测试前先把 v54 的 `.so` 同时推到插件 `nativeLibraryDir` 兄弟位与 `/data/local/tmp`。

### 2.5 另一个必须注意的"假信号"

`marks pre/post-call/post-wait 0x0/0x0/0x0`、`ls_copy 0`、`stream progress 0x0` **不代表固件没跑**：
这些 store 只在 `PANVK_DEBUG=kbase_diag` 时才发射（`gpu_queue.c:573-583`、`:601-611`、`:620-629`），
v54 manifest 的 env 是 `PANVK_DEBUG=1`，不匹配任何 flag 名 ⇒ 这些字段**恒为 0**，无信息量。
（不要把这三个 0 解读成"固件连 wrapper 都没进"。）

---

## 3. 问题 A-2：`kbase_renew_tiler_heap` 的调用点与条件（③为何可能合法缺席）

* 定义：`gpu_queue.c:2192`（`kbase_renew_tiler_heap()`）；辅助 `kbase_try_destroy_retired_heap()` `:2172`。
* **唯一调用点**：`gpu_queue.c:2822`，在 `panvk_queue_submit_ioctl_kbase()`（`:2705`）的提交尾部：

```c
2787:   if (touched & graphics_mask) {                 /* touched |= BIT(qsubmit->queue_index)  :2760 */
2788:      queue->kbase_tiler_submit_count++;
2790:      queue->kbase_tiler_work_count += submit->tiler_work_estimate;   /* 该字段全树无写入点 ⇒ 恒 0 */
       }
2802:   if (queue->kbase_tiler_submit_count >= kbase_tiler_heap_renew_interval() ||
2803:       (submit->tiler_work_estimate && renew_work &&
2804:        queue->kbase_tiler_work_count >= renew_work)) {
2806:      result = kbase_wait_graphics_targets(queue, submit->kbase_target_seqnos, UINT64_MAX);
2809:      if (result != VK_SUCCESS) return result;     /* drain 失败 ⇒ 连日志都不打 */
2815:      mesa_logi("kbase: tiler heap renewal (uAPI %u.%u, submits %u, renew interval %u)" …);  ← ③
2822:      result = kbase_renew_tiler_heap(queue);
2830:      queue->kbase_tiler_submit_count = 0; queue->kbase_tiler_work_count = 0;
       }
```

* 门槛：`kbase_tiler_heap_renew_interval()` 默认 **128**（`KBASE_TILER_HEAP_RENEW_INTERVAL`，`:79`；
  可用 env `PANVK_KBASE_HEAP_RENEW_INTERVAL` 覆盖，`:86-95`）。
* 计数器只在一次提交**触及 VT 或 FRAG 子队列**时自增（`:2787-2788`；`graphics_mask` 定义 `:2782-2784`）。
* ①（work 阈值）那条分支因 `submit->tiler_work_estimate` 无 producer 而恒假（`:2790`、`:2803`）。

⇒ **③ 不出有三种合法原因**：图形提交数没到 128（本场景 12:20:56 开始渲染、12:20:59 就 OOM，
约 3 秒，完全可能不足 128 次）；或提交全落在 compute；或图形 drain 失败。
**因此"③没出"不能证明 v54 没跑**——只有 ①/② 能。反之：若下一轮 ①/② 出现而 ③ 始终不出现，
则应怀疑"提交数不足"或"drain 卡住"。

---

## 4. 问题 B：ring dump 逐条解码

### 4.1 opcode 位域与枚举出处（可复核）

* 指令基类：`v12_pack.h`（生成头，`/root/zenithblue/build/android-v4/src/panfrost/genxml/v12_pack.h`）
  `struct MALI_CS_BASE { uint64_t data; enum mali_cs_opcode opcode; }`（`:1771-1774`），
  其 `_unpack` 宏：`__gen_unpack_uint(opcode, &opaque[0], 56, 63)`（`:1794`）。
* `__gen_unpack_uint` 语义（`src/panfrost/genxml/pan_pack_helpers.h:39-51`）：
  `for (w=(start)/32; w<(end)/32+1; w++) __val |= cl[w] << ((w-(start)/32)*32); out = (__val >> (start%32)) & mask;`
  ⇒ start=56,end=63 取 `cl[1] >> 24`，而 `opaque[2]` 是小端双字 ⇒
  **opcode = `word >> 56`（最高字节）**。
* opcode 枚举：`v12_pack.h:1280-1325`（`NOP=0, MOVE48=1, MOVE32=2, WAIT=3, … SET_STATE_IMM32=28,
  REQ_RESOURCE=34, FLUSH_CACHE2=36, SYNC_ADD64=51, ERROR_BARRIER=47, HEAP_SET=48, CALL=32 …`）。
* G720 = `PAN_ARCH 12`（`genxml/gen_macros.h:53-58` 的 `PAN_ARCH==12 → v12_pack.h`），与设备一致。
* 字段位域（同生成头）：
  * `MALI_CS_SET_STATE_IMM32`：`value` bits0..31、`state` bits32..39、`opcode` bits56..63（`:2858-2871`）；
    `state` 枚举 `:1711-1715`：`SB_SEL_ENDPOINT=0, SB_SEL_OTHER=1, SB_SEL_DEFERRED=2,
    SB_MASK_STREAM=8, SB_MASK_WAIT=9`。
  * `MALI_CS_REQ_RESOURCE`：`compute` bit0、`fragment` bit1、`tiler` bit2、`idvs` bit3、`rt` bit4（`:3078-3089`）。

### 4.2 逐条解码表（**只有 6 个字可用**：日志里第 7 字被截断为 `0x17…`）

| idx | 字节偏移（+last_off） | 原始 64 位字 | opcode（word>>56） | 解码 |
|---|---|---|---|---|
| 0 | +0 | `0x1c00000000000003` | `0x1c`=28 `SET_STATE_IMM32` | `state=0(SB_SEL_ENDPOINT)`, `value=3 = SB_ITER(0)` |
| 1 | +8 | `0x1c00000900000008` | 28 `SET_STATE_IMM32` | `state=9(SB_MASK_WAIT)`, `value=8 = SB_WAIT_ITER(0)=BIT(3)` |
| 2 | +16 | `0x1c00000100000000` | 28 `SET_STATE_IMM32` | `state=1(SB_SEL_OTHER)`, `value=0 = SB_ID(LS)` |
| 3 | +24 | `0x1c00000200000001` | 28 `SET_STATE_IMM32` | `state=2(SB_SEL_DEFERRED)`, `value=1 = SB_ID(DEFERRED_SYNC)` |
| 4 | +32 | `0x1c0000080000fff0` | 28 `SET_STATE_IMM32` | `state=8(SB_MASK_STREAM)`, `value=0xfff0 = all_iters_mask & ~BIT(3)` |
| 5 | +40 | `0x220000000000000c` | `0x22`=34 `REQ_RESOURCE` | `compute=0, fragment=0, tiler=1, idvs=1`（= VT 子队列的资源申报） |
| 6..15 | +48.. | `0x17…`（日志截断） | **未解码**（原文字节不足；按源码应为 MOVE48/HEAP_SET/MOVE32/FLUSH_CACHE2/WAIT/MOVE48/MOVE32/**CALL**/MOVE48/WAIT） | — |

（`SB_*` 宏：`panvk_cmd_buffer.h:278-290`：`PANVK_SB_LS=0, PANVK_SB_IMM_FLUSH=0,
PANVK_SB_DEFERRED_SYNC=1, PANVK_SB_DEFERRED_FLUSH=2, PANVK_SB_ITER_START=3, PANVK_SB_ITER_COUNT=5`；
`SB_ID(nm)=PANVK_SB_##nm`、`SB_ITER(x)=PANVK_SB_ITER_START+x`、`SB_WAIT_ITER(x)=BIT(PANVK_SB_ITER_START+x)`。）

### 4.3 三条独立交叉验证（为什么这个解码可信）

1. **与发射端逐条吻合**：这 6 个字恰好对应 `gpu_queue.c:511/512/513/514/516`（5×`cs_set_state_imm32`）
   与 `:526`（`cs_req_res(&b, kbase_resource_mask(subqueue))`），顺序、state 值、value 值全对。
2. **长度精确自证**：源码发射序列共 20 条单字指令 ⇒ `entry_size = 160 B`，与设备打印
   `entry 160/192 bytes` 的 160 **完全相等**（若 `cs_move64_to` 走了 `>=2^48` 的双 MOVE32 分支
   ——`cs_builder.h:1481-1490`——或 `cs_wait_slots` 拆成两条，长度都会变 168+，不是 160）。
3. **数值自证**：`all_iters_mask` 由 `panvk_vX_device.c:536-547` 算出
   （`all_mask=BITFIELD_MASK(scoreboard_slot_count)`、`all_iters_mask=BITFIELD_RANGE(3, count-3)`）；
   若 `scoreboard_slot_count=16` ⇒ `all_iters_mask=0xfff8` ⇒ `0xfff8 & ~0x8 = 0xfff0`，
   与 idx4 的 value **逐位相等**。并且 `0x0c = CS_TILER_RES(bit2)|CS_IDVS_RES(bit3)`
   （`cs_builder.h:1508-1511`）正是 `kbase_resource_mask(PANVK_SUBQUEUE_VERTEX_TILER)`
   （`gpu_queue.c:126-138`）⇒ **subqueue 0 = VERTEX_TILER**，与 `cs.h` 的 `#if PAN_ARCH >= 11` 分支一致。

### 4.4 "last job" 各字段的语义（谁写的、什么意思）

`gpu_queue.c:853-859` 的打印，字段由 `:655-661` 写入：

| 打印 | 字段 | 语义 |
|---|---|---|
| `ring offset 22656` | `last_job_offset` | 该 ring entry 在 64 KiB 环内的起始字节偏移（`KBASE_RINGBUF_SIZE = 64*1024`，`:58`） |
| `entry 160/192 bytes` | `last_job_entry_size` / `last_job_size` | 真正指令字节数 / 按 64 B cacheline 对齐后的槽位大小（`:651-652`：`ALIGN_POT(entry_size,64)`）。**192-160=32 B 是 0 填充（NOP）** |
| `stream 0x5ff72ec000/8208` | `last_stream_addr`/`last_stream_size` | 本次 wrapper 要 `CALL` 的 PanVK 命令流（另一块 BO）的 GPU VA / 字节数 |
| `flush 22` | `last_flush_id` | 本次提交用的 flush id（`cs_flush_caches` 的 `latest_flush_id`） |
| `extract offset 22768` | 现场读的 `CS_USER_IO_OUTPUT_CS_EXTRACT` | 固件在该环内的**下一条待取指令**偏移（`% KBASE_RINGBUF_SIZE`） |

### 4.5 卡住的 entry 是什么？固件停在哪一步？

* **类型**：不是"tiler 命令"，也不是 VERTEX_TILER 子队列的**工作本身**；它是 kbase 的
  **ring wrapper / 用户态 ring 入口**，由 `kbase_subqueue_emit_job()`（`gpu_queue.c:465-670`）发射，
  用途（源码注释 `:504-510`、`:521-531`）：用户态 ring 自管，因此必须自己在环里
  ① 声明资源需求（`REQ_RESOURCE`）② 恢复子队列上下文寄存器 ③ 装载 tiler heap 上下文（`HEAP_SET`）
  ④ flush cache ⑤ `CALL` 进真正的 PanVK 流 ⑥ 用 `SYNC64_ADD` 递增 seqno ⑦ `ERROR_BARRIER`。

* **完整 20 条（160 B）发射序列与行号**（用于把 extract 偏移映射成指令）：

| idx | 字节 | 指令 | 来源行 |
|---|---|---|---|
| 0-4 | 0..39 | 5×`SET_STATE_IMM32`（SB_SEL_ENDPOINT / SB_MASK_WAIT / SB_SEL_OTHER / SB_SEL_DEFERRED / SB_MASK_STREAM） | `:511,512,513,514,516` |
| 5 | 40 | `REQ_RESOURCE`（tiler+idvs） | `:526` |
| 6 | 48 | `MOVE48`（subqueue ctx 寄存器 ← 上下文地址） | `:533-534` |
| 7 | 56 | `MOVE48`（r92:r93 ← tiler heap ctx） | `:565` |
| 8 | 64 | `HEAP_SET` | `:566` |
| 9 | 72 | `MOVE32`（r94 ← flush_id） | `:588` |
| 10 | 80 | `FLUSH_CACHE2` | `:589-592` |
| 11 | 88 | `WAIT`（IMM_FLUSH） | `:593` |
| 12 | 96 | `MOVE48`（r92:r93 ← stream_addr 0x5ff72ec000） | `:595` |
| 13 | 104 | `MOVE32`（r94 ← stream_size 8208） | `:596` |
| **14** | **112** | **`CALL`（r92:r93, r94）** | **`:597`** |
| 15 | 120 | `MOVE48`（r92:r93 ← seqno cell 地址） | `:619` |
| 16 | 128 | `WAIT`（all_mask=0xffff） | `:620` |
| 17 | 136 | `MOVE48`（r94:r95 ← 1） | `:630` |
| 18 | 144 | `SYNC64_ADD`（system scope，r94:r95 → [r92:r93]，defer 到 DEFERRED_SYNC） | `:631-632` |
| 19 | 152 | `ERROR_BARRIER` | `:635` |

* **`extract 22768 = 22656 + 112` ⇒ 正好是 idx14 的 `CALL`**（16 进制：22656+112=22768 ✓）。
  即：wrapper 前 14 条（idx0..13）已被固件取走/执行，环指针停在/停在 `CALL` 上。
  按 CSF 语义，`CALL` 跳出环进入 `stream 0x5ff72ec000` 时该队列的 extract 不再前进
  （返回时才继续）⇒ **卡点在"被 CALL 的 PanVK 命令流"里**（该 `CALL` 未能返回）。
  **推断/未验证**：也有另一种读法（`CALL` 本身因资源/scoreboard 未满足而未完成）；
  两者都指向 `CALL`/其目标流，但要用下一节的证据区分。

### 4.6 `seqno 118` / `target 119` / `insert 22848`

* `insert 22848`：CPU 写指针（字节）。`22848 - 22656 = 192 = padded_size` ⇒ 最后一个 entry 就是上面这个 wrapper。
* `seqno 118`：seqno cell 当前值。每个 wrapper 末尾的 `SYNC64_ADD(+1)`（idx18）完成一次就 +1
  ⇒ **118 个 job 已完成**。
* `target 119`：CPU 侧等待目标。发射时 `target_seqno = subq->kbase.emitted_jobs + 1`（`gpu_queue.c:554`）
  ⇒ emitted_jobs = 118，等第 119 个。`kbase_subqueue_wait_seqno()`（`:729`）在 10 s 看门狗
  （`KBASE_WAIT_TIMEOUT_NS`，`:60`）后放弃并 dump ⇒ 设备时间轴 12:20:59 OOM → 12:21:09 dump 与 10 s 吻合。
* `active`/`error` 等字段的值请以原始全文为准（本报告只拿到父级摘录）。

### 4.7 与 "tiler heap OOM" 是因果还是两件事？

* **能确定的**：两个事件时间上先 OOM（12:20:59）后 timeout（12:21:09），且 wrapper 的资源申报
  （idx5 `REQ_RESOURCE`: tiler+idvs）与 tiler heap 上下文装载（idx7-8）都在 `CALL` **之前**已完成
  （extract 已越过它们）⇒ **固件不是因为"没申报资源/没装载 heap"而停在 wrapper 里**。
* **不能确定的**：wrapper dump 里**没有任何**关于被 CALL 流内部状态的样本
  （没有 stream 内容、没有 scoreboard/异常寄存器、`stream progress` 因 KBASE_DIAG 关闭而恒 0）。
  因此"OOM 导致该流在 tiler 分配处死等/重试"只是**最省事的解释（推断，未验证）**，
  与"两个独立问题（OOM 只是伴随现象，真正的 hang 在流内某次 WAIT）"**无法用现有证据区分**。
* **要判定，下一轮必须拿到被 CALL 流的现场**：
  * 打开 `dev->debug.decode_ctx`（走 `gpu_queue.c:901-912`：`if (dev->debug.decode_ctx && insert > extract)`
    → 打 `kbase: csf timeout on subqueue …` + `pandecode_cs_binary(ringbuf_dev+last_last_job_offset,
    insert-last_last_job_offset, gpu_id)`），即把 **stream 也解出来**；
  * 或打开 `PANVK_DEBUG=kbase_diag`（`gpu_queue.c:573-583/601-611/620-629`），让
    `marks pre/post-call/post-wait`、`ls_copy`、`stream progress` 变成真值——**这次它们全 0 是这个 flag 没开，
    不是固件没动**（§2.5）；
  * 或 dump CS 异常/状态寄存器（`gpu_queue.c:800-810` 那条 `kbase: CS error …` 本次没有出现，
    说明**没有 CS 异常/故障页**，是"纯挂起"）。

### 4.8 这些行是 Mesa（用户态）打的，不是内核打印的

父级称"内核打印的 `last ring[0..15]`"，实际 tag 是 `E/MESA`，字符串与代码都在用户态：
`gpu_queue.c:838-880`（`mesa_loge`），数据来源是 ①`subq->kbase.ringbuf_cpu`（用户态映射的环）
②`subq->kbase.user_io + 8192` 处的 CS user-IO 输出页（`CS_USER_IO_OUTPUT_CS_EXTRACT`，`:344-349`）。
（内核 kbase 也有措辞相似的 dump，但字段名 `marks pre/post-call/post-wait` 是 PanVK 自己的 breadcrumb，
只在本树 `gpu_queue.c` 里。）

---

## 5. 未解码 / 未验证清单 + 参考位置

**未解码**：ring 第 7–16 个字（原文被截断为 `0x17…`，无法解码）。
**未验证**（需设备侧或下一轮）：
1. 本次实测**实际加载的 `.so`**（§2.4 命令；由此判定 A 的最终原因）。
2. `negotiated kernel uAPI` 的真实取值（v54 无法从本文档推出；先前 P1 之谜仍未定案）。
3. extract 停在 `CALL` 的两种读法哪种为真（§4.5）。
4. OOM→hang 的因果（§4.7）。
5. 设备 `scoreboard_slot_count` 是否确为 16（由 §4.3 数值反推，**推断**）。

**本报告用到的参考文件 / 行号**：
* 构建树源码：`/root/zenithblue/work/mesa/src/panfrost/lib/kmod/kbase_kmod.c`（:510, :518-519, :556, :586-594, :614, :620-625, :708, :872, :1588）；
  `.../vulkan/csf/panvk_vX_gpu_queue.c`（:58-60, :119-123, :126-138, :225-230, :341-349, :465-670, :729-930, :1025-1100, :2172-2215, :2705-2832, :3081-3120）；
  `.../vulkan/csf/panvk_cmd_buffer.h`（:278-290）；`.../vulkan/panvk_vX_device.c`（:312, :472-517, :536-547）；
  `.../vulkan/panvk_physical_device.c`（:370-392）；`.../genxml/cs_builder.h`（:1481-1490, :1508-1511, :2187-2196, :2231-2237, :2248-2256, :935-943）；
  `.../genxml/pan_pack_helpers.h`（:39-51）；`.../util/log.h`（:41-43, :76-80）；`.../util/log.c`（:113-145）。
* 生成头：`/root/zenithblue/build/android-v4/src/panfrost/genxml/v12_pack.h`（:1280-1325, :1771-1794, :1711-1715, :2846-2876, :3063-3095）。
* 设备侧 shim：`/root/mgl_icd/vkshim_mgl.c`（:42-53）。
* 前序报告：`research/16-p1-p2-implementation.md`（§2.1 objdump、§2.2 v53 md5）、`research/17-p2-implementation.md`（v54 三日志位置/指纹）、`research/14-tiler-heap-oom.md`、`research/12-probe-run-results.md`（:263 CSF fatal 样本）、`research/08-zl2-surface.md`（:191-200 FCL 加载路径）、`research/10-verify-probe.md`（:1608 /data/local/tmp push 流程）。
* 产物指纹（本轮复核）：v54 `a9cba64a…33f1`（20006408 B）、APK 内同名件一致；v53 `58ef996f…bec4`（20005600 B）。
