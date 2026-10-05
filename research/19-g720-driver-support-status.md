# 19 号报告 — 「会不会是驱动本身有问题？」G720/panvk 支持状态的最终判定

面向：OPPO PHZ110（MT6989 / Immortalis-G720 MC12 / Android 16 / 无 root）上自编 Mesa panvk + kbase(CSF)
真实绘制 3–10 s 后 `tiler heap OOM notification` → 10 s → `VK_ERROR_DEVICE_LOST`。

- **本轮全部只读**：服务器侧只做 `ls/cat/grep/sed/git`；未编译、未改 `/root/mesa`、`/root/zenithblue`（含
  `work/mesa`、`build/`）、`/root/MobileGL`；未 `rm -rf`；**未操作手机**。唯一新增文件是本报告。
- 标注约定：**【已定论】**=源码/行号/正文直接支撑；**【推断】**=由已定论事实演绎；**未验证**=只提出假设或未取到证据。
- 报告日期：2026-10-05。

---

## 0. 一页结论（先读这段）

1. **前提纠错（本轮最重要的发现）**：**G720 = arch v12，不是 v14。** arch 14 是 2025 年的 **G1 系列
   （G1-Ultra / G1-Premium / G1-Pro）**。本机是 arch 12。⇒ 任务书里"arch14/CSF 路径本身未被验证"这个
   风险假设**不成立**（它是把两个世代弄混了）。**【已定论】**
2. **上游 panvk 官方支持 G720/v12**：Mesa **25.1** 起（release notes 明列 "Mali G720 and G725 on
   Panfrost and panvk"）；当前上游 `panvk` 对 **v10/v12/v13 直接放行**，**只有 v6/v7/v14 需要
   `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1`**。**【已定论，有 URL + 行号】**
3. **G720+kbase+CSF+panvk 在真机上已被独立项目跑通**：`wonderkast02/panvk-g720-kbase-csf`
   （Mali-G720 **MC8** / MT6899 / kbase uAPI 1.30 / r49p1 / Android），公开 Beta 0.1.0-beta.2，
   验证了 compute、graphics、offscreen、readback、MSAA、X11 WSI、AHB、tessellation、geometry shader。
   ⇒ **不存在"G720/arch12 的 CSF 路径天生不可用"这回事**。**【已定论，第三方一手文档】**
4. **我们的 kbase 后端就是从那个跑通的项目逐字拷来的**：`/root/zenithblue/patches/kbase-common/files/README`
   原文 "copied verbatim from https://github.com/wonderkast02/panvk-g720-kbase-csf branch
   `android-candidate-beta-1.9.4`, commit `3549264275…`"。**连 G720 专属的那条修复
   （`cs_reg_count` 从硬编码 96 改为由 CSF 接口推导）都在我们的树里。**⇒ "驱动主体"不是猜的。**【已定论】**
5. **真正未验证的是"我们这条具体装配线 + 我们这台具体变体"**，而不是架构：
   - `zenithblue` 项目自己声明：**只支持并验证了 Mali-G615（v11）**，G720/v12 是
     "**untested and unsupported**"；`profiles/g720-v12-csf.json` 状态 = `"planned P24"`。
   - 补丁目录**只有 `csf/` 和 `csf-v11/`，没有 `csf-v12/`** ⇒ G720 这条线拿不到任何 arch 定向补丁。
   - 跑通方的参考机是 **MC8 / kbase uAPI 1.30 / r49p1**；我们是 **MC12 / uAPI 1.21**（≈r42–r44）。
     参考项目自己写了 "**um resultado em MC8 != claim universal de G720**"、
     "Outras variantes da Mali-G720 permanecem experimentais até validação independente"。
6. **判定：(a) 驱动主体没问题、是移植/配置问题**（不是 (b) 也不是 (c)），但带一个必须说清的限定：
   (a) 成立的基础是"**v12 路径**已被公开验证"；**我们这条装配线（zenithblue 无 csf-v12）与我们这台变体
   （MC12/uAPI 1.21）仍未验证** ⇒ 剩余风险是**中等、可定位、可在本机证伪的**，不是"押错方向"。
   已定位的两条与 OOM 直接相关的移植缺陷（14 号报告 H1：uAPI 阶梯 1.25→1.6 **漏掉 1.18**，
   导致 `csi_handlers` 从未置位，任何 tiler OOM 都走 fatal；H2：`tiler_work_estimate` 生产者在我们的树里
   被删空，`kbase_renew_tiler_heap()` 成死代码）**都是移植缺陷，不是架构限制**。
7. **一步判定它的最小实验**：把 **wonderkast02 的 Beta 2 二进制**（公开 zip，SHA-256
   `fc1d69647c071ca3fe30ae2fb450e95c91c08e90779eb32c865fa384dff5aaca`）原样装到本机 PHZ110 上跑。
   - 若**它也**在几秒后 tiler heap OOM ⇒ 问题在**我们这台变体/这台内核（MC12 / uAPI 1.21）**的配置面，
     与 zenithblue 的补丁栈无关 ⇒ 把力气转向 uAPI 1.21 的 CSG/tiler 协商（H1/H2 正是这一类）。
   - 若**它能跑住** ⇒ 问题在 **zenithblue 的装配（缺 `csf-v12/`、kbase 文件从 beta-1.9.4 逐字拷入）** ⇒
     直接对齐那个 commit 的补丁集即可。
   这一条同时就是参考项目 ROADMAP 里"**validar outras variantes da Mali-G720**"那件事。**【推荐先做】**

---

## 1. 前提纠错：本机是 arch **v12**，不是 v14（**已定论**）

三条独立证据，全部指向 arch 12：

| # | 证据 | 出处 |
|---|---|---|
| 1 | Mesa 的 GPU 型号表里 **G720 只注册在 `PAN_PROD_ID(12, 8, 0)`**：`FIFTHGEN_MODEL(PAN_PROD_ID(12, 8, 0), 4, "G720", "G720", …)`；而 `PAN_PROD_ID(14, 8, 0/1/3)` 分别是 **"G1-Ultra"/"G1-Premium"/"G1-Pro"** | `/root/mesa/src/panfrost/model/pan_model.c:106`（G720）、`:108-112`（G1 系列） |
| 2 | 设备名 "Mali-G720 MC12" 只可能匹配上面那条 arch-12 记录（表里没有 arch-14 的 G720） | 探针实测 `phys[0]: "Mali-G720 MC12" vendor=0x13b5 device=0xc8700000 api=1.4`，`/root/research/probe10/out-device/l_tri.txt:15` |
| 3 | 若把探针报的 `deviceID=0xc8700000` 当 GPU_ID：`PAN_ARCH_MAJOR(x)=((x)&BITFIELD_RANGE(28,4))>>28 = 0xC = 12` | 定义见 `/root/mesa/src/panfrost/model/pan_model.h:25`、`pan_arch()` `:105-119`（**注**：`deviceID == 硬件 GPU_ID` 这一步是**【推断】**，但与 1、2 完全一致） |

旁证（我方仓库自己的命名）：产物目录叫 `dist/android-g720-v12-csf/`；
`profiles/g720-v12-csf.json` 里写死 `"panArch": 12`。

而 **arch 14 = G1 系列**，Mesa 文档写得清清楚楚：

```
| G720               | 5th Gen (v12) | 3.1 | 3.1 | 1.4 |
| G725               | 5th Gen (v13) | 3.1 | 3.1 | 1.4 |
| G1-Pro, G1-Premium | 5th Gen (v14) | 3.1 | 3.1 | 1.4 |
| G1-Ultra           |               |     |     |     |
```

出处：`/root/mesa/docs/drivers/panfrost.rst:31-38`；同一张表在线上
<https://docs.mesa3d.org/drivers/panfrost.html>（当前 main，已含 "G615, G715 | Valhall (v11)" 一行，
说明我们树里那份 docs 略旧，但 **v12=G720 / v14=G1 的对应关系一致**）。

**顺带纠两个任务书里的小错**：
- "先例 `/root/panvk-mtk` 面向 arch12 / G610" —— **G610 是 arch 10**（G310/G610 同属 Valhall v10），不是 12。
- "funnymdzz 那个 fork 的 kbase 分支拒绝 arch14" —— 那是**上游默认行为**（见 §2），不是它独有的拒绝；
  而且它与本机（v12）无关。

---

## 2. Q1：上游 panvk 对 G720 / v12 / CSF 的支持状态

### 2.1 官方支持：Mesa 25.1 起（**已定论**）

Mesa **25.1.0 Release Notes**（2025-05-07）"New features" 一节逐字列有：

> **Mali G720 and G725 on Panfrost and panvk**

出处：<https://gitlab.freedesktop.org/mesa/mesa/-/raw/main/docs/relnotes/25.1.0.rst>
（同批还列了 `VK_KHR_depth_stencil_resolve on panvk`、`Vulkan 1.2 on panvk/v10+` 等一大堆 panvk 新特性。）
第三方报道：<https://www.phoronix.com/news/Mesa-25.1-Newer-Mali-5th-Gen>
（标题即 "Mesa 25.1 Panfrost & PanVK Begin Supporting Newer Arm Mali 5th Gen Graphics"；
该站对本 agent 返回 403，仅作旁证，不作为主要依据）。

我们用的树 `VERSION` = `26.3.0-devel`，**远在 25.1 之后**，所以 v12 支持是在树内的。

### 2.2 上游的放行/门禁规则（**已定论，行号**）

`/root/mesa/src/panfrost/vulkan/panvk_physical_device.c:1001-1027`：

```c
switch (arch) {
case 6: case 7: case 14:                       /* ← v14 在这里，被门禁 */
   if (!os_get_option("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER")) {
      result = panvk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                            "WARNING: panvk is not well-tested on v%d, "
                            "pass PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 …", arch);
      goto fail;
   }
   break;
case 10: case 12: case 13:                     /* ← v12（我们）在这里，直接放行 */
   break;
default:
   … "%s not supported" …
}
```

⇒ **本机 v12 走的是"直接放行"分支，根本不需要任何环境变量。** 文档侧同步说明：

> On GPUs where PanVK support is experimental, the driver refuses to load by default.
> Setting `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` enables it. Experimental support comes with
> no guarantees: it may be broken, may require newer kernel driver versions, and may be removed.

出处：<https://docs.mesa3d.org/drivers/panfrost.html>（Support 段）。同页也说明 panvk
**只对 Mali-G610 声明 conformant**，其余（含 G720）都是非 conformant。

### 2.3 已知限制 / 未实现项（这就是本问题的"证据缺口"）

- **没有找到任何上游 issue/MR 记录 "panvk + v12/v13 + tiler heap OOM" 或 "VERTEX_TILER 未实现"。**
  多轮检索（`mesa gitlab issue panvk tiler heap OOM`、`panvk VERTEX_TILER limitation v12`、
  `panvk CSF 0xc3 TRANSLATION_FAULT tiler`）都只回到 `panvk_vX_gpu_queue.c` 源码镜像页与 Mesa 文档，
  **没有命中一条相关的 bug/限制条目**。⇒ 标注为 **"未找到公开记录"**（**不等于不存在**；
  fdo GitLab 的 issue 搜索需要登录/JS，本轮未能做穷尽检索 —— **未验证**）。
- 上游 panvk 的 CSF 提交路径（CSG 创建/注册/bind/kick、tiler heap INIT/TERM、TILER_HEAP 描述符）
  在我们树里是**上游原样**：13 号报告已核到 `panvk_vX_cmd_draw.c` 相对上游**只改了 1 行**
  （`panthor_kmod_get_csif_props` → `panvk_get_csif_props`），tiler 描述符生成逻辑未动
  （`/root/research/13-csf-exception-c3.md` §0 第 5 条）。⇒ **"是上游 tiler 代码写错了"这个方向没有正面证据。**
- **唯一一个有公开文档的 G720 专属缺陷是 kbase 侧的**（不是上游、不是 panthor）：
  `.cs_reg_count = 96` 硬编码 → `overflowed register file`；修法是改为由 CSF 接口推导。
  见 §4.2 ⇒ **而这条修复我们已经有了。**

---

## 3. Q2：`funnymdzz/mesa` 的定位 —— 它的验证目标是 **G710/arch 10**，不是 G720

| 项 | 值 | 出处 |
|---|---|---|
| remote | `https://github.com/funnymdzz/mesa.git` | `/root/mesa` `git remote -v` |
| HEAD | `6598829` "pan/lib: limit tessellation kernels to supported architectures"（2026-08-05，作者 funnymdzz） | 本地 `git log`；GitHub API 同一 SHA/日期 |
| 版本 | `26.3.0-devel` | `/root/mesa/VERSION` |
| **自我声明** | "A fork of Mesa … to add a **kbase backend** to Panfrost/PanVK … **The target device is the Google Pixel 7 (Mali-G710, Valhall/CSF, arch ≥ 10).** All custom work sits on top of upstream commit `e24dc5bd1e7`" | `/root/mesa/CLAUDE.md:5-9` |
| G710 是哪一代 | `VALHALL_MODEL(PAN_PROD_ID(10, 8, 2), 0, "G710", …)` ⇒ **arch 10** | `/root/mesa/src/panfrost/model/pan_model.c:88-90` |
| 近期提交主题 | `panvk/kbase: avoid duplicate submit barrier`、`panvk/kbase: remove diagnostics from the hot path`、`panvk/kbase: avoid redundant LS completion copy`、`panvk: add compute-backed tessellation support`、`Merge upstream Mesa main and enable KRAID for PanVK kbase` | GitHub API `repos/funnymdzz/mesa/commits` |

**结论（Q2）**：funnymdzz/mesa 是**活跃维护的 kbase 后端 fork**，但它公开声明的验证目标是
**Pixel 7 / G710 / arch 10**，**通篇没有声明 G720/arch 12**。它的树保留上游的 arch-14 门禁
（`case 14` 需 `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1`，见 §2.2 引的就是它的代码）。
⇒ "G720 是否被作者验证过"：**没有任何公开证据显示被验证过**（**未找到 ≠ 未发生**）。

**另一个必须纠正的点**：**这份 fork 并不是我们部署的那份驱动。** 我们部署的 `.so` 来自
`/root/zenithblue/`（见 §4），funnymdzz/mesa 在本次工作里扮演的是**对照 diff 的参考树**
（15 号报告就是这么用的），在 zenithblue 的 `sources.lock` 里它只被列在
`referenceRepos.history`（"secondary reference"），**且注明 "not build inputs"**。

---

## 4. Q3：`zenithblue` 这个产物到底是什么来历 —— **实验性拼装，且自称 G720 不受支持**

### 4.1 基线是**上游 Mesa**，不是 funnymdzz 的 fork（**已定论**）

```
/root/zenithblue/sources.lock:
  mesaRepo   = https://gitlab.freedesktop.org/mesa/mesa.git
  mesaCommit = 5a07217f034b3e50d8c7c7794f97a2df1742613b
  mesaVersion= 26.3.0-devel      mesaBranch = main      pinnedOn = 2026-09-18
  referenceRepos.g720-csf = { url: https://github.com/wonderkast02/panvk-g720-kbase-csf,
                              commit: d9cbb91e…,
                              role: "primary CSF / modern Valhall+5th Gen reference
                                     (pan_kmod arch, CSF lifecycle, AHB, sync, G720 tessellation)" }
  referenceRepos.history.funnymdzz = { url: https://github.com/leegao/mesa-funnymdzz,
                              note: "secondary reference" }
  notes: "Secondary history repos are intentionally unpinned; they are not build inputs."
```

构建树实测：`/root/zenithblue/work/mesa` 的 `origin` = `gitlab.freedesktop.org/mesa/mesa.git`，
`HEAD` = `5a07217f034`（"radv: slightly rework initializing the DCC predicate"）。
⇒ **产物 = 上游 Mesa 26.3-devel + zenithblue 补丁栈**，与 funnymdzz 的 fork 是**两条线**。

### 4.2 kbase 后端是**逐字拷贝**来的，且 G720 专属修复已在树内（**已定论**）

`/root/zenithblue/patches/kbase-common/files/README` 原文：

```
New Kbase backend files, copied verbatim from the reference below.
Provenance: https://github.com/wonderkast02/panvk-g720-kbase-csf
  branch android-candidate-beta-1.9.4, commit 3549264275c9663ed73e01d652f4c0d16f21df22
  (src/panfrost/lib/kmod/kbase_* + mali_base_* + mali_kbase_csf_registers.h)
Licenses preserved in-file: kbase_kmod.[ch] MIT (Copyright 2026 Collabora, Ltd. per reference);
  ARM UAPI headers GPL-2.0 WITH Linux-syscall-note.
kbase_kmod.c states target range r32p0-r44p0 (JM uAPI 11.x / CSF uAPI 1.x).
  Poco X6 Pro DDK version still to be confirmed at runtime (VERSION_CHECK).
apply-patches.sh copies this tree over the Mesa checkout AFTER applying *.patch files.
```

这些文件以 **untracked（`??`）** 形式落在构建树里（`git status` 可见：
`?? src/panfrost/lib/kmod/kbase_kmod.c`、`kbase_csf_uapi.h`、`mali_base_csf_kernel.h`、
`?? include/drm-uapi/mali_kbase_ioctl.h` …）—— 这也解释了为什么 14 号报告提醒
"回滚只能靠 `cp` 备份，不能 `git checkout --`"。

**G720 专属修复在我们的树里（逐条核对）**：

- `/root/zenithblue/work/mesa/src/panfrost/lib/kmod/kbase_kmod.c:453-455`
  ```c
  uint32_t stream_features = streams[0].features;
  uint32_t scoreboard_slot_count = (stream_features >> 8) & 0xff;
  uint32_t cs_reg_count = (stream_features & 0xff) + 1;   /* ← 不再是硬编码 96 */
  ```
- `.../panvk/csf/panvk_vX_gpu_queue.c:497-498`（另见 `:1613-1614`、`:1722-1723`）
  ```c
  .nr_kernel_registers = MAX2(csif_info->unpreserved_cs_reg_count, 4),
  ```

对照跑通方文档（`docs/KBASE_CSF.md`，见 §5.2）：这正是他们记录的那条 **"Correção CSF específica
observada na G720"**。⇒ **我们不是在瞎猜 v12 的寄存器文件划分，我们用的是跑通方针对 G720 发布的修法。**

### 4.3 但是：这条装配线**自己声明 G720 不上台面**（**已定论**）

| 证据 | 原文/值 |
|---|---|
| `zenithblue/README.md`「Supported GPUs」 | "**Only the Mali-G615** (Mesa `PAN_ARCH` v11 …) **is supported and validated** … Other Mali GPUs (including G610/v10, **G720/v12** and the Bifrost/Valhall v7/v9 JM parts) have planned profiles or patch scaffolding but are **untested and unsupported**." |
| 参考机 | Poco X6 Pro (MT6897) / **Mali-G615 MC6 / v11** / CSF / `/dev/mali0` |
| `profiles/g720-v12-csf.json` | `{"profile":"g720-v12-csf","gpu":"Mali-G720","panArch":12,"frontend":"CSF", …, "status":"**planned P24**"}` |
| 补丁目录 | `patches/` = `android app-loader bcn-layer common csf **csf-v11** jm-v9 kbase-common wsi` ⇒ **没有 `csf-v12/`** |
| 装配脚本 | `scripts/apply-patches.sh`：`SERIES="common android kbase-common app-loader wsi"`；`if CSF then SERIES="$SERIES csf csf-v$ARCH"`。对 `g720-v12-csf` 展开为 `… csf csf-v12`，而 **`csf-v12` 目录不存在** ⇒ 该 arch 拿不到任何定向补丁（只有通用 `csf/` 两个 patch） |
| 项目自己的架构文档 | `docs/MALI-GPU-ARCHITECTURES.md`："this repository supports and validates exactly **one** GPU, the Mali-G615 MC6 (Mesa `PAN_ARCH` **v11**)"；G720/v12 一行标 "**planned, not validated**" |

**文档漂移提醒（不改变结论，但要记账）**：拷来的 `kbase_kmod.c` 文件头仍写着
"Command submission (CSF queue groups / JM job atoms) is **not wired up yet**; this backend currently
only supports device enumeration and memory management."（`kbase_kmod.c:28-30`）——这与树里实际存在的
`kbase_kmod_csf_group_create()`（`:510-573`）矛盾，是**上游 Collabora 版文件头没跟着改**的残留，
**不是功能声明**。同类残留还包括头部的 "Targets … r32p0–r44p0"（对我们的 uAPI 1.21 恰好在范围内，
但对跑通方的 r49p1 反而**不在**范围内 —— 见 §5.3）。

---

## 5. Q4：同硬件上的公开先例（G720 / Immortalis 跑 panvk）

### 5.1 结论：**有，而且是全栈真机验证的**（**已定论**）

**`wonderkast02/panvk-g720-kbase-csf`**（项目名 "Drive G720 / PanVK"）
<https://github.com/wonderkast02/panvk-g720-kbase-csf>

| 项 | 值（取自其 README / docs） |
|---|---|
| 定位 | "Vulkan experimental sobre Kbase/CSF no Android"；"não declara conformidade Vulkan nem compatibilidade universal" |
| 参考平台 | MT6899 / **Mali-G720 MC8** / **GPU ID `0xc8700010`** / vendor `0x13b5` / Kbase-CSF / `/dev/mali0` / Android（min API 35） |
| 内核侧 | **Kbase r49p1**，观察到的 **uAPI/UK 版本 1.30**，固件 `mali_csffw.bin` |
| 公开版本 | **0.1.0-beta.2**；包 `PanVK-G720-0.1.0-beta.2.zip` SHA-256 `fc1d6964…`；内嵌 `libvulkan_panfrost.so` SHA-256 `126b8b61…`，21,497,392 B |
| 已验证特性 | Kbase/CSF 用户态、Vulkan 初始化、**compute**、**图形管线**、offscreen、CPU readback、纹理采样、depth/stencil/blend、**MSAA+resolve**、**X11 WSI/swapchain**、**Android AHB import**、**Tessellation**、**Geometry Shader**、Transform Feedback+GS、layered GS、GPU WAIT64 |
| 未声明/实验 | Vulkan conformant ❌；DXVK/D3D11 🧪；Winlator/Vortek 🧪 |
| **明确边界** | "**um resultado em MC8 != claim universal de G720**"；"Outras variantes da Mali-G720 permanecem **experimentais até validação independente**"；ROADMAP 里 "validar outras variantes da Mali-G720" 仍是**未勾选**项 |

⇒ 这就是问题 4 要的"公开先例"：**G720 + kbase + CSF 的 panvk 在真机 Android 上是通的**，
而且踩过的坑有具体记录。

### 5.2 他们踩的坑（一条，且与硬件架构直接相关）

`docs/KBASE_CSF.md`「Correção CSF específica observada na G720」逐字：

> A antiga suposição fixa `.cs_reg_count = 96,` podia terminar em **`overflowed register file`**.
> A correção passou a derivar a quantidade real informada pela interface CSF:
> `.cs_reg_count = (stream_features & 0xff) + 1,` … `.nr_kernel_registers = MAX2(csif_info->unpreserved_cs_reg_count, 4)`
> No dispositivo testado, `stream_features` implicava **aproximadamente 114 registradores**, eliminando o abort.

⇒ **我们的树已经有这条修复**（§4.2 已核到行号）。**所以"寄存器文件划分没适配 G720"这个嫌疑点已被排除。**

另一条他们写成项目铁律的经验，恰好点中我们的相反做法：

> 规则：**"Nunca converter `DRM_FORMAT_MOD_INVALID` em `DRM_FORMAT_MOD_LINEAR` por suposição."**
> （绝不把 `DRM_FORMAT_MOD_INVALID` 凭假设转成 `LINEAR`）

而 15 号报告 H5 核到：**我们的树恰恰走的就是"回退成 `DRM_FORMAT_MOD_LINEAR`"这条路**
（`vk_android.c` 的线性自描述回退块），与跑通方 903 行 patch 里的**相反策略**（回退到 MTK 实际 AFBC
modifier）相反。⇒ 这是一条**有第三方明确规劝、且我们做反了**的移植分歧（影响 AHB/合成/窗口化路径，
对 tri 这种内部 `vkCreateImage` 影响较小 —— 15 号报告 H5 已如此定性）。**【已定论（代码差集）+ 推断（影响面）】**

### 5.3 我们与跑通方的**关键差异**（这才是"未验证"的真正所在）

| 维度 | 跑通方（wonderkast02 Beta 2） | 我们（PHZ110） | 影响 |
|---|---|---|---|
| SoC / GPU | MT6899 / G720 **MC8** | MT6989 / G720 **MC12** | 变体不同；跑通方明确拒绝外推 |
| GPU ID | `0xc8700010` | `0xc8700000`（探针 deviceID） | 低 4 bit 不同 = 核数/变体位不同 |
| kbase DDK / uAPI | **r49p1 / 1.30** | uAPI **1.21**（推断 r42–r44；无 root 未取到 DDK 版本号） | **ABI 世代差一整档**；14 号报告 H1 正是 uAPI 阶梯问题 |
| 后端源码来历 | 自家主干 | **从对方 `android-candidate-beta-1.9.4` 逐字拷来** | 拷的是对方**历史分支**，不是 Beta 2 那条线 |
| 那段源码自称目标范围 | — | `kbase_kmod.c` 头部自称 **r32p0–r44p0** | 我们的 r42–r44 在范围内；**对方验证用的 r49p1 反而在其声明范围之外** ⇒ 这条注释与"跑通方验证过的版本"并不自洽（**未验证**到底哪边为准） |
| 装配/验证状态 | 公开 Beta + 真机特性矩阵 | **zenithblue 自称 G720 "untested and unsupported"，无 `csf-v12/`** | 我们这条线**没有 arch 定向补丁** |

### 5.4 其他 G720/Linux 侧旁证（**未深入验证，仅登记**）

- `Sky1-Linux/mesa-sky1` —— 仓库标题自称 "Mesa 3D graphics library — Sky1-Linux fork with
  **PanVK fixes for Mali-G720/Panthor**"（<https://github.com/Sky1-Linux/mesa-sky1>）。**注意**：
  Panthor（DRM）路线，与我们的 kbase 路线**不是同一 KMD**；README 取回 404，未读到正文。**未验证**。
- Mali-G720-Immortalis（**CIX CP8180**，Linux/UMA）上有真实的 Vulkan 应用栈痕迹：
  llama.cpp issue **#23057** "Vulkan: `GGML_ASSERT(descriptor_set_idx < descriptor_sets.size())` crash
  on ARM UMA (Mali-G720-Immortalis, CIX CP8180)"（<https://github.com/ggml-org/llama.cpp/issues/23057>）。
  这属于**应用侧 bug**，但证明"G720 上有人在跑 panvk/Vulkan"。**未验证**细节。
- 所有检索**都没有**命中"G720 + `tiler heap OOM notification`"的公开记录。⇒ 我们的这个症状
  在公开视野里**没有前例**（**未找到 ≠ 不存在**）。

---

## 6. Q5：判定

### 6.1 明确判断

> **(a) 驱动主体没问题，是我们这条具体装配线/这台具体变体的移植与配置问题。**
>
> **不是 (b)**：不存在"arch14/CSF 路径未被验证"这个风险 —— **本机是 v12，不是 v14**；v12 在
> 上游 Mesa 25.1 起就是**默认放行**的受支持架构，并且已被独立项目在真机全栈验证（compute/graphics/
> MSAA/tessellation/GS/WSI/AHB）。
> **不是 (c)**：没有找到任何与本案吻合的上游 bug 记录（尤其没有 v12 tiler heap 的条目）；
> 而且我们树里 tiler 描述符生成代码相对上游**几乎是原样**。
>
> **但 (a) 带限定**：(a) 的成立依赖"**v12 架构路径**已被验证"，而
> **"我们这台 MC12 + uAPI 1.21 + zenithblue 装配（无 `csf-v12/`）"这一组合仍然未验证**。
> 因此剩余风险是**中等、可定位、能在本机证伪的工程风险**，不是"押错方向"级别的风险。

### 6.2 支撑该判断的具体证据

**支持 (a)（驱动主体没问题）**
1. 上游：Mesa 25.1 release notes 明列 "Mali G720 and G725 on Panfrost and panvk"；
   `panvk_physical_device.c:1001-1027` 对 v12 **免环境变量直接放行**。
   → <https://gitlab.freedesktop.org/mesa/mesa/-/raw/main/docs/relnotes/25.1.0.rst>、
   `/root/mesa/src/panfrost/vulkan/panvk_physical_device.c:1015-1017`。
2. 真机前例：`wonderkast02/panvk-g720-kbase-csf` Beta 2 在 G720 MC8 + kbase/CSF 上验证了
   compute/graphics/offscreen/readback/MSAA/WSI/AHB/tessellation/GS
   → README、`docs/VALIDATION.md`、`docs/STATUS.md`。
3. **我们的 kbase 后端就是从 2 逐字拷来的**（`patches/kbase-common/files/README` 逐字给出
   repo + branch + commit `3549264275…`），**且已包含对方针对 G720 发布的 `cs_reg_count` 修复**
   （`kbase_kmod.c:453-455`、`panvk_vX_gpu_queue.c:497-498`）。
   ⇒ "驱动主体"与"G720 专属适配"两头都有第三方背书。
4. 现象侧也不像架构不通：clear/copy/render/交换链全对（像素精确），真机主界面+3D 能干净渲染约 10 s
   —— 架构级不兼容不会先给你 10 秒正确画面。

**支持"是移植/配置问题"（已定位到具体代码）**
5. H1（14 号报告 §0.2）：CSG 创建在 uAPI ∈ [1.18, 1.25) 掉进了**没有 `csi_handlers` 字段的 `_1_6`
   布局**（ioctl 42），导致 `BASE_CSF_TILER_OOM_EXCEPTION_FLAG` **从未告诉内核** ⇒ 内核把任何
   tiler OOM 都当 fatal：`term_queue_group()` + `report_tiler_oom_error()` ⇒ 正是我们抓到的那行
   `kbase: CSF group N tiler heap OOM notification`（`kbase_kmod.c:655-658`），随后 10 s watchdog
   `KBASE_WAIT_TIMEOUT_NS`（`gpu_queue.c:63`）→ `VK_ERROR_DEVICE_LOST`。
6. H2（14 号报告 §0.2）：`submit->tiler_work_estimate` 的**生产者在我们的树里被删空**
   （`panvk_vX_cmd_draw.c` / `panvk_vX_cmd_buffer.c` 零次赋值）⇒ `kbase_renew_tiler_heap()` 成死代码
   ⇒ heap 只增不换，涨到 `max_chunks` 后每次 OOM 都 fatal。**上游 kbase 的 uAPI 里根本没有
   `TILER_HEAP_GROW`（48=INIT、49=TERM，50 缺号）**，grow 只能靠内核 + 续期两条路。
7. H5（15 号报告）：`DRM_FORMAT_MOD_INVALID` 回退策略与跑通方的**明文铁律相反**
   （我们→LINEAR，对方→MTK 实际 AFBC modifier）。
8. 结构面：`patches/` 无 `csf-v12/`（`csf-v11/` 有），`profiles/g720-v12-csf.json` 状态 `planned P24`，
   项目 README 自称 G720 "untested and unsupported" ⇒ **这条线从未被本项目的验证阶梯覆盖过**。

**为什么不是 (b)**
9. arch 14 是 G1 系列（`pan_model.c:108-112`），Mesa 对 v14 **需要** `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1`
   （`panvk_physical_device.c:1003`）；本机 deviceName/GPU_ID 匹配的是 arch-12 的 G720 记录
   ⇒ (b) 的前提是错的。（**反过来说**：如果哪天真的要在 v14/G1 上开工，那才真是 (b)。）

**为什么不是 (c)**
10. 没有找到任何吻合的上游 bug 条目（§2.3）；我们树里 tiler 描述符/HEAP_SET 代码与上游几乎一致
    （13 号报告 §0 第 5 条：`panvk_vX_cmd_draw.c` 相对上游仅 1 行不同）。

### 6.3 **一步就能判定**的最小实验（按性价比排序）

**E1（决定性、零编译、强烈推荐先做）—— 用跑通方的二进制打我们的机器**

装 `wonderkast02` 的 **Beta 2**（公开 zip，SHA-256 `fc1d6964…`；内嵌 `libvulkan_panfrost.so`
SHA-256 `126b8b61…`）到 PHZ110，用现有探针工作流（`/root/research/probe10/runner.sh` 的形态）
跑 `render` + `tri` + 一次真实 3D 场景。

- **它也 OOM/掉卡** ⇒ 病灶在**我们这台变体/这台内核**（MC12 / uAPI 1.21 / MT6989 的 kbase 行为），
  与 zenithblue 补丁栈**无关** ⇒ 全面转向 uAPI 1.21 的 CSG/tiler 协商（H1/H2 正是这一类）。
- **它跑得住** ⇒ 病灶在 **zenithblue 的装配**（缺 `csf-v12/`、kbase 文件取自对方 beta-1.9.4 分支、
  以及 15 号报告那 10 条差异）⇒ 直接对齐对方 `980ac91d…`/`f1d7bed5…` 那条线的补丁集。
- 副作用：**这一次实验正好补上跑通方 ROADMAP 里那个未勾选项**（"validar outras variantes da Mali-G720"），
  所以无论结果如何都是有价值的一手证据。

**E2（已在做）—— 修掉 H1/H2 后重测**
16/17 号报告已实现 P1（1.18 布局 + `csi_handlers`）与 P2（恢复 `tiler_work_estimate`）。
判据不变：OOM 行消失/不再 fatal，或 `vkWaitForFences` 由 -4 回 0。

**E3（若 E1/E2 仍不收敛）—— 确认"G720 寄存器修复"在我们机器上真的生效**
把 `GLB iface` 的 `WORK_REGS/stream_features` 与协商到的 `driver.version` 打出来
（`gpu_queue.c:2104` 的 `mesa_logd` 现成、`kbase_kmod.c` 的 `VERSION_CHECK` 结果未打印），
核对 `cs_reg_count` 是否 ≈114（跑通方实测值）而不是走到 arch≥12 的 128 兜底。
**若打进兜底 ⇒ 说明我们对 uAPI 1.21 的 GLB iface 解析与跑通方（1.30）不同 ⇒ 直接指向配置面。**

**E4（可选，判定 OOM 归属）**：14 号报告 §6/§7 的 S1/S3（打印 `tiler heap desc base/top`、
扰动 `chunk_size` 看故障 VA 是否跟着动）。S3 的价值是判断故障地址是 heap chunk 还是固定结构。

---

## 7. 未验证清单（明确不装懂）

1. **本机 kbase 的真实 DDK 版本号**（无 root、本轮禁操作手机）；uAPI 1.21 系由既有报告推断为 r42–r44 一档。
2. **`deviceID == 硬件 GPU_ID`** 这一步是推断；arch 12 的结论主要靠"deviceName=Mali-G720 MC12 +
   型号表里 G720 只在 arch 12 注册"这两条，已足够但严格性弱于直接读 GPU_ID。
3. **fdo GitLab issue 的穷尽检索未做**（需登录/JS）⇒ "上游无相关 bug 记录"应读作"**本轮未检索到**"。
4. 跑通方 `android-candidate-beta-1.9.4`（我们逐字拷来的那个分支）与已验证的 Beta 2
   （`980ac91d…` / `f1d7bed5…`）之间**具体差多少补丁**，本轮未做 diff ⇒ 有"拷了旧分支"的可能。
5. `Sky1-Linux/mesa-sky1`（标题自称含 G720/Panthor 的 PanVK 修复）正文未取到（README 404）；
   且它走 Panthor，与我们 kbase 不同 KMD。
6. 拷贝来的 `kbase_kmod.c` 头部自称目标 **r32p0–r44p0**，而跑通方参考机是 **r49p1/1.30**
   ⇒ 该注释与参考机并不自洽；哪边为准 **未验证**。
7. 15 号报告 H4/H6/H7/H8/H10 各条对本案的实际贡献仍未单独实验。
8. 本轮未操作手机、未编译、未改任何受保护目录；只在 `/root/research/` 新增本报告。

---

## 8. 出处速查

**本机/本树（服务器，全部只读）**
- arch 映射与型号表：`/root/mesa/src/panfrost/model/pan_model.c:88-90`（G710=arch10）、
  `:106`（G720=arch12）、`:108-112`（G1 系列=arch14）；`pan_model.h:25,105-119`（`PAN_ARCH_MAJOR`/`pan_arch`）
- 上游门禁：`/root/mesa/src/panfrost/vulkan/panvk_physical_device.c:1001-1027`
- 上游文档表：`/root/mesa/docs/drivers/panfrost.rst:31-38`；在线 <https://docs.mesa3d.org/drivers/panfrost.html>
- fork 自我声明：`/root/mesa/CLAUDE.md:5-9`；`/root/mesa/VERSION`（26.3.0-devel）
- 设备实测：`/root/research/probe10/out-device/l_tri.txt:15,28-33`；
  `/root/research/12-probe-run-results.md:19,21`
- zenithblue 基线/参考：`/root/zenithblue/sources.lock`、`scripts/fetch-mesa.sh`、
  `/root/zenithblue/work/mesa`（`git remote`/`git log`/`git status`）
- zenithblue 支持面：`README.md`（Supported GPUs / Reference device）、
  `profiles/g720-v12-csf.json`、`patches/README.md`、`scripts/apply-patches.sh`、
  `docs/MALI-GPU-ARCHITECTURES.md`
- **kbase 后端来历**：`/root/zenithblue/patches/kbase-common/files/README`
- G720 修复在树内：`/root/zenithblue/work/mesa/src/panfrost/lib/kmod/kbase_kmod.c:453-455`；
  `.../vulkan/csf/panvk_vX_gpu_queue.c:496-498,1613-1614,1722-1723`
- 文件头漂移：`.../kbase_kmod.c:1-30`（"not wired up yet"、"r32p0–r44p0"）
- 既有分析：`/root/research/13-csf-exception-c3.md` §0、`14-tiler-heap-oom.md` §0.2/§3/§5、
  `15-panvk-mtk-diff.md` §1/§2/§5、`16-p1-p2-implementation.md`、`17-p2-implementation.md`

**公网**
- Mesa 25.1.0 release notes（"Mali G720 and G725 on Panfrost and panvk"）：
  <https://gitlab.freedesktop.org/mesa/mesa/-/raw/main/docs/relnotes/25.1.0.rst>
- Mesa Panfrost/PanVK 支持表 + 实验性开关说明：<https://docs.mesa3d.org/drivers/panfrost.html>
- Phoronix（旁证）：<https://www.phoronix.com/news/Mesa-25.1-Newer-Mali-5th-Gen>
- **G720 + kbase + CSF panvk 跑通方**：<https://github.com/wonderkast02/panvk-g720-kbase-csf>
  （`README.md`、`docs/STATUS.md`、`docs/VALIDATION.md`、`docs/KBASE_CSF.md`）
- funnymdzz/mesa 提交历史：<https://github.com/funnymdzz/mesa>（API：
  `api.github.com/repos/funnymdzz/mesa/commits`）
- G720/Panthor 旁证（**未验证**）：<https://github.com/Sky1-Linux/mesa-sky1>；
  <https://github.com/ggml-org/llama.cpp/issues/23057>
