# 24 · fix1 实施报告（无条件 kick 调度器）→ v57

> **对象**：真机 OPPO PHZ110 / MT6989 / Immortalis-G720 MC12 / Android 16 / 无 root / kbase CSF uAPI 1.21。
> **上游依据**：`/root/research/23-completion-timeout-crash.md` §3.1 / §5.1 / §6 Fix 1；草稿
> `/root/research/queue-work/fix1-always-kick.patch`（`patch --dry-run` APPLIES-CLEAN，本轮复核一致）。
> **纪律**：未 `git checkout/stash/reset`；未 `rm -rf`；未改 `/root/mesa`、`/root/MobileGL`；
> 未操作手机（无 adb、无安装、未触碰三件套）。改前逐文件 `cp` 备份（见 §4）。
> **本轮新增写入**：`/root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c`（唯一改动的源文件）、
> `/root/research/24-*`、`/root/pack_v57.sh`、`/root/v57/**`、`/root/v57-libvulkan_panfrost.so`、
> `/root/final/mgl-panvk-v57.apk`、`/root/v56-libvulkan_panfrost.so.bak-1791176223`。

---

## 0. 一页结论

| 项 | 值 |
|---|---|
| 改动源文件 | `src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c`（唯一） |
| 改动处 | **:713-733**（删掉用户门铃快速路径 → 无条件 `kbase_kmod_csf_queue_kick()`）+ **:63-68**（`KBASE_WAIT_TIMEOUT_NS` 10 s → 120 s） |
| 是否一并改了超时常量 | **是**（先例 B 的同一补丁 `panvk_mtk.patch:180-207` 同时含这两项，报告 §5.1 逐字引用；父代理明确要求"若 patch 含此附带项请一并实施"）。该第二项可用 §4 的单行回滚单独撤销 |
| 编译 | `ninja` **exit 0**（增量，12/12 步，仅 1 条既有 `-Wc23-extensions` 警告，见 §3） |
| 新 `.so` | `libvulkan_panfrost.so`，20 009 968 B，`sha256 3a76cce8…`，`md5 62258baa…` |
| v56 基线 | 20 010 648 B，`sha256 d0476a0c…`（**与任务给定前缀一致**） |
| APK | `/root/final/mgl-panvk-v57.apk`，10 187 311 B，`sha256 d0362a49…`，versionCode **57** |
| 内嵌驱动校验 | `unzip -p … libvulkan_freedreno.so \| sha256sum` = `3a76cce8c1c4abf27b49d57582ca3096501d0e46c73a0e19f0748777b05e66d2` = 新 `.so` ✔；`bytes=20009968 ≠ 0` ✔ |
| 未做 | fix2（`csi_handlers`）、fix3（诊断日志）、`tiler_work_estimate` 生产者、`cell->error` 判定 —— **一行未动**（§7 有复核命令） |

---

## 1. 改了什么（file:line，前/后）

### 1.1 删除用户门铃快速路径（fix1 本体）

`src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c`，`kbase_subqueue_publish()`：

| | 行号 | 内容 |
|---|---|---|
| 改前 | **716-725**（10 行） | `if (*active) { 写硬件门铃页; kbase_gpu_wmb(); if (*active) return; }` |
| 改后 | **716-732**（注释 17 行） | 无分支，直接落到 `:733` 无条件 `kbase_kmod_csf_queue_kick()` |

改后（`:713-733`）：

```c
   *(volatile uint64_t *)(input_page + CS_USER_IO_INPUT_CS_INSERT) =
      subq->kbase.insert;

   /* Always kick the scheduler.  The userspace doorbell fast path can race
    * an active->idle transition and drop the newly inserted job, leaving the
    * GPU idle with pending work until the wait watchdog gives up.
    * ... (先例出处 + 缓存失效缺失说明，全文见 /root/research/24-fix1.diff) */
   kbase_kmod_csf_queue_kick(dev->kmod.dev, subq->kbase.ringbuf_dev);   /* :733 */
}
```

* 删除块与先例 B（`BkaNeko/panvk-mtk-driver` `patches/panvk_mtk.patch:189-207`）**逐字相同**；
  替换后的注释即报告 §5.1 引用的那句 `Always kick the scheduler. … until the wait watchdog gives up.`
* 顺带消失的局部变量 `output_page` / `active` 不再有未使用告警（编译器无新告警）。

### 1.2 附带项：等待超时 10 s → 120 s

| | 行号 | 内容 |
|---|---|---|
| 改前 | :63 | `#define KBASE_WAIT_TIMEOUT_NS  (10ll * 1000000000ll)` |
| 改后 | **:68** | `#define KBASE_WAIT_TIMEOUT_NS  (120ll * 1000000000ll)`（上方 5 行注释行 63-67） |

**理由与风险（如实记录）**：
* 报告 §5.1 逐字展示：先例 B 的**同一个补丁**在删快速路径的**同一 hunk 组**里还把该常量抬到 120 s
  ⇒ "与已跑通先例逐字对齐"这一目标要求两项一起做。父代理指令亦如此。
* 草稿 `fix4-timeout-120s.patch` 自带注释写的是"仅缓解、不要单独上"——本轮**不是单独上**（fix1 已同树），
  但它确实把 10 s 崩溃窗口拉长为 120 s，会改变 A/B 的观测量（症状出现时间）。**若父代理希望保留
  10 s 的短诊断窗口，用 §4 第 3 条单行回滚即可，不需要重新打 fix1。**
* 该常量只影响 `kbase_subqueue_wait_seqno()` 的用户态看门狗（`:766`），不改变任何提交/唤醒语义。

### 1.3 diff 全文

`/root/research/24-fix1.diff`（`diff -u` 备份 vs 现状，2 个 hunk，未截断）。

---

## 2. 基线与本次产物指纹

| 产物 | 路径 | size | sha256 | md5 |
|---|---|---|---|---|
| v56 基线 `.so`（备份） | `/root/v56-libvulkan_panfrost.so.bak-1791176223` | 20 010 648 | `d0476a0c155af5debbd9cf3a7eee4b8aaab1b6a70bb1947901c2a959c6a9dd1a` | `99b8f9872ff6bafeb411fbe1b9dd2052` |
| **v57 新 `.so`** | `/root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so` | **20 009 968**（−680） | **`3a76cce8c1c4abf27b49d57582ca3096501d0e46c73a0e19f0748777b05e66d2`** | `62258baacdba02ebc2a9d72ba9d87427` |
| v57 `.so` 副本 | `/root/v57-libvulkan_panfrost.so` | 同上 | 同上 | 同上 |
| **v57 APK** | `/root/final/mgl-panvk-v57.apk` | 10 187 311 | **`d0362a49dc9a17c6c00ea2d8cdca347d7f1e29b09358aa47424aab430a1b26ee`** | — |

基线核对：任务给定"树内基线 = v56，新 .so sha256 前缀 `d0476a0c`" ——**实测一致**，未回退到 v52/v53/v54。

### 2.1 可复现性对照（证明 Δ 只有 fix1，排除"顺手带上别的改动"）

```
① 用备份源码覆盖 → ninja → sha256 = d0476a0c155af5…  ← 与 v56 基线逐位相同（确定性构建）
   日志 /root/research/24-build-v56-recheck.log  exit=0
② 贴回 fix1 源码     → ninja → sha256 = 3a76cce8c1c4ab…  ← 与首次构建逐位相同
   日志 /root/research/24-build.log              exit=0
```

⇒ 该构建树是**确定性**的，v57 相对 v56 的二进制差异**只可能**来自本报告 §1 的两个 hunk。

---

## 3. 编译证据

```
cd /root/zenithblue/build/android-v4
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
ninja src/panfrost/vulkan/libvulkan_panfrost.so
→ NINJA_EXIT=0        （/root/research/24-build.log 末尾： "[12/12] Linking target …libvulkan_panfrost.so"）
```

唯一告警（**改动前既有**，非本次引入）：

```
../../work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c:825:7: warning:
  label followed by a declaration is a C23 extension [-Wc23-extensions]
```

（`:825` 的 `int64_t now = os_time_get_nano();` 是 `kbase_wait_continue:` 标签后的声明，v56 亦同。）

### 3.1 ELF 对照（相对 v56）

* `readelf -d`：**32 条目，两侧条目名/值集合完全相同**（RUNPATH / 7 个 NEEDED / SONAME
  `libvulkan_panfrost.so` / FLAGS `SYMBOLIC BIND_NOW` …）；仅 `Dynamic section at offset`、
  `PLTGOT`、`INIT_ARRAY`、`FINI_ARRAY` 的**偏移**因体积缩小而下移 −0x210。
  全文：`/root/research/24-readelf-old.txt` vs `24-readelf-new.txt`。
* `readelf -S` 差异**只有**：`.text`（0x7296d8 vs 0x7298b8，−480 B）、`.eh_frame`、
  `.relro_padding`（NOBITS，−3568 B，即 `size` 里 bss 变小的来源）、`.symtab`、`.strtab`。
  **无** `.data` / `.rodata` 变化 ⇒ 纯代码尺寸缩减，与"删掉一段分支代码"一致。
* `readelf -h`：ELF64 / AArch64 / DYN。

---

## 4. 备份与回滚

| 备份 | 内容 |
|---|---|
| `/root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c.bak-1791176223` | **改动前的 v56 源码**（122 818 B） |
| `/root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c.v57-fix1` | 改动后的 v57 源码副本（123 711 B） |
| `/root/v56-libvulkan_panfrost.so.bak-1791176223` | **v56 二进制**（20 010 648 B，`d0476a0c…`） |

回滚命令（**推荐第 1 条**；均不需要 `git`，不会碰到 58 个未提交改动）：

```sh
# 1) 源码整体回 v56，并增量重编回 v56（产物 sha256 应为 d0476a0c…）
cp -f /root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c.bak-1791176223 \
      /root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c
cd /root/zenithblue/build/android-v4 && export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH && \
  ninja src/panfrost/vulkan/libvulkan_panfrost.so

# 2) 源码回滚后如需立刻拿回二进制、不重编：
cp -f /root/v56-libvulkan_panfrost.so.bak-1791176223 \
      /root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so

# 3) 只撤销"超时 120 s → 10 s"这一项（保留 fix1）：
sed -i 's/#define KBASE_WAIT_TIMEOUT_NS  (120ll \* 1000000000ll)/#define KBASE_WAIT_TIMEOUT_NS  (10ll * 1000000000ll)/' \
      /root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c   # 然后重跑 ninja

# 4) 反向核对（现状 == v57 源码）：
#    sha256 /root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c
#    = 647e26c9a4d4f6e6f170bf57ab8daf75b24a9b9925ec2faa05095b8cde2f2d25
```

---

## 5. APK v57（打包配方 `/root/pack_v57.sh`）

* **载荷**：新 `libvulkan_panfrost.so` → 改名 `lib/arm64-v8a/libvulkan_freedreno.so`；
  **v54 原物** `libMobileGL.so`（`sha256 72919c73…`）与 `classes.dex`（`sha256 6bd3abde…`）。
* **manifest**：与 v54 **逐字符一致**，只改 `versionCode 54→57`、`versionName → "5.7-fix1-always-kick"`。
  自动核对：`aapt2 dump xmltree` 的 v57 与 v54（`/root/research/17-v54-manifest.txt`）
  **只差这两行**；与 v56 亦只差这两行。`pojavEnv` / `boatEnv` 调试串保持原样，
  **`MESA_VK_WSI_HEADLESS_SWAPCHAIN` 命中数 = 0（已确认不存在）**。

### 5.1 校验（必查项，全部通过）

```
=== payload entries ===
 16956584  lib/arm64-v8a/libMobileGL.so
 20009968  lib/arm64-v8a/libvulkan_freedreno.so      ← 非空、尺寸正确
     1328  classes.dex
=== 内嵌驱动 sha256（权威校验）===
unzip -p /root/final/mgl-panvk-v57.apk lib/arm64-v8a/libvulkan_freedreno.so | sha256sum
  3a76cce8c1c4abf27b49d57582ca3096501d0e46c73a0e19f0748777b05e66d2   == 新 .so ✔
  bytes = 20009968  （≠ 0 ✔，未重演空载荷事故）
=== 另两项载荷与 v54 原物逐字节相同 ✔
=== zip 完整性：No errors detected ✔
=== badging：versionCode='57'  versionName='5.7-fix1-always-kick' ✔
=== apksigner verify：Signer #1 CN=DSH Mali Driver，SHA-256 eba50950… ✔
```

---

## 6. 上机建议（不在本报告执行范围内）

1. 只上 v57，单独存 logcat；关注 `kbase: timeout on subqueue` / `vk_queue_set_lost` 是否消失。
2. **判据**：报告 §3.1 的"若删掉快速路径后 10 s 超时消失 ⇒ 结论 (a) 成立"。
   ⚠️ 因为本轮同时把超时抬到 120 s，**"10 秒超时消失"已不是有效判据**——必须看
   120 s 内是否还出现 `timeout snapshot`（若 120 s 才崩，说明 kick 未修好，只是窗口变长）。
   若需要严格复现报告 §3.1 的判据，请用 §4 第 3 条把常量退回 10 s 再编一版。
3. 若 v57 无效 ⇒ 下一轮 A/B 上 fix2（`csi_handlers` → 0），**不要**在同一棵树上叠加。

---

## 7. 明确"没做的事"（复核命令）

| 未做 | 复核 | 结果 |
|---|---|---|
| fix2：`csi_handlers` 未改动 | `grep -n csi_handlers src/panfrost/lib/kmod/kbase_kmod.c` | `:552` / `:582` 仍为 `BASE_CSF_TILER_OOM_EXCEPTION_FLAG`；该文件 mtime `2026-10-05 11:53:54`（本轮未触碰） |
| fix3：未加诊断日志 | `grep -n 'goto kbase_wait_continue' …gpu_queue.c` | `:812` 仍在，`cell->error` 吞掉逻辑原样 |
| `tiler_work_estimate` | `grep -c tiler_work_estimate …gpu_queue.c` | 6 处，与 v56 相同（本轮未增删） |
| 未动其它树 | `git status --porcelain` | 58 项（本轮只新增 1 个 `.bak` 未跟踪文件；无 tracked 文件被 checkout/stash/reset） |
| 未操作手机 | — | 本轮无任何 adb / 安装 / 三件套操作 |

---

## 8. 证据文件清单（均在 /root/research/）

`24-fix1-kick-implementation.md`（本文）、`24-fix1.diff`、`24-build.log`、`24-build-v56-recheck.log`、
`24-readelf-old.txt`、`24-readelf-new.txt`、`24-sec-old.txt`、`24-sec-new.txt`、
`24-v57-manifest.txt`、`24-ts.txt`；打包脚本 `/root/pack_v57.sh`。
