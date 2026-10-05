# 28 · v61 = 当前树（v60）+ v58 的两行缓存失效

> **对象**：OPPO PHZ110 / MT6989 / Immortalis-G720 MC12 / arch **v12** / Android 16 / 无 root / kbase CSF uAPI 1.21。
> **构建树**：`/root/zenithblue/work/mesa`（**不是** `/root/mesa`）；构建目录 `/root/zenithblue/build/android-v4`。
> **日期**：2026-10-05。
> **纪律**：未 `git checkout/stash/reset`（树内未提交改动原样保留）；未 `rm -rf` 任何目录；
> 未改 `/root/mesa`、`/root/MobileGL`；**未操作手机**（无 adb、无安装、未碰手机）。
> **标注**：【已定论】= 有 file:line 或命令输出支撑；【未验证】= 本机无证据（含全部真机行为）。

---

## 0. 一页结论

| 项 | 值 |
|---|---|
| 本轮唯一新增 | `panvk_vX_gpu_queue.c` **+2 行**，`kbase_subqueue_publish()` 内两次读 `*active` 前各一条 `kbase_cache_invalidate_range()` |
| 插入行号（改后） | **`:719`**（首读之前）、**`:723`**（硬件门铃后二次读之前） |
| diff 形态 | `718a719` / `721a723`，**纯新增 2 行、0 删除、0 修改**；全树仅此一个文件变化 |
| 源文件 sha256 | 改前 `f3bf4ea5…`（= v60 基线）→ 改后 `05a61202…` |
| **确定性对照** | **撤掉这 2 行重编 → `1335b5c0…`，20 007 840 B，与 v60 载荷 `.so` 逐位相同 ✔✔** |
| 可复现性 | 贴回 2 行，**两次独立重编**均得 `1915d16e…`，20 008 768 B，逐位一致 ✔ |
| 编译 | `ninja` **exit 0**（三次全部 0）；仅剩 1 类既有 warning（`-Wc23-extensions` @ `:820`，与本次改动无关） |
| 新 `.so` | `libvulkan_panfrost.so`，**20 008 768 B**，`sha256 1915d16e…`，`md5 0ee4ed6f…` |
| `readelf -d` | v60/v61 **同为 32 条**；`NEEDED`(8)/`SONAME`/`RUNPATH`/`FLAGS`/`RELASZ`/`RELACOUNT`/`PLTRELSZ`/`STRSZ`/`VERNEEDNUM`/`*ARRAYSZ` **逐条 identical**；仅 4 条绝对地址条目平移 `+0x3F0` |
| P5 状态 | **仍清零**：`strings <新 .so> \| grep -c "MTK AFBC"` = **0**（APK 内载荷同样 0）✔ |
| C1 状态 | **未动**：`panvk_vX_cmd_draw.c` sha256 `83356733…` = v60 原值 ✔ |
| 超时常量 | **未动**：`:63 #define KBASE_WAIT_TIMEOUT_NS (10ll * 1000000000ll)` = 10 秒 ✔ |
| APK | `/root/final/mgl-panvk-v61.apk`，10 191 407 B，`sha256 4c1443ed…`，versionCode **61**，versionName `6.1-c1-plus-cache-invalidate` |
| 载荷校验 | APK 内 `libvulkan_freedreno.so` = `1915d16e…`（= 新 `.so`，20 008 768 B ≠ 0）✔；`libMobileGL.so` = `72919c73…`、`classes.dex` = `6bd3abde…`，**均与 v54 逐字节相同** ✔ |
| manifest | 与 v54（`/root/research/17-v54-manifest.txt`）**只差 versionCode/versionName 两行**；`MESA_VK_WSI_HEADLESS_SWAPCHAIN` 命中 **0** ✔ |
| 回滚 | 见 §7，命令已实测（STEP B 即该命令的结果） |

---

## 1. 本轮唯一改动（`src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c`）

改动来源：`/root/research/25-fix.diff`（v58 的 diff）**只取其中两行**，其余（大段注释）**未采纳**。

改后 `kbase_subqueue_publish()` 的尾段（`:714-727`）：

```c
   kbase_gpu_wmb();
   uint8_t *output_page = (uint8_t *)subq->kbase.user_io + 8192;
   volatile uint32_t *active =
      (volatile uint32_t *)(output_page + CS_USER_IO_OUTPUT_CS_ACTIVE);

   kbase_cache_invalidate_range((const void *)active, sizeof(*active));   /* :719  首读之前 */
   if (*active) {
      *(volatile uint32_t *)subq->kbase.user_io = 1;
      kbase_gpu_wmb();
      kbase_cache_invalidate_range((const void *)active, sizeof(*active)); /* :723  门铃后二次读之前 */
      if (*active)
         return;
   }

   kbase_kmod_csf_queue_kick(dev->kmod.dev, subq->kbase.ringbuf_dev);
```

**逐字保持不变**：门铃写 `*(volatile uint32_t *)subq->kbase.user_io = 1;`、两次 `if (*active)`、
`return` 快速路径、以及末尾的兜底 `kbase_kmod_csf_queue_kick()` —— 一字未改。

`kbase_cache_invalidate_range()` 是**同文件 `:177` 的 static 函数**（`kbase_subqueue_wait_seqno()` 在 `:790` 已用它失效 seqno cell），
因此本改动**不需要任何新 include / 新声明**。

### 1.1 精确 diff（对照 `.bak-1791178351`，即 v60 基线）

```
718a719
>    kbase_cache_invalidate_range((const void *)active, sizeof(*active));
721a723
>       kbase_cache_invalidate_range((const void *)active, sizeof(*active));
```

行号复核（`grep -n`）：

```
177:kbase_cache_invalidate_range(const void *start, size_t size)      <- 定义
719:   kbase_cache_invalidate_range((const void *)active, sizeof(*active));   <- 新增·首读
723:      kbase_cache_invalidate_range((const void *)active, sizeof(*active));<- 新增·门铃后二次读
790:      kbase_cache_invalidate_range((const void *)cell, kbase_seqno_stride()); <- 既有
```

---

## 2. 改前备份

```bash
F=/root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c
cp -f "$F" "$F.bak-$(date +%s)"      # → $F.bak-1791178351
```

* 备份文件：`/root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c.bak-1791178351`
* 备份内容 sha256：`f3bf4ea51a582f736d6f6aea1286830996c1dc05f860aea2771e5a6b5864162c`
* 该哈希 **= v60 基线**（与既有的 `.bak-1791176223` 也完全相同，已 `diff -q` 确认逐字节一致）。

---

## 3. 构建（三次，全部 exit 0）

**前置陷阱（本轮踩到，务必记住）**：直接 `ninja` 会以
`/bin/sh: 1: aarch64-linux-android35-clang: not found` 失败（exit 1）。NDK **不在默认 PATH**：

```bash
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
cd /root/zenithblue/build/android-v4 && ninja src/panfrost/vulkan/libvulkan_panfrost.so
```

（该失败发生在编译阶段、未产出 `.o`，`libvulkan_panfrost.so` 当时保持 v60 的 13:22 版本未变，不污染任何结论。）

| 构建 | 触发 | 结果 |
|---|---|---|
| v61（首次） | 加 2 行 | `1915d16e…`，20 008 768 B，exit 0 |
| v61-repro1 | `touch` 源文件后重编 | `1915d16e…`，20 008 768 B，exit 0 ✔ 可复现 |
| **control（撤 2 行）** | `cp` 回 `.bak-1791178351` 后重编 | **`1335b5c0…`，20 007 840 B，exit 0** ✔✔ |
| v61-repro2 | 重新贴回 2 行后重编 | `1915d16e…`，20 008 768 B，exit 0 ✔ 可复现 |

日志：`/root/research/28-build-v61.log`、`28-work/28-build-v61-repro1.log`、
`28-work/28-build-control-nov58.log`、`28-work/28-build-v61-repro2.log`、`28-work/control-run.log`。

**warning**：三次均只有既有的一类 `-Wc23-extensions`（`panvk_vX_gpu_queue.c:820: label followed by a declaration`，
v60 时在 `:818`，因本次 +2 行平移 —— 与本次改动无关，v60 同样存在）。

---

## 4. ★确定性对照（本轮的硬判据）

```
### STEP B: CONTROL -- 撤掉这 2 行重编
source sha256 after revert:
f3bf4ea51a582f736d6f6aea1286830996c1dc05f860aea2771e5a6b5864162c   <- = v60 基线
OK: source byte-identical to v60 baseline bak
ninja_exit=0
1335b5c07ed28afd93f76edac4e5fc8a6341c5f74346fb57352a506cc4196330   20007840   <- 逐位等于 v60
```

| 项 | v60 载荷 | 撤 2 行后的对照产物 | 判定 |
|---|---|---|---|
| sha256 | `1335b5c0…` | `1335b5c0…` | **逐位相同 ✔** |
| 字节数 | 20 007 840 | 20 007 840 | 相同 ✔ |

⇒ **结论：撤掉这两行后，产物逐位等于 `1335b5c0…`（= v60）。**
这同时证明了两件事：
1. 本轮改动**只**引入了这 2 行（没有任何其他残留变量）；
2. `1335b5c0` 这个"v60"是可**确定性重建**的，v61 与它的差异**只**来自这 2 行。

反之，加回 2 行后两次独立构建都是 `1915d16e…`，说明 v61 产物本身也是确定性的。

---

## 5. `readelf -d` 与 v60 逐条比对

`readelf -d` 全文：`/root/research/28-work/28-readelf-v61.txt`（v60 版 `28-readelf-v60.txt`）。

* 两边**都是 32 条** dynamic entry。
* `diff` 只有 4 行不同，**全部是绝对地址**，且统一平移 `+0x3F0`（= 1008，来自 `.so` 体积 20 008 768 − 20 007 840 = 928 B 加上节对齐）：

| 条目 | v60 | v61 |
|---|---|---|
| Dynamic section offset | `0x11553f0` | `0x11557e0` |
| `PLTGOT` | `0x11580c8` | `0x11584b8` |
| `INIT_ARRAY` | `0x11573a8` | `0x1157798` |
| `FINI_ARRAY` | `0x1157398` | `0x1157788` |

* 抽取非地址标签后 **逐条 identical**（`diff -q` 无输出）：
  `NEEDED` ×8（liblog/libnativewindow/libsync/libm/libz/libdl/libc）、`SONAME`=libvulkan_panfrost.so、
  `RUNPATH`=`$ORIGIN/../../android_stub`、`FLAGS`=SYMBOLIC BIND_NOW、`FLAGS_1`=NOW、`RELA`/`RELASZ`(377664)/
  `RELAENT`/`RELACOUNT`(15730)/`JMPREL`/`PLTRELSZ`(5616)/`PLTREL`/`SYMTAB`/`SYMENT`/`STRTAB`/`STRSZ`/
  `GNU_HASH`/`INIT_ARRAYSZ`/`FINI_ARRAYSZ`/`VERSYM`/`VERNEED`/`VERNEEDNUM`/`NULL`。

⇒ **无任何链接时结构性变化**（没有新增/减少依赖，没有改 SONAME/RUNPATH）。

---

## 6. 纪律复核

| 检查 | 命令 | 结果 |
|---|---|---|
| P5 仍清零（源级） | `strings <新 .so> \| grep -c "MTK AFBC"` | **0** ✔ |
| P5 仍清零（字节级） | `grep -c "MTK AFBC" <新 .so>` | **0** ✔ |
| P5 仍清零（APK 内载荷） | `unzip -p <apk> …libvulkan_freedreno.so \| strings \| grep -c` | **0** ✔ |
| C1 未被触碰 | `sha256sum panvk_vX_cmd_draw.c` | `83356733df9d4001ace6f3705593eb5a155480d80e4ece6a4504f2ef0fd248dc` = v60 原值 ✔ |
| 超时常量未动 | `grep -n "define KBASE_WAIT_TIMEOUT_NS"` | `:63` = `10ll * 1000000000ll` ✔ |
| 全树仅 1 文件变化 | `diff` vs `.bak-1791178351` | 2 行新增，无其他文件被本轮写入 ✔ |
| 未用危险 git | — | 全程仅 `cp -f` + `python3` 改写，未 `git checkout/stash/reset` ✔ |
| 未 `rm -rf` | — | 仅 `rm -f` 针对 `/root/v61` 内自己的中间文件 ✔ |

---

## 7. APK `/root/final/mgl-panvk-v61.apk`

打包脚本：`/root/pack_v61.sh`（从 `pack_v60.sh` 逐字复制，仅改 `W=/root/v61`、versionCode/versionName、输出名、日志名）。
打包日志：`/root/research/28-pack-v61.log`。

* 文件：`/root/final/mgl-panvk-v61.apk`，**10 191 407 B**，`sha256 4c1443ed03fa23f9e6ed61db48cdf9d6ff9d10839e92b8c579f6980d4560291a`，`md5 efa577bd…`
* badging：`package=com.dsh.plugin.driver.g720`，**versionCode=61**，`versionName=6.1-c1-plus-cache-invalidate`，
  `minSdkVersion=26`，`targetSdkVersion=34`。

载荷（`unzip -p … | sha256sum`，**全部非空且等于源**）：

| APK 内条目 | 字节数 | sha256 | 来源比对 |
|---|---|---|---|
| `lib/arm64-v8a/libvulkan_freedreno.so` | 20 008 768 | `1915d16e…` | = 新 `.so` ✔ |
| `lib/arm64-v8a/libMobileGL.so` | 16 956 584 | `72919c73…` | = `/root/v54/…` ✔ |
| `classes.dex` | 1 328 | `6bd3abde…` | = `/root/v54/classes.dex` ✔ |

**manifest 与 v54 的 parity**（`/root/research/28-v61-manifest.txt` vs `/root/research/17-v54-manifest.txt`）：
`diff` **只有 versionCode/versionName 两行**，其余（`pojavEnv`/`boatEnv` 的完整 env 串、`renderer`、`des`、
`minMCVer`/`maxMCVer`、activity、intent-filter、meta-data 顺序与取值）**逐字符一致**；
`MESA_VK_WSI_HEADLESS_SWAPCHAIN` 命中 **0** ✔（未添加该变量）。

---

## 8. 回滚命令（已实测）

```bash
F=/root/zenithblue/work/mesa/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c
cp -f "$F.bak-1791178351" "$F"                       # 撤掉 2 行，回到 v60 基线
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
cd /root/zenithblue/build/android-v4 && ninja src/panfrost/vulkan/libvulkan_panfrost.so
sha256sum src/panfrost/vulkan/libvulkan_panfrost.so  # → 1335b5c0…, 20 007 840 B (= v60)
```

该命令的**结果已在本轮 STEP B 实测**：`ninja_exit=0`，产物 `1335b5c0…` / 20 007 840 B，与 v60 载荷逐位相同。
（如需同时回滚源文件与产物，执行上面三行即可；`.bak-1791178351` 在改动前已建立，内容从未被覆盖。）

---

## 9. 未验证 / 边界（不夸大）

* **真机行为全部未验证**：本轮未以任何方式操作手机（无 adb、未安装、未抓 logcat）。
  `v61` 是否解决 23 号定位的「门铃快速路径丢唤醒」（`timeout on subqueue` / `extract` 停在 wrapper 的 `CALL` 之前）
  **属于待真机验证的假设**，本报告不作断言。
* **`dc civac` 的语义安全**：`25-fix.diff` 注释主张「该 output page 全部由固件写、CPU 从不弄脏，故失效不会丢数据」。
  本轮**只照抄这两行代码**，未对该主张做独立取证（未读 firmware 侧、未跑一致性实验）。
* 本轮的"确定性对照"只证明**构建可复现且单变量**，不证明**语义正确**。
