# research/ —— 研究论文与结论摘要目录

本目录收录本项目（在**免 root** 的 Android 设备上，让 **Minecraft 启动器**跑在
**自编开源 Mali 驱动 Mesa PanVK** + **MobileGL DirectVulkan 后端**之上）过程中产生的
**全部系统性研究笔记**。

- 每篇 `NN-*.md` 都是一份**可独立阅读的技术报告**，含源码位置与实测证据；
- `summaries/` 是各篇的**中文结论摘要**（决策用，比正文短得多）；
- `attachments/` 是报告的**原始附件**（diff / 构建日志 / 校验程序），从 16 号起启用（**按报告号分子目录**）；
- `paper.md` 是把 01–12 综合而成的**综述论文**；
- `00-paper-skeleton.md` 是 `paper.md` 的原始骨架（保留作为写作过程记录）。

> **状态口径**：本目录是这条研究线的**唯一权威记录**。
> 凡是"未验证"的推断，正文与摘要都已如实标注，`paper.md` 沿用同一口径。
> ⚠️ **17–31 号尚未并入 `paper.md`**（该论文仍止于 01–12 的 6 次错误点迁移）；
> 17–31 覆盖的是**第 7 次迁移之后**的完整战役（tiler heap OOM → C2/P2 决定性修复 → `CALL` 卡住），
> 与工程实录 [`docs/09 §23–§34`](../docs/09-mobilegl-integration.md)、
> 里程碑 [`CHANGELOG.md` M12–M14](../CHANGELOG.md) 互为对照。

---

## 1. 正文（31 篇，服务器 `/root/research/` 原样收录）

| # | 文件 | 标题 | 一句话主题 | 结论要点 | 状态 |
|---|---|---|---|---|---|
| 01 | [`01-aidl-route.md`](01-aidl-route.md) | AIDL（imapper5）路线可行性 | 能否不建 AOSP 拿到 mapper 客户端能力 | ★ **前提被纠正**：mapper 从来没有 AIDL 接口，v5 是 native stable-C `AIMapper`；但 `u_gralloc_imapper5_api.cpp` **已在不建 AOSP 的前提下编译+链接成功**（复用 VNDK 树，产物 49,016 B，导出 `u_gralloc_imapper_api_create`） | ✅ 已实测 |
| 02 | [`02-hidl-route.md`](02-hidl-route.md) | HIDL（imapper4）路线可行性 | HIDL 4.0 生成头的获取/生成路径与链接依赖 | **可行且便宜**（0.5–1.5 人天）；`prebuilts/vndk` v29–v34 预生成了 HIDL 头（**v35/v36 不存在**，本机 `ro.vndk.version=34` 正好吻合）；`hidl-gen` 有预编译二进制，2 个 curl 即可跑；唯一真坑是 libc++ `std::__1` vs `std::__ndk1`，修法确定 | ✅ 已实测编译+链接 |
| 03 | [`03-libgralloctypes.md`](03-libgralloctypes.md) | libgralloctypes 能力边界 | NDK `AHardwareBuffer` 能否完全替代 gralloc/IMapper | **不能**：decode 系列是纯函数，但字节流唯一来源是 IMapper@4.0 HIDL 的跨进程 `get()`；**NDK/VNDK/libui 全都没有返回 DRM modifier 的 API**；纯 NDK 连 dma-buf fd 都拿不到 | ✅ 已定论（负面） |
| 04 | [`04-mesa-ugralloc.md`](04-mesa-ugralloc.md) | Mesa `u_gralloc` 精读 | ops 契约 / 6 个后端 / `vk_android.c` 调用点 | 本机**既有 5 个后端全部不可用**（CROS/LIBDRM/QCOM 靠模块名精确匹配、imapper4/5 在 `-Dandroid-stub=true` 下**根本没编**、FALLBACK 只给 `DRM_FORMAT_MOD_INVALID` ⇒ panvk 里 NULL 解引用）⇒ **必须新增后端** | ✅ 已定论 |
| 05 | [`05-bypass-patch.md`](05-bypass-patch.md) | 绕开 `u_gralloc` 的最小补丁 | 方案 A：`vk_android.c` 的「AHB 自描述回退」 | 只改 **1 个文件约 90–105 行**、无需 meson 改动、严格加性（仅在前述调用失败时生效）；重编约 30–60 秒；**成功率估计 70–80%** | ✅ **已实施并编入 v50**（v50 已上机，见 `docs/09 §28.4`） |
| 06 | [`06-panvk-wsi.md`](06-panvk-wsi.md) | PanVK Android WSI 深挖 | `panvk_wsi.c` 全链依赖与错误返回点 | ★ **决定性事实**：`/dev/dma_heap/system` 权限 **0444** ⇒ `kbase_kmod.c` 用 `O_RDWR` 打开失败 ⇒ `kbase_kmod_supports_dmabuf()=false` ⇒ panvk 落 `sw_device=true`、`supports_modifiers=false` ⇒ DRI3/raw-fd 一行都没走到；与 u_gralloc 失败是**同一根因链**（⚠️ 该链对**出厂件**不成立，见 `docs/09 §23.4` 的更正） | ✅ 已定论 |
| 07 | [`07-mobilegl-wsi.md`](07-mobilegl-wsi.md) | MobileGL 侧能否绕开交换链 | 离屏渲染 / env 全清单 / 只改 MGL 的可行性 | **不存在"只改配置就出画面"的路**（默认 FBO 物理上就是交换链图像，无 blit）；但 ★ **一行杀招** `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1` 可把"WSI 建链失败→崩"降级为"干净跑完→黑屏"，用于**判定坏的只有 WSI** | ✅ 已定论（该判定的结论已被 v50 上机结果反过来读，见 `docs/09 §28.4`） |
| 08 | [`08-zl2-surface.md`](08-zl2-surface.md) | ZL2 如何把 Surface 交给渲染器 | SDL / EGLBridge / SurfaceView 链与可配置项 | 用哪个桥**完全由 `POJAV_RENDERER` 字符串决定**；`custom_gallium`/`gallium_panfrost` ⇒ OSMesa 桥，**一次都不调 `vkCreateSwapchainKHR`**；代价是**每帧 CPU 合成**；`boatEnv` 在 ZL2 全树 0 命中 | ✅ 已定论 |
| 09 | [`09-precedents.md`](09-precedents.md) | 真实世界先例 | `VK_ERROR_INVALID_EXTERNAL_HANDLE` / `u_gralloc` 的公开案例与补丁 | 汇总上游/社区的同类问题与处理方式，作为本仓库补丁方向的旁证 | ⚠️ 无独立中文摘要（其结论已补为 `summaries/09-结论摘要.md`） |
| 10 | [`10-verify-probe.md`](10-verify-probe.md) | 验证方案与独立探针 | 可编译的最小 WSI 探针 + 兜底验证路径 | 交付 7 模式探针工程 `probe10/`（`panvk_wsi_probe.c`）；把卡点**精确化**到 `vkCreateSwapchainKHR` 第 4 步的 `u_gralloc_get_buffer_basic_info()` | ✅ 已编译，**已上机（8 模式，见 12）** |
| 11 | [`11-wsi-patch-implementation.md`](11-wsi-patch-implementation.md) | WSI 补丁实施（v50） | 05 方案 A + MR !43659 式 LINEAR 推断的落地与验证 | 3 个文件**严格加性**改动（`u_gralloc_fallback.c` 的 `-EINVAL→-EAGAIN`、新增 `panvk_infer_linear_modifier()`、`vk_android.c` 的 AHB 自描述回退约 150 行、`nativewindow_stub.cpp` 的 `lockPlanes` 桩）；①a 对 WSI 自身 AHB **不生效**（`format=1` 不是 YUV）⇒ 放行交换链的是 ①b；新 `.so` md5 `e08e0764…`/20,005,320 B；★ **构建目录实测判定**：只有 `build/android-v4` 有产物、`wsi_x11` 计数 0、`platforms=['android']` ⇒ **`research/06` 的"出厂件含 x11 WSI"不成立**（闭合 U4）；附三份 diff 原文 | ✅ 已实施+静态验证；**v50 已上机**（真交换链 + 真画面约 10 秒，`docs/09 §28.4`） |
| 12 | [`12-probe-run-results.md`](12-probe-run-results.md) | 真机探针 8 模式结果 | 逐步 `VkResult` 钉死卡点 | ★★ **驱动渲染被独立证明**（`render` 无 surface/无 root、`failures=0`、像素 `64/128/191/255` 三点精确）；**原假设不能复现**（`win`/`headless`/`winimpdef` 三路 `vkCreateSwapchainKHR` 全 `VK_SUCCESS`）；唯一可复现的 `-1000072003` 只在 AHB `IMPLEMENTATION_DEFINED(0x22)`；★ **`-4` 的真身 = kbase CSF fatal exception `0xc3`**（`tri`：`vkQueueSubmit→0` 但 `vkWaitForFences=-4`），不含 draw 的 clear+copy 全正常 ⇒ **首要目标转为查 CSF `0xc3`** | ✅ 已实测（探针 md5 `f735e1f4…`，加载补丁前驱动 `4417b369…`） |
| 13 | [`13-csf-exception-c3.md`](13-csf-exception-c3.md) | CSF `0x7dc002c3 / exception 0xc3` 定位报告 | 逐位解码异常码 + 触发面 + 最小验证步骤 | ★ **`0x7dc002c3` 是 GPU MMU 的 `AS_FAULTSTATUS` 原值**：`EXCEPTION_TYPE=0xC3` = **TRANSLATION_FAULT_3**（L0→L1→L2 有效、最后一级 PTE 无效）、`ACCESS_TYPE=READ`、`SOURCE_ID=0x7DC0` = **CSF 固件自己的 LSU**；`sideband 0x0000005fffe1e000` **就是故障 GPU VA**。"三个 group 同时 fatal"**不是 3 次故障**（kbase 把同一份 payload 复制给该 kctx 所有在位 CSG）；触发面 = 只有真 `vkCmdDraw` 才会让固件碰 **tiler heap**（clear/copy 走 `vk_meta` 全屏 fragment 路径，不消费 chunk）；给出 S1/S2/S3 三步最小验证 | ✅ 已定论（只读；未改任何只读树） |
| 14 | [`14-tiler-heap-oom.md`](14-tiler-heap-oom.md) | tiler heap OOM 定案 | 为什么 grow 没发生 + 最小可行修法 | ★ **OOM 通知是"验尸报告"**：kbase 在送通知**之前**就已 `term_queue_group()`；★ **grow"没成功"的真相 = 堆被顶到天花板且无人重置**：`initial_chunks=10`/`max_chunks=400`/`chunk_size=1 MiB`，而 Mesa 侧唯一的重置手段 `kbase_renew_tiler_heap()` **是死代码**（触发条件要求 `submit->tiler_work_estimate != 0`，而该字段**全树没有任何写入点**）⇒ 堆单调涨到 400 MiB → `-ENOMEM` → 杀组；★ **10 秒黑洞对上了**：OOM `11:25:37.423` → DEVICE_LOST `11:25:47.413` = **9.99 s** = `KBASE_WAIT_TIMEOUT_NS`；dma_heap 的 `O_RDONLY` **不是**本案凶手；两处最小修法 **(A)** 建 CSG 走 uAPI 1.18 布局并置 `csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG`、**(B)** 接上 renew（最小 2 行：去掉 `tiler_work_estimate` 前置条件） | ✅ 已定论（源码级 + 公开 kbase r43p0 支撑；**(A) 已实现为 v53 的 P1 并上机验证无效 ✗**，见 16） |
| 15 | [`15-panvk-mtk-diff.md`](15-panvk-mtk-diff.md) | 与「已跑通先例 `/root/panvk-mtk`」的彻底 diff | 先例真实身份 + 两棵树的可移植差异 | ★ **先例身份被澄清**：`/root/panvk-mtk` 只是**补丁仓库**（单 commit，`patches/panvk_mtk.patch` + 构建脚本），源码真身是 `/root/mesa`（`funnymdzz/mesa@6598829`，未打补丁的原始态）；两树**同源同作者血脉**（注释逐字相同），`kbase_kmod.c` / `panvk_vX_gpu_queue.c` / `panvk_physical_device.c` 差异极小 ⇒ **先例的成功不能归因于任何一处 tiler 参数/workaround 的不同**；找出 **6 条**可移植差异，其中 **H1（`csi_handlers` 从未送达内核）最高**、**H2（`tiler_work_estimate` 生产者被整段删掉 ⇒ renew 永不执行）已定论** | ✅ 已定论（只读 diff） |
| 16 | [`16-p1-p2-implementation.md`](16-p1-p2-implementation.md) | P1 落地报告（v53） | CSF group create 走 uAPI 1.18 布局并置 `csi_handlers` | 版本阶梯 `1.25/1.6` → **`1.25/1.18/1.6`**（新增 1.18：ioctl **`0xc028803a`**、结构体 40 B、`csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG`；uAPI 判断原样保留、`<1.18` 老路径不变）；新 `.so` size **20,005,600** / md5 **`7f3a0e8f…b404`** / sha256 **`58ef996f…bec4`**；**反汇编确认新分支进二进制**；v53.apk sha256 **`9c99af82…d48e`**；载荷逐位校验非空；**P2 未实施**（一次只改一个自变量，v52 就是现成对照）；附 ABI 偏移自证、回滚三步、并发冲突提示 | ✅ 已实现+编译+打包+静态验证；★ **真机验证：无效 ✗**（仍 OOM、无 `TILER_OOM CSI handler (1.18 layout, ioctl 58)` ⇒ 推断 1.18 分支被版本门挡住，见 `docs/09 §27.3`） |
| 17 | [`17-p2-implementation.md`](17-p2-implementation.md) | **v54 落地报告：接上 tiler heap renew（P2 / 修法 B）** | 去掉 `submit->tiler_work_estimate &&` 前置门，让 `kbase_renew_tiler_heap()` 真正触发 | ★ **P2 落地**：`kbase_tiler_submit_count` 按"每 N 次图形提交"推进（clear-only 不计）；新增 `PANVK_KBASE_HEAP_RENEW_INTERVAL` 开关（默认 128）与版本/分支诊断日志；产物 = v54 驱动 `a9cba64a…`（20 006 408 B）/ APK `860d0780…` | ✅ 已实现+编译+打包；★ **真机有效**（见 `docs/09 §31`） |
| 18 | [`18-ring-dump-and-missing-logs.md`](18-ring-dump-and-missing-logs.md) | Ring dump 逐条解码 + v54 三条新日志为何没出 | kbase ring entry 的发射序列与"改了却没日志"的两种原因 | ★ **ring entry 指令级解码**：每个 entry 20 条单字指令（160 B = 20×8）、`CALL` 在第 14 条（+112 B）—— 这张解码表后被 `docs/09 §29.4/§31` 反复引用；并给出"三条新日志没出"的问题 A/B 判定（**后来被证明与"静默回落"同源**，见 `docs/09 §33.1`） | ✅ 已定论（只读解码） |
| 19 | [`19-g720-driver-support-status.md`](19-g720-driver-support-status.md) | 「会不会是驱动本身有问题？」G720/panvk 支持状态最终判定 | funnymdzz/mesa 的验证目标覆盖范围 | **不是驱动本身的问题**：该 fork 是**活跃维护的 kbase 后端**，但其公开声明的验证目标 ≠ G720（uAPI 世代差一整档）；给出"跑通方二进制"的可移植性边界 | ✅ 已定论（负面排除） |
| 20 | [`20-v55-working-driver-package.md`](20-v55-working-driver-package.md) | v55：把 wonderkast02 的 G720 先例驱动打进我们的插件壳 | 先例驱动**到底能不能装上去** | ★ **提醒**：先例件是 Android **HAL 模块**（`hw_get_module`/`HMI`），**不是 JSON 可发现的桌面式 ICD** ⇒ v55 按任务书字面装上去**必定在 ICD 入口处失败**，**不要用它做判定**（会做成假阴性）；两个阻断点给出定论 | ✅ 已定论（装不上去 ≠ 病灶在设备侧） |
| 21 | [`21-p5-modifier-fallback.md`](21-p5-modifier-fallback.md) | **P5 实施记录 —— MTK AFBC modifier 回退** | AHB modifier 从 LINEAR 改 AFBC（跑通方方向） | 跑通方铁律要求 AFBC **`0x0800000000000072`**（= `DRM_FORMAT_MOD_ARM_AFBC(32x8\|SPARSE\|SPLIT\|YTR)`）；开关 `PANVK_GRALLOC_AFBC_FALLBACK`（默认 1）；★ **真机 ⇒ `exception 0xc3`（MMU `TRANSLATION_FAULT_3`）⇒ 判定有害、已撤回** | ✗ **已证伪并撤回**（见 `docs/09 §32`） |
| 22 | [`22-vertex-corruption-arch12.md`](22-vertex-corruption-arch12.md) | G720（arch **v12**）上「几何/顶点数据被读错」的定位 | 顶点属性打包是否与跑通方不同 | ★ **前提纠错**：顶点属性描述符（stride/format/offset/divisor）**不是**差异点；列出两条互相独立、可落地的候选改动并按可能性排序 | ✅ 已定论（只读定位；改动未上机） |
| 23 | [`23-completion-timeout-crash.md`](23-completion-timeout-crash.md) | **10 秒完成超时 → `VK_ERROR_DEVICE_LOST` 定案报告** | 为什么 `vkWaitForFences` 恰好 10 s 后得 `-4` | ★ 六条结论：本机**无法启用完成/故障通知**（协议层）；`SYNC_ADD64` 唤醒路径逐行打点；把"环被取空"变成**不可信信号**；两个先例（panvk-mtk / G610）逐字对照 | ✅ 已定论（`KBASE_WAIT_TIMEOUT_NS` = 9.99 s 吻合） |
| 24 | [`24-fix1-kick-implementation.md`](24-fix1-kick-implementation.md) | fix1 实施报告（无条件 kick 调度器）→ **v57** | 删掉"快速路径"是否解决超时 | ★ **无效**：无条件 kick 并未解决 10 s 超时 ⇒ 反证"快路径误判"这一假设；附 v57 产物哈希 | ✗ **已证伪**（见 `docs/09 §32`） |
| 25 | [`25-cacheinvalidate-kick-fix.md`](25-cacheinvalidate-kick-fix.md) | CS_ACTIVE 缓存失效修复（保留快路径）→ **v58** | 读 GPU 写内存前是否漏了 cache invalidate | 在 `kbase_subqueue_publish()` 读 `*active` 前各加一条 `kbase_cache_invalidate_range()`（`:733`/`:737`，共 **2 行**）；★ **真机无效** ⇒ 缓存一致性不是该超时的成因 | ✗ **已证伪**（见 `docs/09 §32`） |
| 26 | [`26-c1-heap-suppression.md`](26-c1-heap-suppression.md) | **C1：kbase 上抑制逐 render pass 的 tiler-heap 操作** → **v59** | 只留 `cs_vt_start`、抑制其余 heap 操作会怎样 | 6 处用 `cmdbuf_skips_gpu_heap_ops()` 守卫：不再发 `cs_vt_end`/`cs_finish_fragment`/`cs_frag_end`，且不再注册/撤销 **TILER_OOM** 处理器；★ **真机有害 ⇒ 流水线在第 3~4 个作业即卡死** | ✗ **已证伪并撤回**（见 `docs/09 §32`） |
| 27 | [`27-v60-clean-c1.md`](27-v60-clean-c1.md) | **v60：干净单变量构建（v54 基线 + C1）** | 把 C1 单独隔离出来再判一次 | ★ **P5 与 v58 缓存失效全部撤回后**，v54 逐位基线 + C1（唯一新变量）⇒ **仍有害** ⇒ 确证"卡死"是 **C1 本身**，不是构建不干净 | ✗ **已证伪**（干净单变量版） |
| 28 | [`28-v61-c1-plus-cacheinvalidate.md`](28-v61-c1-plus-cacheinvalidate.md) | v61 = 当前树（v60）+ v58 的两行缓存失效 | C1 与缓存失效叠加是否有救 | **无救**：两者叠加仍有害；本轮一次编译失败**未产出 `.o`**、未污染当时在用的 `.so`（报告内已如实标注） | ✗ **已证伪** |
| 29 | [`29-v62-c2-tiler-wait.md`](29-v62-c2-tiler-wait.md) | **v62 = v61 + C2（上游 open MR `!44173`）** | 复用 tiler heap 前先等待自家 tiling 工作退休 | ★ C2 首次编入：`get_tiler_desc()` 内、取 VERTEX_TILER builder 之后插入 `cs_wait_slots(b, dev->csf.sb.all_iters_mask)`；★ **判据纪律**：C2 的**独有判据是归属注释 `upstream MR !44173`**，`cs_wait_slots(all_iters)` 的**计数不能单独作判据**（v54 本就有一处同形调用）；撤 C2 ⇒ 逐位 = `1915d16e…`（v61） | ✅ 已实现+编译；本轮未上机（效果见 30） |
| 30 | [`30-v63-clean-c2.md`](30-v63-clean-c2.md) | **v63：v54 基线 + C2（干净单变量版本）** | 把 C1 与 v58 两行撤干净后，只留 C2 | ★ **确定性对照通过**：撤 C1 + 撤 v58 两行 ⇒ 重编**逐位 = `a9cba64a…`（20 006 408 B = v54）**；再只贴回 C2（429 B/10 行，与 v62 原块逐字节相同）⇒ `c03f0e7b…`（20 007 048 B），**二次重编逐位相同** | ✅ 已实现+编译+打包；★ **真机：存活跃升到分钟级**（`docs/09 §29.3`） |
| 31 | [`31-v65-next-contract-fix.md`](31-v65-next-contract-fix.md) | **v65：kbase ring wrapper 不得抢占 PanVK 的 `SB_MASK_STREAM`** | v64 挂起的机制定位 + 下一处"与内核/固件契约不符"的点 | ★ **v64 三次挂起签名一致**：三个子队列的 ring `extract` **精确停在各自最后一条 ring entry 的 `CALL`**、`CS_ACTIVE=0`、`cell->error=0`、无 CS fault、无 TILER_OOM、10 s 内 20 次 rekick 一字节未动 ⇒ 机制 = **流切换（`CALL`）处的 CS 状态被卡**；v65 删掉 wrapper 重写 `SB_MASK_STREAM` 的 2 行；★ **并如实指出**：「2 分 51 秒」最长存活**不在 `cap.txt` 内、本轮无法复核** | ✅ 已实现+编译+打包+确定性对照；★ **v65 未上机**（在途） |

---

## 2. 综述论文

| 文件 | 内容 |
|---|---|
| [`paper.md`](paper.md) | **综述论文**：摘要 / 引言 / 系统与设备背景 / 方法 / 失败点分析与修复（**6 次**错误点迁移）/ 证据 / 讨论（三条路线对比 + **"WSI 不是最终瓶颈，CSF 绘制执行才是"**）/ 结论与后续工作 / 附录 |
| [`00-paper-skeleton.md`](00-paper-skeleton.md) | 骨架（写作过程记录，`paper.md` 的前身） |

---

## 3. 中文结论摘要（`summaries/`）

| 文件 | 对应正文 | 作用 |
|---|---|---|
| [`summaries/01-结论摘要-突破.md`](summaries/01-结论摘要-突破.md) | 01 | ★ 突破：imapper5 后端**不建 AOSP 也能编出来**；含 libc++ 双 ABI 硬阻塞的绕过法 |
| [`summaries/02-结论摘要-可行且便宜.md`](summaries/02-结论摘要-可行且便宜.md) | 02 | HIDL imapper4：0.5–1.5 人天，最大阻塞已证明低风险 |
| [`summaries/03-结论摘要.md`](summaries/03-结论摘要.md) | 03 | 纯 NDK 无法绕开 gralloc；**缺口核心就是 modifier** |
| [`summaries/04-结论摘要与行动计划.md`](summaries/04-结论摘要与行动计划.md) | 04 | 现有 5 个后端全部不可用 + 新后端 ops 契约 + 直连 mapper AIDL 的方案 |
| [`summaries/05-07-08-关键结论.md`](summaries/05-07-08-关键结论.md) | 05 / 07 / 08 | 决策依据：**三条路线选择表**（A 修 WSI 推荐 / B 切 gallium 桥 / C shim 层绕过） |
| [`summaries/06-结论摘要与决定性事实.md`](summaries/06-结论摘要与决定性事实.md) | 06 | ★ `/dev/dma_heap/system` 0444 这条根因（⚠️ 对出厂件已被 `docs/09 §23.4` 更正）；路线 R1–R5 排名；05 与 06 之间一处**未解矛盾**（该矛盾**已闭合**，见 [`11-wsi-patch-implementation.md`](11-wsi-patch-implementation.md) §1） |
| [`summaries/09-结论摘要.md`](summaries/09-结论摘要.md) | 09 | ★ 真实世界先例：**上游没有"不依赖 gralloc 的 WSI"**；`-EINVAL→-EAGAIN` 一行修复 + !43659 式 LINEAR 推断；Mali 生态公认解 = **自分配 + blit**；mapper AIDL/stable-C 在应用态**无成功先例** |
| [`summaries/10-结论摘要-可上机探针.md`](summaries/10-结论摘要-可上机探针.md) | 10 | 7 模式探针清单 + 判据（logcat tag `MESA`）+ 先 render、再 ahb/mapper、再 headless、最后 win 的顺序 —— **已上机，结果见 [`12-probe-run-results.md`](12-probe-run-results.md)** |

> **已知缺口（如实记录）**：07 / 08 两篇**没有独立**的中文摘要 —— 其结论已并入
> [`summaries/05-07-08-关键结论.md`](summaries/05-07-08-关键结论.md)。
> ~~09（真实世界先例）没有中文摘要~~ ⇒ ✅ **已补**（[`summaries/09-结论摘要.md`](summaries/09-结论摘要.md)）。
> 11–16 六篇为密集报告，**不另设摘要**（正文本身已足够短到可当摘要读；每篇的 TL;DR 即摘要）。

---

## 4. 附件（`attachments/`）

| 目录 | 内容 |
|---|---|
| [`attachments/16/`](attachments/16/) | 16 号（P1/v53）的原始附件：[`16-p1.diff`](attachments/16/16-p1.diff)（P1 unified diff，85 行）、[`16-p1-build.log`](attachments/16/16-p1-build.log)（ninja 全文，`NINJA_EXIT=0`）、[`16-p1-layout-check.c`](attachments/16/16-p1-layout-check.c)（ABI/字段偏移自证程序） |
| [`attachments/17/`](attachments/17/) | 17 号（P2/v54）：[`17-v54-gpu_queue.diff`](attachments/17/17-v54-gpu_queue.diff)、[`17-v54-kbase_kmod.diff`](attachments/17/17-v54-kbase_kmod.diff)（P2 的两份 diff）、`17-v53-manifest.txt` / `17-v54-manifest.txt`（`aapt2 dump` 清单原文） |
| [`attachments/21/`](attachments/21/) | 21 号（P5/v56）：`21-v56-manifest.txt` |
| [`attachments/24/`](attachments/24/) | 24 号（fix1/v57）：[`24-fix1.diff`](attachments/24/24-fix1.diff)、`24-v57-manifest.txt` |
| [`attachments/25/`](attachments/25/) | 25 号（缓存失效/v58）：[`25-fix.diff`](attachments/25/25-fix.diff)、`25-v58-manifest.txt` |
| [`attachments/26/`](attachments/26/)–[`attachments/30/`](attachments/30/) | C1 系列（v59/v60/v61）与 C2 系列（v62/v63）的 `aapt2 dump` 清单原文 |
| [`attachments/31/`](attachments/31/) | 31 号（v65）：`31-v64-manifest.txt` / `31-v65-manifest.txt`（两版清单，**仅 versionCode/versionName 两行不同**）、`31-build-revert.log` / `31-build-v65-reapply.log`（**确定性对照**的两次 ninja 全文）、`31-v65.so.sha256`（`b9952f75…`） |

> **附件纪律**：只收**文本**（`.diff` / `.txt` / `.log` / `.sha256`）。
> 驱动 `.so`、APK 一律**不入库**（含 `31-work/v65-payload.so`）⇒ 其哈希登记在
> [`MANIFEST.md`](../MANIFEST.md) §B.1 / §E.2（这本身就是仓库规则的一次实证）。

---

## 5. 与工程实录的关系

- 工程侧"实际怎么做的、踩了哪些坑、每一步的原文日志"在
  [`../docs/09-mobilegl-integration.md`](../docs/09-mobilegl-integration.md)（共 **28** 节，
  §23 = v48/v49、§24 = v50 WSI 补丁、§25 = 真机探针 8 模式、§26 = v51/v52 调试取证链路、
  §27 = v53(P1) 与真机结果、§28 = **真机实测记录（v46–v53）**）；
- 本目录的 01–16 是**专题深化**（WSI / gralloc / 启动器契约 / CSF / tiler heap），
  `paper.md` 负责把两者缝合；
- 引用约定：**`docs/09 §N`** 指工程实录第 N 节；**`research/NN`** 指本目录第 NN 篇。

## 6. 背景（一句话）

目标设备：**OPPO PHZ110 / 联发科天玑 9300（MT6989）/ Immortalis-G720 MC12 / Android 16 (SDK 36) / 无 root**。
目标：让 **MobileGL 的 Direct(Vulkan) 后端**跑在**我们自己从源码编译的 Mesa PanVK**
（在插件里以 `libvulkan_freedreno.so` 之名分发，源码产物为 `libvulkan_panfrost.so`）之上。

**当前状态（截至本目录最后一次更新）**：
驱动加载 ✓ · 扩展枚举 ✓ · `vkCreateInstance` ✓ · `vkCreateDevice` 成功 ✓ · 队列族 `flags=0x7` ✓ ·
feature 逐位一致 ✓ · 交换链参数全部合法 ✓ ·
**判据行已达成** ✓（原文：`OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)`）·
**驱动渲染被独立证明** ✓（探针 `render`/`ahb`/`win` 三模式 `failures=0`）·
**原假设"交换链建不起来"不能复现** ✓（三路 `vkCreateSwapchainKHR` 全 `VK_SUCCESS`）·
★★ **真交换链 + 真画面** ✓（v50 上机：`Swapchain created, extent = 2376x1080, swapchain imageCount = 3`；
**用户实测 MC 主界面（含 3D 全景）干净渲染约 10 秒**，不花屏/不乱跳/不撕裂）·
★ **两条各自独立、且都还没修的病灶**：
1. **CSF exception `0xc3`**（探针 `tri`：`vkQueueSubmit` 成功但 `vkWaitForFences = -4`）—— `research/13`；
2. **tiler heap OOM**（真实 App 现场：`E/MESA: kbase: CSF group 0 tiler heap OOM notification`，
   判据行 → +5~10 s 该行 → +9 s `-4`）⇒ **P1（v53）已上机验证无效 ✗**，
   按 `research/14` 的定案，**下一步是 P2（接上 tiler heap renew，治"只涨不落"）**——
   `docs/09` §27/§28.5/§28.6、[`16-p1-p2-implementation.md`](16-p1-p2-implementation.md)、
   [`14-tiler-heap-oom.md`](14-tiler-heap-oom.md)。
