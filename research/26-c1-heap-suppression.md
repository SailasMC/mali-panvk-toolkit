# 26 · C1：kbase 上抑制逐 render pass 的 tiler-heap 操作（只留 `cs_vt_start`）→ **v59**

> **对象**：OPPO PHZ110 / MT6989 / Immortalis-G720 MC12 / arch **v12** / Android 16 / 无 root / kbase CSF uAPI 1.21。
> **依据**：`/root/research/22-vertex-corruption-arch12.md` ★ C1（最高优先级）。
> **本轮定名**：**v59 = v58 + C1 合并版**（日后归因请一律写作 "v58+C1"）。
> **纪律**：未 `git checkout/stash/reset`（树内 62 项未提交改动原样保留）；未 `rm -rf`；
> 未改 `/root/mesa`、`/root/MobileGL`；**未操作手机**（无 adb、无安装、未碰红线三件套）。
> **本轮新增写入**：`/root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c`（唯一改动的源文件）、
> `/root/research/26-*`、`/root/pack_v59.sh`、`/root/v59/**`、`/root/26-libvulkan_panfrost.so`、`/root/final/mgl-panvk-v59.apk`。
> **标注**：【已定论】= 有 file:line 或命令输出；【推断】= 演绎；**未验证** = 未取到证据/无法在本机（不碰手机）验证。

---

## 0. 一页结论

| 项 | 值 |
|---|---|
| 改动文件 | `src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c`（**唯一**） |
| 语义 | kbase（`phys_dev->kbase_node_path[0] != '\0'`）上**不再发** `cs_vt_end` / `cs_finish_fragment` / `cs_frag_end`，**不再注册/撤销 TILER_OOM 异常处理器**；`cs_vt_start()`(`:1444`) **保持不变** |
| 新增判据 | `cmdbuf_skips_gpu_heap_ops()`，定义 `:253-262`（注释 `:244-252`） |
| 补丁来源 | `/root/research/vertex-work/mesa-suppress-kbase-heap-ops.diff`（39/-13 净 +7 行 hunk 内容；`git apply -p1 --check` → **通过**，本轮**逐字照用**，未按 §5 校准） |
| 确定性对照 | 撤掉 C1 → 增量重编 → `.so sha256 = ac198f58…`，**20 011 416 B = v58 基线逐位相同** ✔ |
| 可复现性 | 重新 `git apply` C1 → 再编 → `sha256 b918a45f…`，**20 012 600 B 再次逐位相同** ✔（两次独立构建一致） |
| 编译 | `ninja` **exit 0**（12 步；v10/v11/v12/v13/v14 五个 arch 全部重编 `csf_panvk_vX_cmd_draw.c`，**0 warning**） |
| 新 `.so` | `libvulkan_panfrost.so`，**20 012 600 B**，`sha256 b918a45f…`，`md5 7fca6e1a…` |
| `readelf -d` | v58 与 v59 **同为 32 条 dynamic entry，非地址条目逐条 identical**；`NEEDED` 8 条 / `SONAME` / `RUNPATH` 完全一致；仅 `PLTGOT`/`INIT_ARRAY`/`FINI_ARRAY` 的绝对偏移随体积 +1184 B 平移（见 §4） |
| APK | `/root/final/mgl-panvk-v59.apk`，10 191 407 B，`sha256 e42abdfb…`，versionCode **59**，versionName `5.9-c1-kbase-heap-suppress` |
| 载荷校验 | `unzip -p … libvulkan_freedreno.so \| sha256sum` = `b918a45f…` = 新 `.so`，**bytes = 20 012 600 ≠ 0** ✔；`libMobileGL.so`/`classes.dex` 与 **v54 原物逐字节相同** ✔ |
| manifest | 与 v54（`/root/research/17-v54-manifest.txt`）**只差 versionCode/versionName 两行**；`MESA_VK_WSI_HEADLESS_SWAPCHAIN` 命中 **0** ✔ |
| C1 与 v58 改动是否重叠 | **互不重叠**：C1 只碰 `panvk_vX_cmd_draw.c`，v58 只碰 `panvk_vX_gpu_queue.c`（不同文件、无共同 hunk）✔ |
| 回滚 | 见 §7（单文件 `cp` 还原 + `ninja`，已实测回到 `ac198f58`） |

---

## 1. C1 具体改了什么（post-patch 行号，`panvk_vX_cmd_draw.c`）

`v59` 相对 `v58` 的全部差异 = 下面 6 处（`+39/-13`，其中 19 行是新增注释+helper）：

| # | 行号（改后） | 被抑制/保留的东西 | 改动形态 |
|---|---|---|---|
| 1 | **`:244-262`** | 新增注释 `:244-252` + `static inline bool cmdbuf_skips_gpu_heap_ops(const struct panvk_cmd_buffer *)` `:253-262`，判据 = `phys_dev->kbase_node_path[0] != '\0'`（与 `panvk_device.h:160/180`、`panvk_vX_device.c:312` 同一判据） | 纯新增 |
| 2 | **`:4225-4226`** | `cs_vt_end(b, cs_defer_indirect())`（= VERTEX_TILER_COMPLETED，`PAN_ARCH >= 11` 路径；原 `:4206`） | 加 `if (!cmdbuf_skips_gpu_heap_ops(cmdbuf))` 守卫 |
| 3 | **`:4243-4244`** | `cs_vt_end(b, cs_defer(SB_WAIT_ITER(x), SB_ID(DEFERRED_SYNC)))`（`PAN_ARCH < 11` 路径；原 `:4223`） | 同上（同一 `cs_match_iter_sb` 循环内） |
| 4 | **`:4529-4537`** | TILER_OOM 异常处理器**注册**：`calc_tiler_oom_handler_idx()` / `handlers_bo->addr.dev` / `cs_move64_to` / `cs_move32_to` / `cs_set_exception_handler(..., MALI_CS_EXCEPTION_TYPE_TILER_OOM, ...)`（`cs_set_exception_handler` 改后 `:4535`；原 `:4513`） | 整块包进 `if (!cmdbuf_skips_gpu_heap_ops(cmdbuf)) { … }` |
| 5 | **`:4552-4553`** | TILER_OOM 异常处理器**撤销**（`addr=0,len=0` 的 `cs_set_exception_handler`；原 `:4529`） | 加同一守卫 |
| 6 | **`:4659-4662`** | 新增**空分支** `if (cmdbuf_skips_gpu_heap_ops(cmdbuf)) { /* Heap chunks are reclaimed by the queue's wholesale heap renewal instead of FINISH_FRAGMENT / FRAGMENT_COMPLETED. */ } else if (td_count == 1) {…} else if (td_count > 1) {…}` ⇒ 顺带抑制 `cs_finish_fragment()` `:4664`(td==1) 与 `:4668`(td>1)、`cs_frag_end()` **`:4674`**（原 `:4637`/`:4641`/`:4647`）。三者都在 `else` 链内，kbase 恒取首分支。 | `if (td_count == 1)` → `if (skip) {} else if (td_count == 1)` |

**保持不变（C1 的关键"只留"）**：`cs_vt_start(b, cs_now())` `:1444`（原 `:1425`），无任何守卫。✅ 与报告 C1 的一句话定义逐字吻合。

### 1.1 代码注释中给出的机理（本轮照抄，未改一字）

```
kbase runs the vertex/tiler and the fragment work in separate CS groups and
this driver replaces the firmware's per-render-pass tiler-heap protocol with
a wholesale heap renewal (kbase_renew_tiler_heap()).  Emitting
VERTEX_TILER_COMPLETED / FRAGMENT_COMPLETED / FINISH_FRAGMENT on top of that
makes the firmware recycle heap chunks (and the kernel validate statistics)
against a heap generation that no longer matches the stream that produced
them -- i.e. the tiler can read position/polygon data out of chunks that were
released while still in use.  Only VERTEX_TILER_STARTED is kept: the kernel's
tiler-OOM path rejects requests with no render pass in flight.
```

与报告 §0.4 的旁证一致：我们树 `:4620-4623` 的自有注释本就承认
`FINISH_FRAGMENT` 是"释放 heap chunk"的操作（"required to guarantee that used heap chunks
won't be released prematurely"）——却被我们无条件发出。

### 1.2 与上游 `main` 的关系（本轮只陈述，不改）

`panvk_vX_cmd_draw.c` 相对 `HEAD` 的改动现在 = **1（既有树内基线 `:107 panthor_kmod_get_csif_props` → `panvk_get_csif_props`）+ C1 的 6 处**。
C1 的 helper 是**新增的 kbase 判据**，上游 `main` 没有它（上游 tiler-heap 协议走 panthor，不需要）。

### 1.3 一个必须记住的语义取舍（**未验证**，留待真机）

抑制 TILER_OOM 处理器意味着：kbase 上若发生真实 tiler heap 溢出，**不会**走进我们注册的
handler，而是**保持 pending**（`cs_set_exception_handler` 的 disable 语义本就是为了"留给下一个
renderpass 重新注册"）。跑通方在 kbase 上就是这么做的（报告 §0.4），因此这是**有意为之**，
但它是 v59 唯一一处"从有到无"的鲁棒性下降点：真机若出现 tiler OOM，表现将是**作业挂住/超时**
而不是被 handler 救回。§6 给出观测建议。

---

## 2. 补丁落盘与合法性

```
cd /root/zenithblue/work/mesa
git apply -p1 --check /root/research/vertex-work/mesa-suppress-kbase-heap-ops.diff   # → OK（无输出）
cp -f src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c \
      src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c.bak-1791177353                     # 备份（先备份后打补丁）
git apply -p1 /root/research/vertex-work/mesa-suppress-kbase-heap-ops.diff          # → OK
```

- 备份原文件 sha256：`6b4c154983a8d7481f0d7a5e6a48a525a8f978c58ca9f84d50e4c8a28b24ef94`
- 补丁副本：`/root/research/26-work/mesa-suppress-kbase-heap-ops.diff`
- 实际 diff：`/root/research/26-work/c1.diff`
- `git diff --numstat` 该文件：**`40  13`**（= 既有 1/-1 基线 + C1 的 39/-12；见 §1 表）
- `git status --porcelain | wc -l`：**62 → 63**，增量**只有**一个未跟踪备份文件
  `?? src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c.bak-1791177353` ⇒ **未新增任何非预期改动** ✔

---

## 3. 编译

```
cd /root/zenithblue/build/android-v4
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
ninja            # NINJA_EXIT=0
```

尾部（`/root/research/26-build-v59.log`）：

```
[1/22] Generating src/git_sha1.h with a custom command
[2/12] Compiling C object src/panfrost/vulkan/libpanvk_v12.a.p/csf_panvk_vX_cmd_draw.c.o
[3/12] Compiling C object src/panfrost/vulkan/libpanvk_v11.a.p/csf_panvk_vX_cmd_draw.c.o
[4/12] Compiling C object src/panfrost/vulkan/libpanvk_v10.a.p/csf_panvk_vX_cmd_draw.c.o
[5/12] Linking static target src/panfrost/vulkan/libpanvk_v10.a
[6/12] Linking static target src/panfrost/vulkan/libpanvk_v11.a
[7/12] Linking static target src/panfrost/vulkan/libpanvk_v12.a
[8/12] Compiling C object src/panfrost/vulkan/libpanvk_v13.a.p/csf_panvk_vX_cmd_draw.c.o
[9/12] Linking static target src/panfrost/vulkan/libpanvk_v13.a
[10/12] Compiling C object src/panfrost/vulkan/libpanvk_v14.a.p/csf_panvk_vX_cmd_draw.c.o
[11/12] Linking static target src/panfrost/vulkan/libpanvk_v14.a
[12/12] Linking target src/panfrost/vulkan/libvulkan_panfrost.so
```

- `NINJA_EXIT=0`，**无 error**；`grep -n 'cmd_draw'` 只命中编译/链接行，**该文件 0 warning**。
  （v58 那唯一一条 `-Wc23-extensions` 属于 `gpu_queue.c:825`，本轮该文件未重编。）
- 五个 arch（v10–v14）全部重编 ⇒ 新判据在**所有 arch 上都能编译**，不是 arch12-only hack。

---

## 4. 确定性对照（本轮最强证据）

| 步骤 | 源码状态 | `.so` sha256 | 大小 |
|---|---|---|---|
| ① v59 构建 | **v58 + C1** | `b918a45fc96be2bea9722b861a76b87e0bed388b89c1bb60a9a00c79f56ca8ad` | 20 012 600 B |
| ② **撤销 C1**（`cp` 回 `.bak`）→ `ninja`（`NINJA_EXIT=0`） | = v58 | **`ac198f581a76669eca2688f6c1a723fc8505a1c1af66bf506d9c5f4ae5013c2d`** | **20 011 416 B** |
| ③ 重新 `git apply` C1 → `ninja`（`NINJA_EXIT=0`） | = v58 + C1 | **`b918a45f…` 再次逐位相同** | 20 012 600 B |

⇒ 结论三连：
1. **② == 任务给定 v58 基线（`ac198f58`，20 011 416 B）逐位相同** ⇒ 树内当前**没有**任何未记录的
   额外改动，C1 就是 v58→v59 的**唯一** delta；
2. **构建确定性成立**（同源再编得同哈希）；
3. **C1 的字节级影响面被唯一确定**：`Δsize = +1184 B`。

日志：`/root/research/26-build-control-noc1.log`（无 C1）、`/root/research/26-build-v59-reapply.log`（再应用）。

`readelf -d` 逐条比对（`/root/research/26-readelf-v58.txt` vs `/root/research/26-readelf-v59.txt`）：

```
2c2   Dynamic section at offset 0x1156130 → 0x1156620   (32 entries → 32 entries)
21c21 (PLTGOT)     0x1158e08 → 0x11592f8
28c28 (INIT_ARRAY) 0x11580e8 → 0x11585d8
30c30 (FINI_ARRAY) 0x11580d8 → 0x11585c8
```

- **只有 3 条"数据段绝对地址"条目平移**（随 +1184 B 体积），标签名/顺序/条数（**32**）不变；
- `NEEDED`(liblog/libnativewindow/libsync/libm/libz/libdl/libc) + `SONAME(libvulkan_panfrost.so)` +
  `RUNPATH($ORIGIN/../../android_stub)` **逐条 identical**（`TAGS_IDENTICAL: yes`）。
  ⇒ v59 与 v58 的**加载契约完全一致**，v59 不需要任何新的运行时依赖。

---

## 5. APK（`/root/final/mgl-panvk-v59.apk`）

打包脚本：`/root/pack_v59.sh`（= `pack_v58.sh` 的 v59 版，唯一差异 = `versionCode 59` / `versionName` /
工作目录 `/root/v59` / 输出名）。日志 `/root/research/26-pack-v59.log`。

```
=== payload entries
 16956584  lib/arm64-v8a/libMobileGL.so
 20012600  lib/arm64-v8a/libvulkan_freedreno.so
     1328  classes.dex
=== payload sha256 (must equal source; must be NON-EMPTY)
b918a45f…  -                                          ← in-APK libvulkan_freedreno.so
b918a45f…  /root/zenithblue/build/.../libvulkan_panfrost.so   ← 新 .so（相等 ✔）
72919c73…  -                                          ← in-APK libMobileGL.so
72919c73…  /root/v54/lib/arm64-v8a/libMobileGL.so      ← v54 原物（相等 ✔）
6bd3abde…  -                                          ← in-APK classes.dex
6bd3abde…  /root/v54/classes.dex                       ← v54 原物（相等 ✔）
=== payload byte counts inside apk (must be non-zero)
libvulkan_freedreno.so bytes = 20012600   ← 非空 ✔
libMobileGL.so      bytes = 16956584
classes.dex         bytes = 1328
=== apk
10191407 B   sha256 e42abdfb129bb9a6d707502ab671e9132eb861cb15fe7164efb1f5431a88aefe
=== badging
package: name='com.dsh.plugin.driver.g720' versionCode='59' versionName='5.9-c1-kbase-heap-suppress'
minSdkVersion:'26'  targetSdkVersion:'34'
=== manifest parity vs v54 (diff 输出，只此两行)
< ...:versionCode=54      > ...:versionCode=59
< ...:versionName="5.4-p2-tiler-heap-renew"
> ...:versionName="5.9-c1-kbase-heap-suppress"
=== MESA_VK_WSI_HEADLESS_SWAPCHAIN 命中数 = 0 ✔
```

- manifest dump：`/root/research/26-v59-manifest.txt`；v54 参照 `/root/research/17-v54-manifest.txt`。
- `env`（`pojavEnv` / `boatEnv` 两条 `meta-data`）与 v54 **逐字符相同**，未新增任何变量。

---

## 6. 真机观测建议（**未验证**，交给父代理/下一轮）

1. **主判据**：v59 vs v58 在同一存档/同一路径下，世界几何是否从"巨大三角/斜切板条"变正常。
2. **回归哨兵（对应 §1.3）**：
   - `dmesg | grep -i 'tiler\|oom'` 或 logcat 里的 `kbase` / `panvk` 报错；
   - 若出现 **"timeout on subqueue"** / `extract halted, insert > extract` 且**伴随几何恢复正常**，
     则说明"抑制后不再回收 chunk"生效，而新的瓶颈是堆容量 → 下一轮应控制
     `kbase_renew_tiler_heap()` 的换新节奏，而不是恢复 `FINISH_FRAGMENT`；
   - 若出现**作业长时间挂住**且 `PANVK_DEBUG=1` 下有 TILER_OOM 相关 pending 异常，则印证 §1.3 的取舍点。
3. **采样**：`cs_vt_start` 保持不变，因此逐 pass 的"tiling 开始"信号仍在，便于与 v58 的 ring dump 对齐。

---

## 7. 回滚（已实测可用）

**A. 源码层回滚（源文件 → v58）**
```bash
cd /root/zenithblue/work/mesa && \
cp -f src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c.bak-1791177353 \
      src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c && \
cd /root/zenithblue/build/android-v4 && \
PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH ninja
# 预期：.so sha256 = ac198f581a76669eca2688f6c1a723fc8505a1c1af66bf506d9c5f4ae5013c2d（20 011 416 B）
```
（等价写法：`cd /root/zenithblue/work/mesa && git apply -R -p1 /root/research/vertex-work/mesa-suppress-kbase-heap-ops.diff`）
**上面 ② 步已实测：回滚后重编逐位得回 `ac198f58…`。**

**B. 产物层回滚（驱动/插件 → v58）**
```bash
adb install -r /root/final/mgl-panvk-v58.apk      # 或直接在插件面板重装 v58
# v58 载荷 libvulkan_freedreno.so sha256 = ac198f58…（已复核，见 26-work/v58.so.sha256）
```

**C. 单文件 .so 回滚（临时替换，不重编）**
```bash
# v58 驱动原物（20 011 416 B）：
#   在 v58 apk 内：unzip -p /root/final/mgl-panvk-v58.apk lib/arm64-v8a/libvulkan_freedreno.so
# v59 驱动副本（20 012 600 B）：/root/26-libvulkan_panfrost.so
```

---

## 8. 交付物清单

| 路径 | 内容 |
|---|---|
| `/root/research/26-c1-heap-suppression.md` | 本报告 |
| `/root/final/mgl-panvk-v59.apk` | **v59 插件 APK**，versionCode 59，`sha256 e42abdfb…` |
| `/root/26-libvulkan_panfrost.so` | v59 驱动 `.so` 副本，`sha256 b918a45f…`，20 012 600 B |
| `/root/research/26-work/libvulkan_panfrost-v59.so` | 同上（证据目录副本） |
| `/root/research/26-work/v59.so.sha256` `.md5` `.size` | v59 三元组 |
| `/root/research/26-work/v58.so.sha256` | v58 基线哈希 |
| `/root/research/26-work/control-noc1.so.sha256` | 确定性对照（无 C1）哈希 = `ac198f58…` |
| `/root/research/26-work/cmd_draw.pre-C1.sha256` | 打补丁前的源文件哈希 = `6b4c1549…` |
| `/root/research/26-work/c1.diff` | `git diff` 实际落盘 diff |
| `/root/research/26-work/backup.txt` | 备份文件名（含时间戳） |
| `/root/research/26-build-v59.log` | v59 编译日志（exit 0） |
| `/root/research/26-build-control-noc1.log` | 对照编译日志（exit 0） |
| `/root/research/26-build-v59-reapply.log` | 再应用编译日志（exit 0） |
| `/root/research/26-readelf-v58.txt` / `26-readelf-v59.txt` | `readelf -d` 全文 |
| `/root/research/26-readelf-v58.tags` / `26-readelf-v59.tags` | 关键 dynamic tag（identical） |
| `/root/research/26-v59-manifest.txt` | v59 manifest xmltree dump |
| `/root/research/26-pack-v59.log` | 打包 + 载荷校验日志 |
| `/root/pack_v59.sh` | 打包脚本（可重放） |
| `.../panvk_vX_cmd_draw.c.bak-1791177353` | **改动前源文件备份（回滚用）** |

---

## 9. 风险与下一轮

1. **未验证**：v59 的真实图形效果（本机不碰手机）——本报告只保证"改动正确落盘 + 确定性重编 + 打包校验"。
2. **C2 未做**（按任务指示留 v60）：上游 open MR **!44173** "wait for prior tiling work before reusing tiler heap"，
   补丁 `/root/research/vertex-work/mesa-mr44173-tiler-heap-wait.diff`。它与 C1 **正交**（C1 关掉的是
   "逐 pass 的结束/回收操作"，C2 补的是"复用共享 tiler heap 前先等上一批 tiling 退休"），
   **可在 v59 之上直接叠加**。
3. **C3 未做**（低）：`patch_vs_attribs()` 基准实例累加（报告 §0.3 第二点）。
4. 若 v59 让几何恢复正常但引入新超时，请优先按 §6.2 的判据区分"堆容量"与"异常处理器缺失"两条路径，
   下一轮再决定是调 `kbase_renew_tiler_heap()` 节奏还是恢复 handler（**不建议**恢复 `FINISH_FRAGMENT`）。
