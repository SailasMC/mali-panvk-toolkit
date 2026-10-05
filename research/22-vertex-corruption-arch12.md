# 22 号报告 — G720(arch **v12**) 上「几何/顶点数据被读错」的定位

面向：OPPO PHZ110（MT6989 / Immortalis-G720 **MC12** / arch **12** / Android 16 / 无 root），
自编 Mesa panvk（kbase/CSF）+ MobileGL DirectVulkan 跑 Minecraft，**能进主界面、FPS 51、UI 正常，
但世界几何是巨大的三角/斜切板条**。

- 报告日期：2026-10-05
- 纪律：**未操作手机**（无 adb 安装/无 UI 操作/未触碰红线三件套）；
  **未改** `/root/mesa`、`/root/zenithblue`（含 `work/mesa`、`build/`）、`/root/MobileGL`；
  未 `rm -rf` 任何既有目录；Mesa 树只做 `cat/grep/sed/diff/git` 与**读入内存的语法检查**
  （`clang -fsyntax-only`，不产生任何对象文件）。
  新增写入仅在 `/root/research/22-vertex-corruption-arch12.md` 与 `/root/research/vertex-work/**`。
- 标注：【已定论】= 有源码/行号/命令输出直接支撑；【推断】= 由已定论事实演绎；
  **未验证** = 未取到证据或无法在本机（不碰手机）验证。

---

## 0. 一页结论（TL;DR）

1. **前提纠错（本轮最重要的结论）：顶点属性描述符（stride/format/offset/divisor）不是差异点。**
   我们树的整条顶点输入链路
   （`emit_vs_attrib()` `csf/panvk_vX_cmd_draw.c:270`、`prepare_vs_driver_set()` `:326`、
   `emit_varying_descs()` `:461`、`patch_vs_attribs()` `:3189`、unbound 属性 `:360`）
   **与上游 Mesa `main`（截至 2026-10-02）逐字相同**；`git diff` 显示我们对该文件只有 **1 行**改动
   （`:107` 的 `panthor_kmod_get_csif_props` → `panvk_get_csif_props`）。⇒
   **不存在"v12 走错分支"的属性打包代码**（下面 §2 给了逐条 arch12 分支的清单与核对结果）。
   【已定论】

2. **"跑通方"的身份被查清，并且它就是同一棵树的后代**：`wonderkast02/panvk-g720-kbase-csf`
   的 tag `0.1.0-beta.2`（commit `f1d7bed571766c49e5dd464f92d1fda264612311`，父提交 `980ac91de74d`）
   **是一个完整 Mesa 分支**（不是只有 kbase 补丁），其 `panvk_vX_cmd_draw.c` = 我们的文件
   **+ GS/曲面细分 + xfb + poly heap** 等特性，**− 我们树上游 9 月的 CRC/继承渲染改动**；
   而且它的注释里引用了**我们自己的报告编号**（"P2 / report 14 fix B"）。⇒
   该仓库实为**本项目另一条（可用）分支**，可直接当 oracle 用。
   已用发布二进制的**符号级**证据核对：`libvulkan_panfrost.so` 含 `launch_tess_stages`/
   `launch_gs_stages`/`prepare_poly_heap`/`prepare_vertex_shader`（**只有 ref 树才有**），
   且**不含** `render_needs_crc_patch`/`prepare_layer_count_inherited_ctx`（**只有我们树才有**）。
   【已定论（符号级）】

3. **顶点属性侧与跑通方的全部差异只有两处，而且我们这侧"更新/更保守"**：
   - unbound 属性描述符：我们写 `table=17` 特化 ATTRIBUTE（上游 `6eccd73966 "panvk/v10+: Fix
     unbound LD_ATTR.auto32"`，2026-08-14，**比跑通方新**），跑通方写 `NULL_DESCRIPTOR`；
   - `patch_vs_attribs()`：跑通方**无条件重写绝对 offset**，我们**只在 `firstInstance != 0` 时才从
     描述符里读出旧值再加一次**（`cmd_draw.c:3208-3278`）。
   ⇒ 第一处对我们有利；第二处**只在"间接绘制 + 逐实例属性 + 同一命令缓冲重复提交"时才错**，
   与 MC 的常规绘制路径基本无关。**【已定论（代码差集）+ 推断（触发条件）】**

4. **真正被读错的"顶点数据"几乎肯定不是属性，而是 tiler heap 里的位置/多边形数据。**
   两条互相独立、都可直接落地的改动（§5），按可能性排序：
   - **C1（最高）kbase 未抑制逐 render pass 的 tiler-heap 操作**：我们发出
     `cs_vt_end()`(`:4206/:4223` = VERTEX_TILER_COMPLETED)、`cs_finish_fragment()`(`:4637/:4641` =
     FINISH_FRAGMENT)、`cs_frag_end()`(`:4647` = FRAGMENT_COMPLETED)，并注册 TILER_OOM 中断处理器
     (`:4513/:4529`)；**跑通方在 kbase 上把这四项全部抑制，只留 `cs_vt_start()`**，理由写在代码注释里：
     VT 与 fragment 在 kbase 上属**不同 CS group**，统计量/credit 落在**不同 heap 世代**上，固件会据此
     **回收仍在使用的 heap chunk**。我们自己的注释（`:4620-4623`）也承认 FINISH_FRAGMENT
     **就是释放 heap chunk 的操作**——却被我们无条件发出。补丁草稿：
     `/root/research/vertex-work/mesa-suppress-kbase-heap-ops.diff`（95 行，`git apply --check` 通过，
     `-fsyntax-only` PAN_ARCH=12 与 10 均 exit 0）。
   - **C2（次高，第三方已在 G720 上复现）上游 MR !44173（open，2026-09-02）
     "panvk/csf: wait for prior tiling work before reusing tiler heap"**：
     `get_tiler_desc()`（我们树 `:1215`）在**不等待本子队列上一批 tiling 作业退休**的情况下重编程
     共享的 per-queue tiler heap ⇒ **tile 对齐的几何错乱，报告者就在 Mali-G720 MC10 上**。
     我们树与跑通方**都还没有这个 4 行补丁**。补丁草稿：
     `/root/research/vertex-work/mesa-mr44173-tiler-heap-wait.diff`（`git apply --check` 通过，
     `-fsyntax-only` PAN_ARCH=12 exit 0）。
   - **C3（低）`patch_vs_attribs` 基准实例累加**（见上第 3 条第二点）。

5. **最小实验（已编译通过，未在真机运行）**：给现成探针加了一个 `--mode=va`：
   4 个子测试覆盖 4 种 stride/offset 布局 + 1 个"永不绑定"的 vec4/uvec4 属性 + 逐实例(divisor=1)属性，
   并支持 `VA_REPEAT=N` **把同一个命令缓冲重复提交 N 次**（N≥400 跨过 kbase 的 128 次 heap 换新边界）。
   二进制已在服务器上构建：`/root/research/vertex-work/probe-vertex/panvk_wsi_probe_va`
   （312936 B，NDK r27c `-O2 -g -Wall`，**0 warning**），补丁
   `/root/research/vertex-work/probe-vertex/panvk-wsi-probe-va.diff`（524 行）。
   运行命令见 §6。**【已定论（编译）】/ 未验证（真机结果）**

---

## 1. 方法与"跑通方"溯源（**已定论**）

| 项 | 值 | 出处 |
|---|---|---|
| 我们树 HEAD | `5a07217f034b3e50d8c7c7794f97a2df1742613b`，2026-09-18 16:45 UTC（`26.3.0-devel`，**浅克隆，本地无历史**） | `git log -1` |
| `csf/panvk_vX_cmd_draw.c` 相对 HEAD 的改动 | **1 个 hunk / 1 行**：`panthor_kmod_get_csif_props(dev->kmod.dev)` → `panvk_get_csif_props(dev)` | `git diff src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c` |
| 跑通方 release | tag `0.1.0-beta.2` → commit `f1d7bed571766c49e5dd464f92d1fda264612311`（父 `980ac91de74d`），发布 2026-09-24 | GitHub API `releases` / `commits` |
| 跑通方二进制 | `PanVK-G720-0.1.0-beta.2.zip` → `libvulkan_panfrost.so` sha256 `126b8b61…`，21497392 B，**未 strip（含完整 DWARF）** | 20 号报告 §1；本轮复核 |
| 符号级核对（本轮新增） | 二进制含 `launch_tess_stages`(4)/`launch_gs_stages`(4)/`prepare_poly_heap`(4)/`prepare_vertex_shader`(1)/`patch_vs_attribs`(4)；**不含** `render_needs_crc_patch`、`prepare_layer_count_inherited_ctx` | `strings -a` + `nm -C` |
| 结论 | 二进制 ≈ tag 源码（只有 ref 树才有的符号在、只有我们树才有的符号不在）⇒ **ref 源码可作行为一致性的 oracle** | 【已定论（符号级）】 |

方法：
1. 把 ref 的 `csf/panvk_vX_cmd_draw.c`、`csf/panvk_vX_cmd_buffer.c`、`csf/panvk_vX_gpu_queue.c`、
   `panvk_vX_descriptor_set.c`、`panvk_vX_shader.c`、`lib/pan_desc.c`、`genxml/v12.xml` 拉到
   `/root/research/vertex-work/ref/`（**只读副本**）；
2. 先做**函数级**差分（正则抽取 `^name(` 边界的完整函数体），再对顶点/IDVS 相关函数逐个 diff；
3. 再拉上游 `main` 的同名文件做第二次差分，用来判定"我们的行为是上游行为还是我们的补丁行为"；
4. 最后查上游 GitLab issue/MR（`gitlab.freedesktop.org/api/v4/projects/176`）。

---

## 2. 顶点输入链路：**我们的树 = 上游**（**已定论**）

### 2.1 描述符打包本身

`csf/panvk_vX_cmd_draw.c` 的顶点属性发射（逐字核对，行号为本树快照）：

| 函数 | 行 | 内容 | 与上游 main 的差异 |
|---|---|---|---|
| `prepare_vi()` | 245 | 判定 `attribs_changing_on_base_instance`（per-instance 且 stride≠0） | 无 |
| `emit_vs_attrib()` | 270 | `cfg.offset`（per-instance 时 `+= base_instance*stride`）、`cfg.format`、`cfg.table=0`、`cfg.buffer_index=vb_offset+binding`、`cfg.stride`、`attribute_type`(1D / 1D_POT_DIVISOR / 1D_NPOT_DIVISOR)、`frequency`、`divisor_r/e/d` | 无 |
| `prepare_vs_driver_set()` | 326 | 未绑定属性 → `table=17` 特化 ATTRIBUTE；未绑定 vertex buffer → `NULL_DESCRIPTOR`；driver set = `{16×ATTRIBUTE + dummy SAMPLER + dyn bufs + N×BUFFER}` | 无 |
| `emit_varying_descs()` | 461 | `MALI_ATTRIBUTE_TYPE_VERTEX_PACKET`，`table=61`，`offset=1024+off`，**`buffer_index = PAN_ARCH >= 12 ? 1 : 0`**，`attribute_stride`/`packet_stride` | 无（仅 kbase/GS 侧无关改动） |
| `update_tls()` | 751 | **`#if PAN_ARCH >= 12` → `IDVS.VERTEX_TSD` / `IDVS.FRAGMENT_TSD`；否则 `IDVS.TSD_0`** | 无（ref 仅在别处加 xfb/tess 的 tls_size 计算） |

`git diff src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c` 与上游 `main`（`gitlab…/raw/main/...`，
189361 B）对比：**只有 7 个 hunk / 82 行**，且全部是"上游在我们 base 之后新增的功能"，
**没有一行触碰属性打包**（详情：`/root/research/vertex-work/ref/up_vs_ours.diff`）：
`vk_android.h` include、`panvk_get_csif_props`、`uint64_t fn_addr`、间接绘制前的 `cs_flush_stores(b)`
（= 上游 `fbb4993c5d`）、EFR 渲染信息补丁 ×2、`CmdEndRendering2KHR`。

### 2.2 genxml 描述符布局：v11/v12/v13/v14 **完全一致**

- `<struct name="Attribute" size="8" align="32">`：`v12.xml:1184`，字段与 `Offset/Stride/Divisor D/
  Buffer index/Attribute stride/Packet stride` 在 **v11/v12/v13/v14 逐字节相同**（脚本比对）。
- `<struct name="Buffer">` 三档相同。
- 枚举 `Attribute Type`(1=1D,2=1D POT,3=1D NPOT,5=1D Prim Index Buffer,6=Vertex packet)、
  `Attribute Frequency`(0=Vertex,1=Instance)、`Descriptor Type`(5=Attribute,9=Buffer)、
  `Buffer Type`(1=Simple,2=Tiler heap,3=Structure,4=Vertex packet) 在 v11/v12/v13 **取值相同**。
  ⇒ **不可能存在"v12 的 attribute_type/format 枚举编号变了但表没更新"这类错读**。【已定论】

### 2.3 arch12 专项分支清单（逐条核对）

`grep -n "PAN_ARCH" csf/*.c` 得到的 v12 相关分支，全部**在跑通方树里语义相同**：

| 位置（本树） | 内容 | ref 是否相同 |
|---|---|---|
| `:503` | `buffer_index = PAN_ARCH >= 12 ? 1 : 0`（varying packet） | 相同 |
| `:771-780` | v12 → `VERTEX_TSD`/`FRAGMENT_TSD` | 相同 |
| `:2165-2190` | v12 → `VERTEX_SPD = spds.all_{points,triangles}`（<12 才拆 POS/VARY SPD） | 相同（仅 kbase/GS 重构） |
| `:868 / :1259 / :3148 / :3174 / :3392 / :3418` | v12 的 scissor/limit/indirect 分支 | 相同 |

⇒ **"某些仅对 v10/v11 生效、对 v12 走错分支"这一假设，在顶点/IDVS 路径上不成立。** 【已定论】

---

## 3. 上游：G720/v12 顶点与 tiler 的已知问题（任务第 3 项）

本轮从上游 GitLab（project 176）查到的**相关**条目（全部给链接）：

| 类型 | 编号/状态 | 标题 | 与我们的关系 |
|---|---|---|---|
| **MR（open）** | [!44173](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/44173)（2026-09-02） | **panvk/csf: wait for prior tiling work before reusing tiler heap** | **直接命中**：报告者 CIX P1 / **Mali-G720 MC10**，描述"back-to-back render passes 下 **tile 对齐的错乱**"，bisect 到 `get_tiler_desc()` 复用 per-queue tiler heap 时未等待本子队列上一批 tiling 退休；补丁 = 在 `get_tiler_desc()` 里 `cs_wait_slots(b, dev->csf.sb.all_iters_mask)`。**未合并**，我们树没有。⇒ 我们的 **C2** |
| issue（closed） | [#16259](https://gitlab.freedesktop.org/mesa/mesa/-/issues/16259) | panvk: image flickering in supertuxkart with Mali-G720 | 同为 G720 的显示错乱（已关闭；未取到关闭原因） |
| issue（open） | [#14948](https://gitlab.freedesktop.org/mesa/mesa/-/issues/14948) | panfrost,panvk: Call for help on Immortalis G720 with GPU hangs (Cix Sky1) | G720 的 GPU hang 汇总线（panthor；与我们的 kbase 线不同） |
| issue（open） | [#15551](https://gitlab.freedesktop.org/mesa/mesa/-/issues/15551) | Mali G720 only using Private Heap Allocations (limited to ~4GB) | 对应上游 2026-09-24 的 `f333dd6d1c`（**在我们 base 之后**，未进我们树） |
| issue（open） | [#15308](https://gitlab.freedesktop.org/mesa/mesa/-/issues/15308) | panvk: Support for Vulkan features/extensions for the new Minecraft Vulkan render | MC 的 Vulkan 特性需求清单 |
| MR（merged） | [!32234](https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/32234) | panvk: fix frag_completed for layered rendering | 说明 `FRAGMENT_COMPLETED` 语义确为分层渲染记账 |
| commit | `6eccd73966`（2026-08-14） | panvk/v10+: Fix unbound LD_ATTR.auto32 | **`table=17` 的出处**：`LD_ATTR.auto32` 打到 NullDescriptor 会间歇返回 `0001` 而非 `0000`；改用特化 ATTRIBUTE + 不存在的 table 17 来保证 OOB |
| commit | `a56b15edc6`（2026-05-22） | panvk/csf: fix VERTEX_SPD dirty tracking when topology changes | v12 的 `VERTEX_SPD` 脏跟踪（我们树已含） |
| commit | `fbb4993c5d`（2026-09-24） | panvk/csf: flush the FAU stores before indirect draws read them | 间接绘制的 FAU 可见性（**我们 base 之后**，未进树） |

**在我们 base（2026-09-18）之后、触及 panvk 的全部提交（120 条 `src/panfrost` 提交里）**
没有任何一条修顶点属性/stride/IDVS 描述符（只有上面 `fbb4993c5d`/`f333dd6d1c`/`4ecc496633`/
`d7d2cd7b19`/`81db71cfba`/EFR/压缩等）。⇒ **"上游已经修了属性 bug 而我们没同步"这条路可以排除。**
【已定论】

---

## 4. 与跑通方的差异（**只列顶点/IDVS/描述符/tiler 相关**）

差分产物全部在 `/root/research/vertex-work/ref/`：`cmd_draw.diff`(4338 行，大部分是 GS/曲面细分)、
`cmd_buffer.diff`(317 行)、`gpu_queue.diff`(439 行)、`funcs_ours.txt`/`funcs_ref.txt`。

### 4.1 顶点属性侧（**共 2 处**）

| # | 位置（本树） | 跑通方 | 我们 | 判定 |
|---|---|---|---|---|
| V1 | `panvk_vX_cmd_draw.c:355-362`（`prepare_vs_driver_set`） | `pan_cast_and_pack(&descs[i], NULL_DESCRIPTOR, cfg);` | `pan_cast_and_pack(&descs[i], ATTRIBUTE, cfg) { cfg.table = 17; cfg.format = (MALI_R16F<<12)\|RGBA; }` | **我们更新**（= 上游 `6eccd73966`）。**不是病灶** |
| V2 | `:3208-3278`（`patch_vs_attribs`） | 无条件：`cs_move32_to(attrib_offset, attrib_info->offset)` 后加 `firstInstance*stride`（每次从 API 绝对值重算） | 只在 `cs_if(NEQUAL, first_instance)` 内：`cs_load32_to(attrib_offset, vs_drv_set, pan_size(ATTRIBUTE)*i+8)` 后**把 `firstInstance*stride` 累加进去再写回** | 见 §5 C3。**只在间接绘制 + 逐实例属性 + 命令缓冲被重复执行时错** |

> 注意：`patch_vs_attribs()` 只在 `draw.indirect.buffer_dev_addr != 0` 时被调用
> （`cmd_draw.c:3491-3492`：`if (draw.indirect.buffer_dev_addr) patch_vs_attribs(...)`），
> 且只有 `attribs_changing_on_base_instance != 0`（per-instance 且 stride≠0）时才进入循环。**【已定论】**

### 4.2 IDVS / SPD / varying 侧（**语义无差异**）

- `prepare_vs()`(ours) vs `prepare_vertex_shader()`(ref)：**只把 `gfx_state_dirty(VS)` 提成参数**，
  v12 的 `VERTEX_SPD = spds.all_triangles/all_points` 分支逐字相同（`diff` 仅签名与两处 `shader_dirty`）。
- `emit_varying_descs()`：ref 多出"GS/TES 才是 varying 产生者"的三元选择（为 GS/曲面细分），
  `buffer_index`/`table=61`/`offset=1024+`/`attribute_stride`/`packet_stride` **逐字相同**。
- `update_tls()`：ref 多出 xfb/tcs/tes/gs 的 `tls_size` 取大（为 GS/曲面细分）；
  `VERTEX_TSD`/`FRAGMENT_TSD` 段相同。
- `set_tiler_idvs_flags()` / `prepare_tiler_primitive_size()`：仅参数化重构。
- SPD 的构造块（`panvk_vX_shader.c`，我们 `:1235-1345` vs ref `:1442-1552`）：**逐字相同**
  （唯一差异是 `panvk_pool_free_mem(&shader->data_mem)` vs `(&shader->rsd)`，上游改名）。

### 4.3 tiler heap 侧（**关键差异**）

| # | 位置（本树） | 跑通方 | 我们 | 判定 |
|---|---|---|---|---|
| H1 | `:4206` / `:4223` | `if (!cmdbuf_skips_gpu_heap_ops(cmdbuf)) cs_vt_end(...)` | **无条件** `cs_vt_end(b, …)`（VERTEX_TILER_COMPLETED） | **C1** |
| H2 | `:4636-4648` | `if (cmdbuf_skips_gpu_heap_ops(cmdbuf)) { /* 由整堆换新回收 */ } else if (td_count==1) … else if (td_count>1) …` | **无条件**：`td_count==1 → cs_finish_fragment()`(FINISH_FRAGMENT)；`td_count>1 → …+cs_frag_end()`(FRAGMENT_COMPLETED) | **C1** |
| H3 | `:4508-4515` / `:4526-4530` | 只在非 kbase 时 `cs_set_exception_handler(TILER_OOM, …)`（注册/注销） | **无条件**注册/注销 | **C1** |
| H4 | `:1425` `cs_vt_start()` | **保留**（内核 tiler-OOM 校验要求 in-flight > 0） | 相同 | 不动 |
| H5 | `panvk_vX_cmd_buffer.c` / `cmd_draw.c` | 有 `account_tiler_work()`（9 行）并在 draw 路径调用、二级→一级累加（`cmd_buffer.c` 1036-1041） | **零次赋值**（全树 grep 只有声明与消费） | 与 15 号报告 H2 一致；影响换新节奏，**不直接产生错乱** |
| H6 | `panvk_vX_gpu_queue.c` | `kbase_export_sync_targets()` + `SYNC-A3`（在 `REQ_RESOURCE` 之前用 `csif_info->cs_reg_count-4` 作 scratch 发 `SYNC64_WAIT`） | 无（我们走 CPU/KCPU 桥） | 属队列同步；**未验证**与几何错乱的关系 |
| H7 | `panvk_vX_gpu_queue.c:73` 注释 | —— | 我们的注释写"**firmware chunk recycling disarmed（kbase 上没有 FRAGMENT_COMPLETED heap op）**" | **该注释与实际代码不符**：我们确实在 `td_count>1` 时发 `cs_frag_end`，且**单层路径也在发 FINISH_FRAGMENT**（我们自己的注释 `:4620-4623` 说 FINISH_FRAGMENT 是"释放 heap chunk"的操作）。**这是 C1 的旁证** |
| H8 | `panvk_vX_cmd_buffer.c` | 同名文件字节不同（44929 vs 49195） | 多出 `kbase_mark_progress` 埋点（PANVK_DEBUG 才生效） | 无 |

> H1/H2/H3 的跑通方原文（我逐字引用在补丁注释里）：
> "On kbase, tiler-heap maintenance is done wholesale by the queue's heap renewal (TERM+INIT of the
> whole heap) rather than through the firmware per-render-pass protocol: **the VT and fragment work
> run in separate CS groups there, so the VERTEX_TILER_STARTED/COMPLETED statistics (VT group) and
> FRAGMENT_COMPLETED credits (fragment group) accumulate against different heap generations.** The
> kernel validates the statistics reported with each tiler-OOM chunk request and terminates the group
> when they are inconsistent ("Invalid Heap statistics provided by firmware"), so on kbase we
> suppress VERTEX_TILER_COMPLETED, FRAGMENT_COMPLETED and FINISH_FRAGMENT heap maintenance."

内核侧机制（公开 kbase，`/root/research/csf-work/pub-kbase/`）与这段文字严丝合缝：
`csf.c:1913-1942 handle_oom_event()` 读 `CS_HEAP_VT_START/VT_END/FRAG_END`，
`if ((frag_end > vt_end) || (vt_end >= vt_start)) { dev_warn("Invalid Heap statistics provided by
firmware: vt_start %d, vt_end %d, frag_end %d"); return -EINVAL; }`，
否则 `renderpasses_in_flight = vt_start - frag_end`、`pending_frag_count = vt_end - frag_end`
交给 `th.c:900 validate_allocation_request()`。**【已定论（代码）+ 推断（因果）】**

---

## 5. 结论：最可能的 1–3 个具体改动点（按可能性排序）

### C1（最高）——kbase 上抑制逐 render pass 的 tiler-heap 操作

- **文件:行**：`src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c`
  - `:4206`、`:4223`：`cs_vt_end(...)` → 加 `if (!cmdbuf_skips_gpu_heap_ops(cmdbuf))`
  - `:4636`：在 `if (td_count == 1)` 之前插入 `if (cmdbuf_skips_gpu_heap_ops(cmdbuf)) { /* 由整堆换新回收 */ } else`
  - `:4513`、`:4529`：`cs_set_exception_handler(TILER_OOM, …)` 的注册与注销都加同一守卫
  - `:245` 之前（`prepare_vi()` 前）：新增 `static inline bool cmdbuf_skips_gpu_heap_ops(cmdbuf)`
    = `to_panvk_physical_device(dev->vk.physical)->kbase_node_path[0] != '\0'`
  - `:1425` `cs_vt_start()` **保持不动**
- **改法**：见草稿 `/root/research/vertex-work/mesa-suppress-kbase-heap-ops.diff`
  （`git apply -p1` 通过；`clang -fsyntax-only`（真实构建参数，`-Werror…`，PAN_ARCH=12 与 10）
  均 **exit 0**，日志 `/root/research/vertex-work/probe-vertex/syntax_check.log`）
- **依据**：跑通方（同一棵树的后代、G720 MC8 上已跑通）在 kbase 上就是这么做的，且它把"为什么"
  写在注释里（§4.3 引用）；我们自己的 `:4620-4623` 注释承认 FINISH_FRAGMENT 会释放 heap chunk；
  内核校验逻辑与"统计量跨 heap 世代"完全对应。
- **为什么能解释症状**：被读错的"顶点数据"是 **tiler heap 里的位置缓冲/多边形列表**（`IDVS` 的
  VERTEX_POS/VARY 输出与 tile list 都在这里），不是应用顶点缓冲里的属性。固件在错误的 heap 世代
  上回收 chunk ⇒ tiler 读到被覆盖的位置数据 ⇒ **巨大三角/斜切板条**，且不会产生 MMU fault、
  不会改变颜色通道、也不像交换链布局错误。
- 定论度：代码差集【已定论】；因果【推断】。

### C2（次高）——上游 MR !44173：`get_tiler_desc()` 复用 tiler heap 前先等待

- **文件:行**：`src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c:1215`（`get_tiler_desc()`），
  在 `struct cs_builder *b = panvk_get_cs_builder(cmdbuf, PANVK_SUBQUEUE_VERTEX_TILER);` 之后插入：
  ```c
  {
     /* The tiler heap is shared across render passes; wait for our own
      * prior async tiling work to retire before reprogramming it. */
     struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
     cs_wait_slots(b, dev->csf.sb.all_iters_mask);
  }
  ```
- **改法**：草稿 `/root/research/vertex-work/mesa-mr44173-tiler-heap-wait.diff`
  （`git apply -p1` 通过；`-fsyntax-only` PAN_ARCH=12 exit 0）
- **依据**：上游 **open MR !44173**，报告者就在 **Mali-G720（MC10）** 上看到 **tile 对齐的错乱**，
  bisect 到 `get_tiler_desc()`；我们树**与跑通方都还没有**这个补丁。
- 定论度：第三方报告【已定论】；"这就是我们这台机器的主因"【推断】——注意**跑通方没有它也工作**，
  所以它可能是"加剧项"而不是唯一主因。

### C3（低）——`patch_vs_attribs()` 的基准实例累加

- **文件:行**：`csf/panvk_vX_cmd_draw.c:3208-3278`
- **改法**：把 `cs_if(b, MALI_CS_CONDITION_NEQUAL, first_instance) { … }` 整体去掉，改成
  **每次都用 `cs_move32_to(b, attrib_offset, attrib_info->offset)` 重建绝对值**再加
  `firstInstance*stride`（跑通方版本，可直接从
  `/root/research/vertex-work/ref/ref-panvk_vX_cmd_draw.c` 的 `patch_vs_attribs()` 抄）。
- **依据**：跑通方注释："driver_set is command-buffer-owned and survives command-buffer execution.
  Never derive a new firstInstance offset from a descriptor patched by an earlier execution."
  我们的版本在同一命令缓冲被**重复执行**时会把 `firstInstance*stride` 反复累加进描述符。
- **触发条件**：`vkCmdDraw*Indirect` + 至少一个 per-instance(stride≠0) 属性 + 命令缓冲重复提交。
  Minecraft 常规路径**基本不满足**，所以排最后。**【已定论（代码）+ 推断（触发）】**

### 明确排除的

- 顶点属性 stride/format/offset/divisor 的**打包逻辑**（= 上游，且跑通方相同）——**排除**。
- genxml 的 v12 描述符布局/枚举编号——**排除**（v11/v12/v13/v14 逐字相同）。
- "上游已修而我们没同步"——**排除**（base 之后无相关提交）。
- 交换链/AHB/modifier 方向（P5，21 号报告）——本轮未涉及，但症状描述（"不是块状涂抹/颜色错位"）
  与 P5 的机制不吻合；如果 C1/C2 无效，建议再回到 P5 做一次 A/B。

---

## 6. 可验证的最小实验（**已编译，未在真机运行**）

产物目录：`/root/research/vertex-work/probe-vertex/`

| 文件 | 说明 |
|---|---|
| `va.vert` / `va.frag` | 新顶点/片元着色器：绑定 `loc0`(vec2 pos)、`loc1`(vec4 col)、`loc3`(vec4 逐实例, divisor 1)；**声明但永不绑定** `loc2`(vec4) 与 `loc4`(uvec4)，它们的默认值 `(0,0,0,1)` 被折进输出颜色 ⇒ **未绑定属性取值错误会直接改变采样像素** |
| `splice_probe.py` | 把 `mode_vertex_attrs()` 拼进 `panvk_wsi_probe.c`（同时在 `DEV_FNS` 加 `vkCmdDrawIndirect`/`vkCmdBindVertexBuffers`/`vkCmdBindVertexBuffers2`，把 `vkCmdBindVertexBuffers2` 归入"可选入口"） |
| `va_mode.c` | 4 个子测试（**每个一次 render pass**）：①stride 24/off 0,8 ②stride 32/off 8,24 ③**动态 stride 48**/off 20,4（`vkCmdBindVertexBuffers2`）④**间接绘制 + `firstInstance=1` + 逐实例属性**。期望中心像素：红/绿/蓝/`255,128,0,255`；实例缓冲 `[0](1,1,1,1) [1](1,.5,.25,1) [2](.25,1,1,1) [3](0,0,0,1)` ⇒ firstInstance 偏移若被多算一次，颜色会漂到 `[2]` 或 `[3]` |
| `build_va.sh` | glslangValidator + `gen_va_spv.py` + 拼接 + NDK 编译（`-I` 指向 probe10 以取 `tri.*.spv.h`）+ 生成补丁 |
| `panvk_wsi_probe_va` | **已构建**，312936 B，`-O2 -g -Wall -DHAVE_TRIANGLE`，**0 warning** |
| `panvk-wsi-probe-va.diff` | 524 行，对 `/root/research/probe10/panvk_wsi_probe.c` 的完整补丁 |

**构建（已验证）**
```bash
cd /root/research/vertex-work/probe-vertex && bash build_va.sh
# 默认源=/root/research/probe10/panvk_wsi_probe.c（只读使用）
```

**真机运行（由父级执行；本子代理按铁律未碰手机）**
```bash
# 1) 属性正确性（单次提交，4 个子测试）
adb shell "cd /data/local/tmp && MESA_LOG_LEVEL=info ./panvk_wsi_probe \
  --icd=/data/local/tmp/libvulkan_panfrost.so --mode=va"
# 2) 跨 heap 换新边界的时间稳定性（400 次重复提交同一命令缓冲，>128）
adb shell "cd /data/local/tmp && VA_REPEAT=400 MESA_LOG_LEVEL=info ./panvk_wsi_probe \
  --icd=/data/local/tmp/libvulkan_panfrost.so --mode=va"
```
日志关注：`[va] sub N centre = … (want …)`、`[va] DRIFT: sub … changed at repetition …`、
`[va] STABLE over N repetitions`。

**如何区分 C1/C2/C3**
| 观测 | 结论 |
|---|---|
| `sub 0..3` 全部 OK，但 `VA_REPEAT=400` 出现 `DRIFT` | **不是属性问题**；是 tiler heap 内容在换新/复用边界被破坏 ⇒ **C1 / C2** |
| `sub 0..3` 中有 MISMATCH（尤其 sub 1/2 = 非紧凑 stride/offset） | 属性 stride/offset/动态 stride 路径确有 bug ⇒ 回到 §2 深挖（与 C1/C2 无关） |
| 只有 `sub 3` 在重复提交后漂移 | **C3**（基准实例累加） |
| 全部 OK 且 STABLE | 属性与 heap 边界都干净 ⇒ 病灶在别处（回到 P5 / 交换链 / 深度·tiler 描述符） |

**C1 与 C2 的判别**：分别单独打上
`mesa-suppress-kbase-heap-ops.diff`、`mesa-mr44173-tiler-heap-wait.diff`（两者可独立应用），
在真机上跑同一个探针与 MC，看哪一个消除错乱。**先 C1**（依据更强、且是"跑通方就在这么做"）。

---

## 7. 未验证 / 需要下一步确认的点

1. **真机结果全部未取**：本报告的所有因果链都是"代码差集 + 内核/固件协议 + 第三方报告"，
   没有一条是在本机（PHZ110）上实测的。C1/C2 的补丁只做到 `git apply --check` 与
   `-fsyntax-only` 通过，**没有做完整 ninja 构建**（避免动 `/root/zenithblue` 的 build 目录）。
2. **ref 仓库与本项目的关系**：ref 的源码注释引用我们自己的"P2 / report 14"，因此它是"同源的另一条分支"
   而不是完全独立的第三方。作为 oracle 有价值，但**不能当作"外部独立验证"**。
3. **`csi_handlers`（15 号报告 H1）**：跑通方**不注册** TILER_OOM handler（C1 的一部分），
   这与"给内核置 `BASE_CSF_TILER_OOM_EXCEPTION_FLAG` 走 incremental rendering"是**两条相反路线**。
   本轮支持跑通方路线；15 号报告 H1 的假设**仍未验证**。
4. **`cs_reg_count` / `nr_kernel_registers`（15 号报告 H8）**：本轮未进一步查证；它对"顶点数据被读错"
   没有直接机制，但与 CS 寄存器划分有关，若 C1/C2 无效可回头查。
5. **上游 issue #16259 的关闭原因**未取到（notes API 返回结构异常，未重试）；如果是被某个提交修掉的，
   值得再挖一次。
6. **probe 的一个已知风险**：顶点/间接缓冲用的是 `HOST_VISIBLE|HOST_COHERENT` 内存类型
   （与探针既有 readback 缓冲同族）。在 kbase 上 GPU 读系统内存通常可行，但**未在真机验证**；
   若 `[va] vkCreateBuffer/Memory` 失败或几何全黑，请先看这段日志。
