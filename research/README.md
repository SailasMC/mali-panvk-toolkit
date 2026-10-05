# research/ —— 研究论文与结论摘要目录

本目录收录本项目（在**免 root** 的 Android 设备上，让 **Minecraft 启动器**跑在
**自编开源 Mali 驱动 Mesa PanVK** + **MobileGL DirectVulkan 后端**之上）过程中产生的
**全部系统性研究笔记**。

- 每篇 `NN-*.md` 都是一份**可独立阅读的技术报告**，含源码位置与实测证据；
- `summaries/` 是各篇的**中文结论摘要**（决策用，比正文短得多）；
- `paper.md` 是把 01–10 综合而成的**综述论文**；
- `00-paper-skeleton.md` 是 `paper.md` 的原始骨架（保留作为写作过程记录）。

> **状态口径**：本目录是这条研究线的**唯一权威记录**。
> 凡是"未验证"的推断，正文与摘要都已如实标注，`paper.md` 沿用同一口径。

---

## 1. 正文（10 篇，服务器 `/root/research/` 原样收录）

| # | 文件 | 标题 | 一句话主题 | 结论要点 | 状态 |
|---|---|---|---|---|---|
| 01 | [`01-aidl-route.md`](01-aidl-route.md) | AIDL（imapper5）路线可行性 | 能否不建 AOSP 拿到 mapper 客户端能力 | ★ **前提被纠正**：mapper 从来没有 AIDL 接口，v5 是 native stable-C `AIMapper`；但 `u_gralloc_imapper5_api.cpp` **已在不建 AOSP 的前提下编译+链接成功**（复用 VNDK 树，产物 49,016 B，导出 `u_gralloc_imapper_api_create`） | ✅ 已实测 |
| 02 | [`02-hidl-route.md`](02-hidl-route.md) | HIDL（imapper4）路线可行性 | HIDL 4.0 生成头的获取/生成路径与链接依赖 | **可行且便宜**（0.5–1.5 人天）；`prebuilts/vndk` v29–v34 预生成了 HIDL 头（**v35/v36 不存在**，本机 `ro.vndk.version=34` 正好吻合）；`hidl-gen` 有预编译二进制，2 个 curl 即可跑；唯一真坑是 libc++ `std::__1` vs `std::__ndk1`，修法确定 | ✅ 已实测编译+链接 |
| 03 | [`03-libgralloctypes.md`](03-libgralloctypes.md) | libgralloctypes 能力边界 | NDK `AHardwareBuffer` 能否完全替代 gralloc/IMapper | **不能**：decode 系列是纯函数，但字节流唯一来源是 IMapper@4.0 HIDL 的跨进程 `get()`；**NDK/VNDK/libui 全都没有返回 DRM modifier 的 API**；纯 NDK 连 dma-buf fd 都拿不到 | ✅ 已定论（负面） |
| 04 | [`04-mesa-ugralloc.md`](04-mesa-ugralloc.md) | Mesa `u_gralloc` 精读 | ops 契约 / 6 个后端 / `vk_android.c` 调用点 | 本机**既有 5 个后端全部不可用**（CROS/LIBDRM/QCOM 靠模块名精确匹配、imapper4/5 在 `-Dandroid-stub=true` 下**根本没编**、FALLBACK 只给 `DRM_FORMAT_MOD_INVALID` ⇒ panvk 里 NULL 解引用）⇒ **必须新增后端** | ✅ 已定论 |
| 05 | [`05-bypass-patch.md`](05-bypass-patch.md) | 绕开 `u_gralloc` 的最小补丁 | 方案 A：`vk_android.c` 的「AHB 自描述回退」 | 只改 **1 个文件约 90–105 行**、无需 meson 改动、严格加性（仅在前述调用失败时生效）；重编约 30–60 秒；**成功率估计 70–80%** | ✅ 方案成文，**未上机** |
| 06 | [`06-panvk-wsi.md`](06-panvk-wsi.md) | PanVK Android WSI 深挖 | `panvk_wsi.c` 全链依赖与错误返回点 | ★ **决定性事实**：`/dev/dma_heap/system` 权限 **0444** ⇒ `kbase_kmod.c` 用 `O_RDWR` 打开失败 ⇒ `kbase_kmod_supports_dmabuf()=false` ⇒ panvk 落 `sw_device=true`、`supports_modifiers=false` ⇒ DRI3/raw-fd 一行都没走到；与 u_gralloc 失败是**同一根因链** | ✅ 已定论 |
| 07 | [`07-mobilegl-wsi.md`](07-mobilegl-wsi.md) | MobileGL 侧能否绕开交换链 | 离屏渲染 / env 全清单 / 只改 MGL 的可行性 | **不存在"只改配置就出画面"的路**（默认 FBO 物理上就是交换链图像，无 blit）；但 ★ **一行杀招** `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1` 可把"WSI 建链失败→崩"降级为"干净跑完→黑屏"，用于**判定坏的只有 WSI** | ✅ 已定论（并已被采用） |
| 08 | [`08-zl2-surface.md`](08-zl2-surface.md) | ZL2 如何把 Surface 交给渲染器 | SDL / EGLBridge / SurfaceView 链与可配置项 | 用哪个桥**完全由 `POJAV_RENDERER` 字符串决定**；`custom_gallium`/`gallium_panfrost` ⇒ OSMesa 桥，**一次都不调 `vkCreateSwapchainKHR`**；代价是**每帧 CPU 合成**；`boatEnv` 在 ZL2 全树 0 命中 | ✅ 已定论 |
| 09 | [`09-precedents.md`](09-precedents.md) | 真实世界先例 | `VK_ERROR_INVALID_EXTERNAL_HANDLE` / `u_gralloc` 的公开案例与补丁 | 汇总上游/社区的同类问题与处理方式，作为本仓库补丁方向的旁证 | ⚠️ 无中文摘要（见 §3 缺口） |
| 10 | [`10-verify-probe.md`](10-verify-probe.md) | 验证方案与独立探针 | 可编译的最小 WSI 探针 + 兜底验证路径 | 交付 7 模式探针工程 `probe10/`（`panvk_wsi_probe.c` 1394 行，**已编译通过**，**未在真机运行**）；把卡点**精确化**到 `vkCreateSwapchainKHR` 第 4 步的 `u_gralloc_get_buffer_basic_info()` | ✅ 已编译，⏳ 未上机 |

---

## 2. 综述论文

| 文件 | 内容 |
|---|---|
| [`paper.md`](paper.md) | **综述论文**：摘要 / 引言 / 系统与设备背景 / 方法 / 失败点分析与修复（5 次错误点迁移）/ 证据 / 讨论（三条路线对比）/ 结论与后续工作 / 附录 |
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
| [`summaries/06-结论摘要与决定性事实.md`](summaries/06-结论摘要与决定性事实.md) | 06 | ★ `/dev/dma_heap/system` 0444 这条根因；路线 R1–R5 排名；05 与 06 之间一处**未解矛盾** |
| [`summaries/10-结论摘要-可上机探针.md`](summaries/10-结论摘要-可上机探针.md) | 10 | 7 模式探针清单 + 判据（logcat tag `MESA`）+ 先 render、再 ahb/mapper、再 headless、最后 win 的顺序 |

> **已知缺口（如实记录）**：尚未产出 07 / 08 / 09 三篇的**独立**中文摘要 ——
> 07 与 08 的结论已并入 `05-07-08-关键结论.md`；**09（真实世界先例）目前没有中文摘要**。

---

## 4. 与工程实录的关系

- 工程侧"实际怎么做的、踩了哪些坑、每一步的原文日志"在
  [`../docs/09-mobilegl-integration.md`](../docs/09-mobilegl-integration.md)（共 22 节）；
- 本目录的 01–10 是**专题深化**（WSI / gralloc / 启动器契约），`paper.md` 负责把两者缝合；
- 引用约定：**`docs/09 §N`** 指工程实录第 N 节；**`research/NN`** 指本目录第 NN 篇。

## 5. 背景（一句话）

目标设备：**OPPO PHZ110 / 联发科天玑 9300（MT6989）/ Immortalis-G720 MC12 / Android 16 (SDK 36) / 无 root**。
目标：让 **MobileGL 的 Direct(Vulkan) 后端**跑在**我们自己从源码编译的 Mesa PanVK**
（在插件里以 `libvulkan_freedreno.so` 之名分发，源码产物为 `libvulkan_panfrost.so`）之上。

**当前状态（截至本目录最后一次更新）**：
驱动加载 ✓ · 扩展枚举 ✓ · `vkCreateInstance` ✓ · `vkCreateDevice` 成功 ✓ · 队列族 `flags=0x7` ✓ ·
feature 逐位一致 ✓ · 交换链参数全部合法 ✓ ·
**判据行已达成** ✓（原文：`OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)`）·
**下一关** = 纹理上传阶段 `VK_ERROR_DEVICE_LOST (-4)`（`docs/09` §22）。
