# 17 · v54 落地报告：接上 tiler heap renew（P2 / 修法 B）+ 版本与分支诊断日志

> 对象：构建树 `/root/zenithblue/work/mesa`（= `build/android-v4` 的来源树）；
> 产物 `build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so`。
> 基线：树内 v53 件 sha256 `58ef996f…bec4`（**未**回退到 v52）。
> 设备（**本轮完全未操作**）：OPPO PHZ110 / MT6989 / Immortalis-G720 MC12 / Android 16。
> 未改 `/root/mesa`、`/root/MobileGL`；未 adb；未碰三件套。
> 共改 **2 个文件**（`panvk_vX_gpu_queue.c`、`kbase_kmod.c`），未动 P1 逻辑，未做 P3–P6。

---

## 0. TL;DR

1. **P2 落地的不是 2 行而是 2 处**：报告 14 的 B1 只去掉 `:2792` 的前置条件**不够**——
   **计数器自增那一处（`:2782`）也被同一个死字段门控**，`kbase_tiler_submit_count` 永远是 0，
   去掉 `:2792` 后 `0 >= 128` 仍为假 ⇒ renew 依旧不发火。v54 同时打开了这两处门。
2. 新 `.so`：size `20006408`，md5 `5917807d13b52c593dd7f87b7b153244`，
   sha256 `a9cba64afa935370906e1a8533550816c6a018f4d6808cb8951dd4e1099333f1`；
   `ninja` 增量 **NINJA_EXIT=0**；SONAME/NEEDED/RUNPATH/FLAGS 与 v53 件**逐条相同**（只位移量变，见 §5）。
3. 诊断日志补了 **3 条 `mesa_logi`**（`mesa_logi` = `MESA_LOG_INFO`，**不是** `mesa_logd`，logcat 里
   一定出得来）：协商到的 uAPI 版本、1.6 兜底被采用（**上一轮这条路完全静默**）、renew 实际发火。
4. **P1 为何"看起来没生效"的最大嫌疑**：`kbase_kmod.c` 的 1.18 分支**成功/失败日志上一轮就已经有了**
   （`mesa_logi`/`mesa_logw` 各一条，本轮读取确认），真机却**一条都没有** ⇒ 最可能是
   **握手拿到的 uAPI 版本 < 1.18**，两个分支都被 `pan_kmod_driver_version_at_least()` 挡掉，
   静默落到 1.6（无 `csi_handlers`）。v54 的 `negotiated kernel uAPI %u.%u` 一行会一眼定案。
5. APK v54 已产出并逐位校验：`/root/final/mgl-panvk-v54.apk`（`versionCode=54`），
   `unzip -p … libvulkan_freedreno.so | sha256sum` = 新 `.so` 的 sha256（**20006408 字节，非空**）；
   manifest/env 与 v53 的 `xmltree` 逐字符 diff **只有 versionCode/versionName 两行**。

---

## 1. 改动 1／2：`src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c`（P2 本体）

**为什么必须改两处**（报告 14 B1 的遗漏）：

```c
2782:   if ((touched & graphics_mask) && submit->tiler_work_estimate) {   /* ← 门 1 */
2783:      queue->kbase_tiler_submit_count++;                            /* ← 永远不执行 */
...
2792:   if (submit->tiler_work_estimate &&                                /* ← 门 2 */
2793:       (queue->kbase_tiler_submit_count >= kbase_tiler_heap_renew_interval() || …
```

`submit->tiler_work_estimate` 全树无写入点（`panvk_cmd_buffer.h:600` 只声明、复合字面量清零），
所以门 1 让 `kbase_tiler_submit_count` 与 `kbase_tiler_work_count` 恒为 0；**只拆门 2** 的话
`0 >= 128` 仍为假、`renew_work && 0 >= 65536` 也为假 ⇒ 堆继续单调涨。**两处都拆才真正接上 renew。**

改后（逐字）：

```c
2777:    * no memory to reclaim.
         …（P2 说明注释）…
2788:   if (touched & graphics_mask) {
2789:      queue->kbase_tiler_submit_count++;
…
2802:   if (queue->kbase_tiler_submit_count >= kbase_tiler_heap_renew_interval() ||
2803:       (submit->tiler_work_estimate && renew_work &&
2804:        queue->kbase_tiler_work_count >= renew_work)) {
```

* 门 2 的第二条（work 阈值）**保留** `submit->tiler_work_estimate &&`：该字段现在恒 0 ⇒ 恒假，
  语义与上游注释一致，且**将来有人补上 producer 时自动恢复**（B2 的入口没被拆掉）。
* 只在 `touched & graphics_mask`（VERTEX_TILER|FRAGMENT）时计数，clear-only 的 fragment 提交
  仍不计（保留原注释的意图）；work 累加写 0 无害。
* 默认节奏：**每 128 次图形提交 renew 一次**（`KBASE_TILER_HEAP_RENEW_INTERVAL`，`:79`）。

## 2. 改动 2／2：诊断日志（version / 是否进入 renew / 1.6 兜底）

| 文件:行（改后） | 日志（`mesa_logi`，I/MESA） | 用途 |
|---|---|---|
| `kbase_kmod.c:516-519` | `kbase: CSF group_create: negotiated kernel uAPI %u.%u` | **实测协商版本**（P1 分支之谜的唯一判据） |
| `kbase_kmod.c:620-624` | `kbase: created CSF group %u with the 1.6 ABI (no csi_handlers: tiler OOM stays fatal)` | 上一轮这条路**静默**；补上后能立刻知道 flag 没送达 |
| `gpu_queue.c:2813-2820` | `kbase: tiler heap renewal (uAPI %u.%u, submits %u, renew interval %u)` | renew 是否真发火 + 发火时的计数 |

* `kbase_kmod.c` 已有的 1.18 成功/失败日志（`mesa_logi … (1.18 layout, ioctl 58)` /
  `mesa_logw("kbase: 1.18 CS_QUEUE_GROUP_CREATE failed…")`）**原样保留**，本轮确认它们在源码里存在。
* **P1 的判断逻辑一行没动**（`>=1.25` → `>=1.18` → `1.6` 阶梯与 `csi_handlers` 赋值不变）。

**完整 diff**：`/root/research/17-v54-gpu_queue.diff`（55 行）、`/root/research/17-v54-kbase_kmod.diff`（28 行）。

## 3. 备份 / 回滚（先备份后改，禁止 git checkout/stash/reset）

```bash
TS=1791172414
# 反改（回滚到 v53 树内状态）：
cd /root/zenithblue/work/mesa
cp -f src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c.bak-$TS src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c
cp -f src/panfrost/lib/kmod/kbase_kmod.c.bak-$TS        src/panfrost/lib/kmod/kbase_kmod.c
# 回滚后再增量重编：
cd /root/zenithblue/build/android-v4 && export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH && ninja
```

| 文件 | 改前 md5（= 树内 v53 现状） | 改后 md5 | 备份 |
|---|---|---|---|
| `panvk_vX_gpu_queue.c` | `ac84fc4c2fb8477e8a2a329043fea3e7`（与报告 14 指纹一致） | `b879e20c6f7c1a4d2775b00ae50d2077` | `.bak-1791172414` |
| `kbase_kmod.c` | `99aa9877cf05e99d668d93105325f53a` | `ec89b573846da51e34ba9d8302c80c56` | `.bak-1791172414` |
| 旧 `.so`（v53 件，保留） | — | sha256 `58ef996f…bec4` | `/root/v53/lib/arm64-v8a/libvulkan_freedreno.so` |
| 新 `.so`（另存一份） | — | sha256 `a9cba64a…33f1` | `/root/final/mgl-v54-libvulkan_panfrost.so` |

## 4. 编译证据（增量 ninja，exit 0）

```
[5/14]  Compiling … csf_panvk_vX_gpu_queue.c.o   (v10..v14 共 5 个变体)
[6/14]  Linking static target src/panfrost/lib/kmod/libpankmod_lib.a
[9/14]  Linking static target src/panfrost/vulkan/libpanvk_v12.a
[14/14] Linking target src/panfrost/vulkan/libvulkan_panfrost.so
NINJA_EXIT=0
```
完整日志：`/root/research/17-build.log`。唯一告警是**既有的**、与本次改动无关的
`panvk_vX_gpu_queue.c:818: warning: label followed by a declaration is a C23 extension`（5 次，v10–v14 各一次）。

**新 `.so`**：size `20006408`（v53 = 20005600，+808 B = 新增字符串），
md5 `5917807d13b52c593dd7f87b7b153244`，
sha256 `a9cba64afa935370906e1a8533550816c6a018f4d6808cb8951dd4e1099333f1`。

新增字符串已确认落在产物里（`strings -a`）：

```
kbase: CSF group_create: negotiated kernel uAPI %u.%u
kbase: created CSF group %u with the 1.6 ABI (no csi_handlers: tiler OOM stays fatal)
kbase: tiler heap renewal (uAPI %u.%u, submits %u, renew interval %u)
kbase: created CSF group %u with TILER_OOM CSI handler (1.18 layout, ioctl 58)   ← P1，仍在
kbase: 1.18 CS_QUEUE_GROUP_CREATE failed: %s; falling back to the 1.6 ABI        ← P1，仍在
```

## 5. `readelf -d` 与 v53 件逐条比对

`/root/research/17-readelf-v53.txt` vs `17-readelf-v54.txt`：
**7 条 NEEDED + SONAME + RUNPATH + FLAGS/FLAGS_1 的集合完全相同**（`TAG_SET_IDENTICAL`），
差异只有 **4 行地址型条目**（`Dynamic section at offset`、`PLTGOT`、`INIT_ARRAY`、`FINI_ARRAY`），
位移量统一 `+0x370`（= 新字符串把 tail 推后 880 B）：

```
0x0000000000000003 (PLTGOT)     0x1157858 -> 0x1157bc8
0x0000000000000019 (INIT_ARRAY) 0x1156b38 -> 0x1156ea8
0x000000000000001a (FINI_ARRAY) 0x1156b28 -> 0x1156e98
```

`NEEDED`：`liblog / libnativewindow / libsync / libm / libz / libdl / libc`；`SONAME: libvulkan_panfrost.so` —— 与 v53 **一致**。

## 6. APK v54

* 路径 `/root/final/mgl-panvk-v54.apk`，size `10187311`，
  sha256 `860d0780817ba4e2ffb95fb975f9b2ed02ad8b87687844ad4d7ac64a7f2644b6`；
  签名 CN=DSH Mali Driver（与 v53 同 keystore），`apksigner verify` 通过。
* 载荷三方校验（**非空**，逐字节等于源件）：

| APK 内条目 | 字节数 | sha256（APK 内 == 源件） |
|---|---|---|
| `lib/arm64-v8a/libvulkan_freedreno.so`（= 新驱动改名） | **20006408** | `a9cba64afa935370906e1a8533550816c6a018f4d6808cb8951dd4e1099333f1` == 新 `.so` |
| `lib/arm64-v8a/libMobileGL.so`（v53 件） | 16956584 | `72919c73a7e07630f329bb4ad9605c606a9aac9e2fc6596683ee7b9f9c76848b` |
| `classes.dex`（v53 件） | 1328 | `6bd3abde2c53506f1b54bb88a8069c3cc394c2fb08440e4ca475cbf8fd64f4ad` |

* **manifest/env 与 v53 逐字符一致**：`aapt2 dump xmltree` 全量 diff（`/root/research/17-v53-manifest.txt`
  vs `17-v54-manifest.txt`）**只有两行不同** —— `versionCode 53→54`、`versionName 5.3-p1-tiler-oom-csi
  → 5.4-p2-tiler-heap-renew`。`pojavEnv`/`boatEnv`（含 `MESA_DEBUG=1:PANVK_DEBUG=1:LIBGL_DEBUG=1:
  EGL_LOG_LEVEL=debug` 等调试 env）**一字未改**；**没有**加 `MESA_VK_WSI_HEADLESS_SWAPCHAIN`。
* 打包脚本留档 `/root/pack_v54.sh`（= `pack_v53.sh` 只改 versionCode/versionName/输出名/载荷来源目录）。

## 7. 发版前已知语义（供下一轮判断，不是 bug）

1. `kbase_renew_tiler_heap()` 若 `kbase_try_destroy_retired_heap()` 返回 false（上一代堆仍有
   firmware 可见引用），本次 renew **跳过**，但调用点**照旧把两个计数器清零** ⇒ 最坏情况
   变成"每 2×128 次图形提交才真正换堆"。要不要在跳过时不清零（即第 3 处改动），留给下一轮实测决定。
2. renew 会 `kbase_wait_graphics_targets()` 排空图形队列 + 建/销堆 context，属**同步开销**；
   若实测发现卡顿，可调 `PANVK_KBASE_HEAP_RENEW_INTERVAL`（本 APK 的 env 里没有，需下一轮加）。
3. 本轮**仍无法**在离线证明"堆不再 OOM"：renew 只保证每 128 次提交重置一次堆，
   "128 次提交内涨到 400 MiB"的极端几何仍可能 OOM。故 **P1 与 P2 必须同时在** —— 这也正是
   下一轮要一眼确认 1.18/1.6 走哪条的原因。

## 8. 下一轮真机该看什么（判据表）

按 logcat 出现顺序（`group_create` 在设备/队列初始化时、**启动早期**打印）：

| # | 看到 | 含义 | 下一步 |
|---|---|---|---|
| 1 | `I/MESA: kbase: CSF group_create: negotiated kernel uAPI X.Y` | 实测版本 | 若 `X.Y < 1.18` ⇒ P1 永远走不到，必须改打法（不能再靠版本门） |
| 2a | `I/MESA: … created CSF group N with TILER_OOM CSI handler (1.18 layout, ioctl 58)` | P1 **真正生效**（flag 已送达内核） | 观察 OOM 是否转成增量渲染、不再 DEVICE_LOST |
| 2b | `W/MESA: kbase: 1.18 CS_QUEUE_GROUP_CREATE failed: …` | ioctl 58 被内核拒（版本号够但布局不符） | 需拿到内核真头文件重定布局 |
| 2c | `I/MESA: … created CSF group N with the 1.6 ABI (no csi_handlers…)` | **flag 未送达**，tiler OOM 仍会杀组 | 说明版本门把两档都挡了（回到 #1 的 X.Y） |
| 3 | `I/MESA: kbase: tiler heap renewal (uAPI X.Y, submits 128, renew interval 128)` | **P2 发火**（期望：启动后每约 128 次图形提交出现一次，`submits` 恒为 128） | 若**始终没有**这行 ⇒ 拆门没生效或该场景无图形提交 |
| 4 | 期望**不再**出现 `E/MESA: kbase: CSF group 0 tiler heap OOM notification`，且 9 秒后**没有** `VK_ERROR_DEVICE_LOST (-4)` | P2 的最终目标 | 若仍 OOM：#3 有而 #4 不满足 ⇒ 128 次内涨满 400 MiB，需下调 interval（阈值标定） |

**注意**：`group_create` 的 1.18/1.6 日志是 `mesa_logi`（INFO）不是 `mesa_logd`（DEBUG），
抓日志务必**不要**用 `*:E` 之类把 INFO 滤掉。
