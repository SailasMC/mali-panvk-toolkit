# MANIFEST —— 产物台账

> **为什么二进制不进 git**：APK / `.so` / `.a` 体积大（单个 10 MB 级、驱动 20–300 MB）、
> 且涉及第三方与厂商许可。按项目约定（[`README.md` §6](README.md#6-约定每次进展都提交-git)），
> **只把"知识 / 源码 / 清单"放进仓库**，产物以**文件名 + 大小 + sha256 前缀 + 一句话用途**登记于此。
>
> **数据来源**：全部为服务器 `/root/final/` 上以 `stat` / `sha256sum` **实测**所得（2026-10-05）。
> **不编造**：凡未经记录的内容（例如某版本的实机运行结果）一律标注"**无记录**"，不作断言。
>
> **哈希口径**：表中 `sha256(前16)` = `sha256sum` 输出的**前 16 个十六进制字符**。
> 其中 v39 / v41 / v47 三个前缀曾由 [`docs/09-mobilegl-integration.md`](docs/09-mobilegl-integration.md)
> §18.3 / §19 / §22 **独立给出**，与本台账实测**完全一致** —— 可作为台账可信度的交叉验证。

---

## A 主体台账：`mgl-panvk-v16` → `mgl-panvk-v49`

> 这是主线（MobileGL Magma + PanVK 渲染器插件）的**完整演进台账**。
> `versionName` 来自各版本构建目录的 `AndroidManifest.xml`（`/root/vNN/`），是**权威的"这一版是什么"**。
> `mtime` 为服务器上的文件时间，用于还原时间序（`versionCode` 单调递增，两者一致）。

| 版本 | versionName | 大小 (B) | sha256(前16) | mtime | 包含什么 / 用于验证什么 |
|---|---|---|---|---|---|
| v16 | `1.6-loaderdebug` | 10,379,823 | `c26ba62d1825ed9a` | 07:01 | 在插件 `pojavEnv` 里加 `VK_LOADER_DEBUG=all`。**用于验证** Android loader 是否认 `VK_ICD_FILENAMES` ⇒ 得到"**忽略**"的铁证（版本号 `1.3.247`/`44.1.0` 而非我们的 `1.4.363`）；并确认 loader 的 debug 输出**没进 logcat** |
| v17 | `1.7-shim` | 10,408,573 | `5a0d65e8563770d7` | 07:08 | 首次打包**自建转发垫片**（唯一名 `libvkpanvk_shim.so`，125 入口）。**用于验证**垫片可导出 MGL 所需全部 loader 风格入口 |
| v18 | `1.8-shim-as-loader` | 10,408,573 | `1c9906178107a95a` | 07:20 | 插件 `lib/arm64-v8a/` 放**原件 `libMobileGL.so` + 完整垫片改名为 `libvulkan.so`**。**用于验证**"顶替 `.so` 名字"是否被命中 ⇒ 游戏能起但**走厂商 blob**，说明**该名字没被命中** |
| v19 | `1.9-shim-both` | 10,400,449 | `5aed9bf972d47eea` | 07:12 | 同上，但 **`.so` 与 `.so.1` 双名**都放完整垫片。**用于验证**"顶替 loader 名字"的后果 ⇒ **JVM 卡死在 `[DEBUG] Calling JLI_Launch`**，与裸 ICD 事故**同一症状** ⇒ 该路线**彻底封死** |
| v20 | `2.0-icd-linked` | 10,179,119 | `b5efd251d6b64681` | 07:17 | 转向"把 ICD 链进去"的尝试（对照 v18/v19 的失败） |
| v21 | `2.1-icd-dladdr` | 10,179,119 | `07221a4ce63781a3` | 07:19 | 用 `dladdr()` 在运行时求"插件自身目录"以定位 ICD（避免硬编码路径） |
| v22 | `2.2-dlopen-preload` | 10,396,285 | `d4382b688d9daa21` | 07:22 | 首次把 **`DLOPEN=` 写进 `pojavEnv` 和 `boatEnv`**（预加载垫片）。**用于验证** `DLOPEN` 预加载机制 ⇒ 该轮 LWJGL 出现 `liblwjgl.so: unknown type` 崩溃（后经读源码定性：**不是 `DLOPEN` 造成的**） |
| v23 | `2.3-dlopen-pojavonly` | 10,396,285 | `08dd31569f0f4c9a` | 07:24 | 同上但**只写 `pojavEnv`**（因为 ZL2 只解析 `pojavEnv`）。**用于验证**"只写 pojavEnv 是否就正常" ⇒ **仍然崩** ⇒ 排除 `boatEnv` 因素 |
| v24 | `2.4-restore-good` | 10,379,823 | `dca7cde69ff31110` | 07:27 | **兜底版本**：内容 = 已知可跑的 v16 配置。**用于保证** ZL2 能正常启动 26.3（MobileGL DirectVulkan 满帧，但 Vulkan 仍走 blob） |
| v25 | `2.5-dlopen-only` | 10,392,189 | `5a6cc97c9cbaa05d` | 07:29 | 只保留 `DLOPEN` 预加载、不带其它改动。**用于验证**`DLOPEN` 单独是否安全（源码定案：**纯预加载、无害**） |
| v26 | `2.6-icd-static` | 10,179,119 | `6e4465159dbd08e7` | 07:34 | 垫片**静态链入 MGL**的方向（为消掉"顶替系统库名字"的依赖） |
| v27 | `2.7-icd-pdpa` | 10,179,119 | `f61dd8d4b9f53835` | 07:39 | 补 `vk_icdGetPhysicalDeviceProcAddr` 分派。**用于验证**能否消除 `wsi_GetSwapchainImagesKHR` 那一类句柄域问题 |
| v28 | `2.8-icd-dualsrc` | 10,179,119 | `613469d5837803ef` | 07:42 | 双源回退（instance GIPA / device GIPA）。**用于验证**句柄域覆盖是否够用 |
| v29 | `2.9-restore-good` | 10,379,823 | `3b09805e3e43f3e5` | 07:43 | **恢复包**（回到已知可跑配置），穿插在探索序列中保证设备可用 |
| v30 | `3.0-icd-3src` | 10,179,119 | `7a059b5fb5a0750f` | 07:45 | **三源回退**（instance / device / physical-device）。**用于验证**任一"取不到入口"的场景 |
| v31 | `3.1-restore-good` | 10,379,823 | `938937a7d4b02885` | 07:46 | **恢复包** |
| v32 | `3.2-vkdbg` | 10,183,215 | `dd7260df2b0af0ff` | 07:49 | 打开 Vulkan 侧调试输出（`vkdbg`）。**用于验证**调用是否真的经过我们的层 |
| v33 | `3.3-restore-good` | 10,379,823 | `79fda3bdd13d8760` | 07:50 | **恢复包** |
| v34 | `3.4-gdpa-devdbg` | 10,179,119 | `8926994c6b15aec2` | 07:52 | device 级 `vkGetDeviceProcAddr` 取指针 + 调试。**用于验证**设备级入口解析 |
| v35 | `3.5-restore-good` | 10,379,823 | `9dcfab48038452f0` | 07:53 | **恢复包** |
| v36 | `3.6-loader-semantics` | 10,183,215 | `f96884d9f040859a` | 07:55 | **123 项 thunk 表**（实现 loader 语义：`vkGet*ProcAddr` 只返回自家 thunk）。**用于验证**句柄域统一与日志可观测性；修掉 `libMobileGL.so+0x961380` 的 NULL 函数调用，换来 `Required extension found: VK_KHR_swapchain ✓` |
| v37 | `3.7-restore-good` | 10,379,823 | `d68e48839c633950` | 07:57 | **恢复包** |
| v38 | `3.8-filelog` | 10,183,215 | `383184bd4b89ec63` | 07:59 | **日志双写**：`__android_log_print` + 追加写 `/sdcard/MG/vkshim.log`。**用于验证**在 Android 丢弃 app `stderr`、logcat 环形缓冲有限的前提下拿到稳定证据（此后所有定位都依赖它） |
| v39 | `3.9-pdpa-first` | 10,183,215 | `1e4c231f0a40d905` | 08:02 | `gipa_pd()` **物理设备级优先**（先 `g_pdpa`，再退回真实 instance）。**用于验证** `vkGetPhysicalDeviceQueueFamilyProperties` / `…Features` / `…Properties` 拿到**正确**函数指针，消除 `apiVersion=540.1018.2112` 这类垃圾值。**※ `docs/09` §18.3 原文给出的前缀 `1e4c231f` 与本行实测一致 ✓** |
| v40 | `4.0-featcmp` | 10,183,215 | `217c6dda7ad8bab3` | 08:04 | `feat[i] want/sup/xor`（6 字 = **192 个 feature 位逐位对比**）。**用于验证** MGL 请求位与驱动支持位是否分歧 |
| v41 | `4.1-featmask` | 10,183,215 | `acf64880d999331f` | 08:09 | 建 device 前**屏蔽驱动不支持的 feature 位** + 打印 `masked-out bits`。**用于验证**"投机性提交被拒"是否就是 `-3` 的来源（结果：`masked-out bits=0x00000000`、`xor` 全 0）。**※ `docs/09` §19 原文给出的前缀 `acf64880` 与本行实测一致 ✓** |
| v42 | `4.2-qfam` | 10,183,215 | `390e64c8a966f0ce` | 08:11 | 打印 `QueueFamilyProperties: count / family[i] flags,queues`。**用于验证**队列族是否真的被看见 |
| v43 | `4.3-a-test` | 10,183,215 | `f1d6a3b728ff9138` | 10:10 | **A/B 二分**：`QFam-diag: fn / viaInstanceGIPA / pdpa / gipa / g_inst`。**用于一次定论**转发层取指针取空了（`fn=0x0` 而 `pdpa` 有效），从而把根因锁定到生成器的分派误判 |
| v44 | — | — | — | — | **不存在**（编号跳号：v43 → v45） |
| v45 | `4.5-pd-fixed` | 10,183,215 | `01313450de7bae85` | 10:13 | **4 处"把物理设备当 instance"修正落地**。**用于验证**修正后队列族 `count=1 / flags=0x7 queues=2`、feature `xor` 全 0、`vkCreateDevice` 成功 |
| v46 | `4.6-wsi-diag` | 10,183,215 | `81083a6e7e9ac8b6` | 10:24 | 增加 `SurfaceCaps:` 与 `CreateSwapchain: surf/usage/fmt/pm/alpha/layers/old` 落盘。**用于验证**交换链入参是否合法 ⇒ 参数全合法但仍 `VK_ERROR_INVALID_EXTERNAL_HANDLE`，把卡点精确到 Mesa `u_gralloc` |
| v47 | `4.7-headless-wsi` | 10,183,215 | `bb6898389f9e791d` | **10:43** | ★★ **判据行达成版本**。载荷 = v46 + `pojavEnv` 增加 **`MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`**。**用于验证**"坏的只有 WSI"：Mesa 把任意 surface 换成 headless 交换链、`queue_present` 空操作返回 `VK_SUCCESS` ⇒ 游戏日志出现 `OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)`（实测时间 10:46:13）。**※ `docs/09` §22 原文给出的前缀 `bb689838` 与本行实测一致 ✓** |
| v48 | `4.8-diag-deep` | 10,183,215 | `e511f980b8a63d53` | 10:50 | 在 v47 基础上**加深诊断**：追加 `MESA_DEBUG=1`、`PANVK_DEBUG=1`、`MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log`、`LIBGL_DEBUG=1`、`EGL_LOG_LEVEL=debug`。**用于**给 `VK_ERROR_DEVICE_LOST (-4)` 取证。**运行结论**（[`docs/09 §23.1`](docs/09-mobilegl-integration.md)）：`-4` 的**首次出现**被钉在 MGL **纹理上传批次**的 `vkQueueSubmit`（`VkTextureManager.cpp`），错误码/位置与 v47 相同；该链**不创建 AHB、不经过 `u_gralloc`、也不经过任何 WSI 代码** |
| v49 | `4.9-nodmaheap` | 10,183,215 | `f1389427f687a0cd` | 10:55 | 在 v48 基础上追加 `PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent`。**用于**针对 `/dev/dma_heap/system` **0444** 这条根因做**反向对照**（关掉 dma-heap ⇒ 退回 kbase 原生分配）。**运行结论**（[`docs/09 §23.2`](docs/09-mobilegl-integration.md)）：**无效** —— 错误码不变、`-4` 照旧 ⇒ **dma-heap / dma-buf 不是该 `-4` 的成因** |

**注 1**：`v17` 的完整 sha256 见 §B 的原始输出（本表只列前 16 位）。
**注 2**：v16–v49 中「**恢复包**」（`*-restore-good`）的作用是把设备恢复到**已知可跑**的配置，
不引入新变量；它们是探索序列的"断续保护"，不是功能版本。
**注 3**：v44 不存在 —— 编号从 v43 直接跳到 v45。

### A.1 与主线并行的两个产物

| 文件 | 大小 (B) | sha256(前16) | 说明 |
|---|---|---|---|
| `restore-good-v26.apk` | 10,379,823 | `62c71f43b2c4b113` | "恢复到 v26 配置"的**独立恢复包**（不在 `mgl-panvk-vNN` 命名序列内）。**用于**在实验失败后把设备恢复为可用状态 |

---

## B 前史与后续（`v12`–`v15`、`v50`–`v53`）

| 版本 | versionName | 大小 (B) | sha256(前16) | 说明 |
|---|---|---|---|---|
| v12 | —（构建目录已不在服务器上；`versionName` 无记录） | 6,070,749 | `e643eaea9449200e` | **首个"复刻社区插件契约 + 追加 ICD 指向"的插件**：`renderer = magma_panvk:libMobileGL.so:libMobileGL.so`、`pojavEnv` 追加 `VK_ICD_FILENAMES=/storage/emulated/0/mali-icd/panvk_icd.json`；`libMobileGL.so` 采用**社区版已验证的构建**，把唯一变量隔离到"驱动来源"上（`docs/09` §7） |
| v13 | — | 10,379,823 | `61b45ee2c38396bd` | v12 之后的迭代（**用途无记录**） |
| v14 | `1.4-panvk` | 14,688,889 | `c7cde6b8581b0cd0` | 把**裸 ICD** 放进插件 lib 目录 ⇒ **JVM 卡死在 `JLI_Launch`**（`docs/09` §9②、§13 的原始事故） |
| v15 | `1.5-panvk-rollback` | 10,379,823 | `f7fb5ecd9d640fdd` | 上述事故的**回滚版**（注意：回滚也必须**递增 `versionCode`**，降级安装会被 `INSTALL_FAILED_VERSION_DOWNGRADE` 拒绝，`docs/09` §9） |
| v50 | `5.0-wsi-patched` | 10,187,311 | `677d81eb29c7c579` | **判据行之后的"真 Android 交换链版"**：载荷换成**新的 `libvulkan_panfrost.so`**（改名 `libvulkan_freedreno.so`，size 20,005,320 / md5 `e08e0764…` / sha256 `a0b2451e…`）+ v49 的 `libMobileGL.so` + `classes.dex`；`pojavEnv` 含 `PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent`、`MESA_DEBUG=1`、`PANVK_DEBUG=1`、`PANVK_GRALLOC_NO_FALLBACK=0`、`PANVK_GRALLOC_NO_INFER_LINEAR=0`，**且已删掉 `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`**（与 v49 的**唯一自变量差异**）。打包脚本见 [`source/pack/pack_v50.sh`](source/pack/pack_v50.sh)。**运行结论**：[`docs/09 §24`](docs/09-mobilegl-integration.md)（补丁范围 / 产物哈希 / **构建目录实测判定**，`research/11`）+ [`docs/09 §25`](docs/09-mobilegl-integration.md)（探针 8 模式，`research/12`）——**唯一致命注意**：**本 APK 从未在真机运行过**，补丁只有静态/链接层验证；而探针已独立证明"原假设（交换链建不起来）不能复现"，真正让 GPU 掉线的是**执行 draw 时的 CSF exception `0xc3`**。**运行结论（已上机，[`docs/09 §28.4`](docs/09-mobilegl-integration.md)）**：删掉 headless 开关后**首次出现真交换链**（MGL 日志 `Swapchain created, extent = 2376x1080, swapchain imageCount = 3`）+ **用户实测主界面（含 3D 全景）干净渲染约 10 秒**（不花屏/不乱跳/不撕裂），之后黑屏崩溃（`vkQueuePresentKHR` / `vkAcquireNextImageKHR` → `-4`） |
| v51 | `5.1-wsi-patched-debug` | 8,595 | `6e9ce7d261b07b99` | **空载荷 APK ⇒ 已废弃，勿用**：打包时 `unzip` 匹配载荷条目失败，产物里**只剩 manifest / `resources.arsc` / `classes.dex` / 签名**，**一条 `lib/arm64-v8a/*` 都没有**（`apksigner verify` 却**通过**）。教训：必须数载荷条目 + `unzip -p … \| sha256sum` 与源件比对（[`docs/09` §26.1](docs/09-mobilegl-integration.md)） |
| v52 | `5.2-wsi-patched-debug` | 10,187,311 | `0bbef03083e74cfb` | **v50 载荷（一字节不改）+ 全套调试 env**（`MESA_DEBUG=1` / `PANVK_DEBUG=1` / `LIBGL_DEBUG=1` / `EGL_LOG_LEVEL=debug` / `MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log`；`MESA_VK_WSI_HEADLESS_SWAPCHAIN` 出现次数 = 0）。**运行结论**（[`docs/09` §28.5](docs/09-mobilegl-integration.md)）：复现「约 10 秒干净画面后黑屏崩溃」，**后台落盘 logcat** 抓到 `E/MESA: kbase: CSF group 0 tiler heap OOM notification`（判据行 → +5~10 s 该行 → +9 s `-4`） |
| v53 | `5.3-p1-tiler-oom-csi` | 10,187,311 | `9c99af82d51b54c3` | **P1**：`kbase_kmod.c` 的 CSF group create 补 **uAPI 1.18 档**（ioctl 58 = `0xc028803a`、40 B、`csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG`）；载荷 = 新 `.so`（size 20,005,600 / md5 `7f3a0e8f…` / sha256 `58ef996f…`）；env 与 v52 逐字符一致（`aapt2 dump` diff 为空）。**运行结论**（[`docs/09` §27.3](docs/09-mobilegl-integration.md)/§28.6）：★ **无效 ✗** —— 仍出现同一条 tiler heap OOM 通知，且**没有**出现期望的 `TILER_OOM CSI handler (1.18 layout, ioctl 58)` ⇒ 推断 1.18 分支被版本门挡住；**P1 单独不够，下一步 P2**（[`research/14`](research/14-tiler-heap-oom.md) Fix B1 / [`research/16`](research/16-p1-p2-implementation.md) §6） |

---

## C `/root/final/` 内的其它 APK（同一目录，非主线）

| 文件 | 大小 (B) | sha256(前16) | 说明 |
|---|---|---|---|
| `panvk-g720.apk` | 4,710,885 | `ffad07a636f1dec2` | 早期把 G720 PanVK 打成驱动插件的产物 |
| `g720-in-zenith-slot.apk` | 4,710,885 | `5fa64a0332587921` | 把 G720 驱动放进 zenith 槽位的产物（与上一行同尺寸，**是否同一内容未核对**） |
| `combo-driver-patch.apk` | 4,350,591 | `7f8fae904eaa103e` | 组合驱动补丁包（`fcl-patch/` 相关） |
| `slot-driver.apk` | 4,317,669 | `16266b5e1a8ffaba` | 槽位驱动包（`pack_slot.sh` 产物） |
| `panvk-zenith.apk` | 7,344,613 | `49418c9d177e9fd9` | zenith 基线 PanVK 的插件包 |
| `panvk-shim.apk` | 16,859 | `d67a326c85c3620d` | **早期**的 `vkshim_icd.c` 路线产物（`tools/vkshim_icd.c`）。**记录一条已证明走不通的路**（`LD_LIBRARY_PATH` 在进程启动后无效） |
| `mobilegl-renderer.apk` | 10,175,097 | `a5d68977b3574b57` | MobileGL 渲染器插件的**原始/社区版**复刻（`renderer = …:libMobileGL.so`，**不含** ICD 指向） |
| `mio-mgl-icd.apk` | 6,664,669 | `4914a8d7101668ad` | `mio` 命名的 MGL + ICD 组合包 |

> ⚠️ 本节各包的**用途多来自文件名与早期记录，未逐项核对**；标注为"未核对"处即表示**本仓库没有可直接引用的记录**。

---

## D 源码与内容块哈希（进 git 的文本产物）

> 这些**是**仓库内容（体积小、纯文本/脚本），哈希用于核对"仓库里的就是当时用的那份"。

| 文件 | 大小 (B) | sha256(前16) | 来源与用途 |
|---|---|---|---|
| [`source/shim/vkshim_mgl.c`](source/shim/vkshim_mgl.c) | 64,413 | `792baae1c364d780` | `/root/mgl_icd/vkshim_mgl.c` —— **最终**转发垫片源码（静态链入 MGL；`gipa_pd` 物理设备优先、`FLOG` 双写、WSI 入口挂钩） |
| [`source/shim/gen_shim.py`](source/shim/gen_shim.py) | 7,051 | `51f3039d2c6de728` | `/root/shimgen/gen_shim.py` —— 从 NDK `vulkan_core.h`+`vulkan_android.h` 解析并生成 `shim_gen.inc` |
| [`source/shim/shim.c`](source/shim/shim.c) | 17,188 | `a147da783fd12fff` | `/root/shimgen/shim.c` —— 手写外壳（6 个特殊函数 + 分派逻辑 + 日志），`#include "shim_gen.inc"` |
| [`source/shim/shim_gen.inc`](source/shim/shim_gen.inc) | 132,213 | `b5b3584360568a1e` | `/root/shimgen/shim_gen.inc` —— **生成物**：119 个泛型转发函数 |
| [`source/shim/vkund.txt`](source/shim/vkund.txt) | 2,718 | `e172c349fa1f9fe1` | `/root/shimgen/vkund.txt` —— MGL 的 **125 个** UND `vk*` 符号清单（实测 125 行） |
| [`source/shim/gen_report.txt`](source/shim/gen_report.txt) | 378 | `f7b858f339b67a80` | `/root/shimgen/gen_report.txt` —— 生成报告（125 / 119 / 6 + 解析路径分布） |
| [`source/shim/build_shim.sh`](source/shim/build_shim.sh) | 1,692 | `05b51143fa9252de` | `/root/shimgen/build_shim.sh` —— 编译垫片（须带 `-Wl,-soname,…`） |
| [`source/pack/pack_mgl_plugin.sh`](source/pack/pack_mgl_plugin.sh) | 2,421 | `7accccd6280af62b` | `/root/pack_mgl_plugin.sh` —— 渲染器插件打包（`aapt2 link` → `zip` → `zipalign` → `apksigner`） |
| [`source/pack/pack_v50.sh`](source/pack/pack_v50.sh) | 3,003 | `b66d33b6fb58dfd7` | `/root/pack_v50.sh` —— **v50**（真 Android 交换链版）的打包脚本，含完整清单模板 |
| [`source/pack/verify_mgl.sh`](source/pack/verify_mgl.sh) | 604 | `cff66dd940a44997` | `/root/verify_mgl.sh` —— 装机后的验证脚本 |
| [`source/pack/AndroidManifest.v46.xml`](source/pack/AndroidManifest.v46.xml) | 1,439 | `22310107ed2eb6cd` | `/root/v46/AndroidManifest.xml` —— **插件清单模板**（`fclPlugin` / `renderer` / `pojavEnv` / `des` / `minMCVer`） |
| [`research/11-wsi-patch-implementation.md`](research/11-wsi-patch-implementation.md) | 22,910 | `ea1444069d6c534a` | `/root/research/11-wsi-patch-implementation.md` —— **v50 WSI 补丁实施报告**（3 文件 diff 摘要 + 产物哈希 + ★ 构建目录实测判定，闭合 U4）。另逐字收录三份 diff 原文：[`11-diff-src_util_u_gralloc_u_gralloc_fallback.c.txt`](research/11-diff-src_util_u_gralloc_u_gralloc_fallback.c.txt)（4,453 B）/ [`11-diff-src_vulkan_runtime_vk_android.c.txt`](research/11-diff-src_vulkan_runtime_vk_android.c.txt)（8,272 B）/ [`11-diff-src_android_stub_nativewindow_stub.cpp.txt`](research/11-diff-src_android_stub_nativewindow_stub.cpp.txt)（1,084 B） |
| [`research/12-probe-run-results.md`](research/12-probe-run-results.md) | 19,911 | `794b1236fbbce55c` | `/root/research/12-probe-run-results.md` —— ★★ **真机探针 8 模式结果**（渲染被独立证明 / 原假设不能复现 / `-4` = CSF `0xc3`） |
| [`research/13-csf-exception-c3.md`](research/13-csf-exception-c3.md) | 34,390 | `cd72b69e0d91ab73` | `/root/research/13-csf-exception-c3.md` —— ★ **CSF `0x7dc002c3`/`0xc3` 定位报告**（= GPU MMU `AS_FAULTSTATUS` 原值：TRANSLATION_FAULT_3 + CSF LSU 的 READ；触发面 = 真 `vkCmdDraw` 碰 tiler heap） |
| [`research/14-tiler-heap-oom.md`](research/14-tiler-heap-oom.md) | 40,079 | `9f3a2c9473def8a5` | `/root/research/14-tiler-heap-oom.md` —— ★ **tiler heap OOM 定案**（堆只涨不落 + 10 秒黑洞 = `KBASE_WAIT_TIMEOUT_NS` + 两处最小修法 A/B） |
| [`research/15-panvk-mtk-diff.md`](research/15-panvk-mtk-diff.md) | 30,083 | `f8728e0abc83c074` | `/root/research/15-panvk-mtk-diff.md` —— 与先例 `/root/panvk-mtk`（实为 `/root/mesa` 的补丁仓库）的彻底 diff；H1/H2 两条可移植差异 |
| [`research/16-p1-p2-implementation.md`](research/16-p1-p2-implementation.md) | 21,322 | `412e7e3387734edb` | `/root/research/16-p1-p2-implementation.md` —— ★ **P1 落地报告（v53）**：uAPI 1.18 档 + `csi_handlers`、产物哈希、反汇编取证、**真机无效 ✗**、P2 补丁草案 |
| [`research/attachments/16/16-p1.diff`](research/attachments/16/16-p1.diff) | 4,163 | `cbc71b17bca8c94f` | P1 的 unified diff（备份件→现件，85 行） |
| [`research/attachments/16/16-p1-build.log`](research/attachments/16/16-p1-build.log) | 279 | `d36a55452fd38393` | P1 增量编译 ninja 全文（`NINJA_EXIT=0`，只重编 `kbase_kmod.c` + 重链） |
| [`research/attachments/16/16-p1-layout-check.c`](research/attachments/16/16-p1-layout-check.c) | 2,088 | `49e0408263a785fc` | ABI/字段偏移自证程序（`sizeof=40`、`offsetof(csi_handlers)=29`） |
| [`research/summaries/09-结论摘要.md`](research/summaries/09-结论摘要.md) | 7,836 | `df011e384b7ef182` | 09 号（真实世界先例）的**中文结论摘要** —— 补齐 `research/README.md` §3 记录的缺口 |

> 签名用 keystore（`/root/dsh-driver.keystore`，别名 `dshdriver`，口令为脚本内明文 `android`）
> **不纳入本仓库**。打包脚本按原样收录以便复现流程，使用者需自备 key。

---

## E 附录：`/root/final/` 的原始 `sha256sum` 输出（可复核）

以下为服务器上一条命令的原始输出（`cd /root/final && sha256sum *.apk`），
供与主线台账逐行比对（**这是台账的原始数据，勿改**）：

```
7f8fae904eaa103ee61985bac68ced6796702c2816391fcbd3c62973aee481d1  combo-driver-patch.apk
5fa64a03325879212302f3d347281c7729204461bd9d92a6318f74be6f371e34  g720-in-zenith-slot.apk
e643eaea9449200eab4513c2a5e79e3c035ee2636fdf451880b286b660b3c63a  mgl-panvk-v12.apk
61b45ee2c38396bd465511fa717ae1afe6a6629a21446a0bd4565d918d8ac589  mgl-panvk-v13.apk
c7cde6b8581b0cd0e1fa90c9034a331fa7be93bc500d9dcd6d97fbcfba4cf637  mgl-panvk-v14.apk
f7fb5ecd9d640fdd3256dac6689922eb36b9685a880cdab761f75a90dba7ce4b  mgl-panvk-v15.apk
c26ba62d1825ed9a0bb93d198eea790b3082a64f81124a1326a285e4718129fc  mgl-panvk-v16.apk
5a0d65e8563770d7d201d5fffca11df49d7a18b17834a9d18839167f1480832c  mgl-panvk-v17.apk
1c9906178107a95aed78d1270e6d0401c00081d78c10fda1646fd4b56915934d  mgl-panvk-v18.apk
5aed9bf972d47eea40776be1955663fafde9e318eeac42be544fe768a498b3bd  mgl-panvk-v19.apk
b5efd251d6b646817990bd9eb91c83b17aee729a4f2f392de9395cc81591406c  mgl-panvk-v20.apk
07221a4ce63781a33eb1ceab93dace8c11203fb4050b14a8143bbf9847f22ecb  mgl-panvk-v21.apk
d4382b688d9daa217d772c33df5617b18261b8baa13601e122b1813c36683736  mgl-panvk-v22.apk
08dd31569f0f4c9a9974bf04d0adca8803332e451da3c0728cccde7768a5153d  mgl-panvk-v23.apk
dca7cde69ff31110136c29217e2a90546bd78c8dbac8aac78e916b3dfaa23405  mgl-panvk-v24.apk
5a6cc97c9cbaa05dd7979e6b7c5a07c38eaefb216cf93eb4f0d402d1483d1564  mgl-panvk-v25.apk
6e4465159dbd08e71da3c5cd9d4a76e01ea785d05db22f32df56910a99e87251  mgl-panvk-v26.apk
f61dd8d4b9f538354db12d726ea2a821248648b972d965828bc90dce88d4021b  mgl-panvk-v27.apk
613469d5837803ef2ac32fe7184d661fe41b9c826e639cf8c15807a35c1f952b  mgl-panvk-v28.apk
3b09805e3e43f3e572f451853fa7ceb4a75f46e4cd249187379eb88ea6c6a5c3  mgl-panvk-v29-restore.apk
7a059b5fb5a0750f086b673d55faafddaea158ae11f869e991d34665897aa6a7  mgl-panvk-v30.apk
938937a7d4b02885f055b6632b63c4986cd49fee0531e716f4d440b864c57a57  mgl-panvk-v31-restore.apk
dd7260df2b0af0ff80ce102ed3ce29311b23afe129e84f1ea757e5f49768da6e  mgl-panvk-v32.apk
79fda3bdd13d8760efb0efa94b3ab15642fcf1718bd9fd0fb932f53a977500da  mgl-panvk-v33-restore.apk
8926994c6b15aec2995e68c26010c1ee066742c6608283ae295527f0d84c406a  mgl-panvk-v34.apk
9dcfab48038452f0a38e111b8e6a4d6a06acdcf7f42d74a613222d36ad04ca9b  mgl-panvk-v35-restore.apk
f96884d9f040859ab0ad18730e9205d477f15b3c013a9f820f57054c0cd823ee  mgl-panvk-v36.apk
d68e48839c63395083750c13df3b7834489540d795e84add4346e2bd6b75236f  mgl-panvk-v37-restore.apk
383184bd4b89ec63a04104db2a77818f61804cb2960fbe930cd5b95c7a93bc8a  mgl-panvk-v38.apk
1e4c231f0a40d905efaf88319d7ec1bb6723516e859558f17f27baa4ad07b034  mgl-panvk-v39.apk
217c6dda7ad8bab372f6635c83f72fb3b750125a48b07fabf18f0a2d782884c0  mgl-panvk-v40.apk
acf64880d999331fa9a14daa346633366f6720532b42aa3c54f2ffc991861e7a  mgl-panvk-v41.apk
390e64c8a966f0ce9ebcd40064c7b3b77e2f3d9b44d772b5f334d22f0f5011f7  mgl-panvk-v42.apk
f1d6a3b728ff91388ac3bd637ed22b5a5e3840b0bce575a45621c8a931fdbc61  mgl-panvk-v43.apk
01313450de7bae85fe2b680dfe29ded3a98d0ac9ff66629e7127c2feffd03298  mgl-panvk-v45.apk
81083a6e7e9ac8b679ff4a07f12fa660ccbba60c9517a91e6c7dda547e927b9f  mgl-panvk-v46.apk
bb6898389f9e791d51dd4e04d2fe6684de3c6b3846968ef3737c70f15a988abc  mgl-panvk-v47.apk
e511f980b8a63d53169fb3d69e92a5a49151498397adfa3e75359a5f377fbab4  mgl-panvk-v48.apk
f1389427f687a0cd7b945b7ad733a446d2f7236fa01c7fd6025b97abfdbecd53  mgl-panvk-v49.apk
677d81eb29c7c57938573dfd9de20d39ff7f0557d466d3ed7ade9510033fc4b9  mgl-panvk-v50.apk
4914a8d7101668ad30105a16f36a57f4c90a08cc8069ab095b95edadbda051cc  mio-mgl-icd.apk
a5d68977b3574b5772754aeb9d40a7c60f6130904e80099ac3be3b0d5838808d  mobilegl-renderer.apk
ffad07a636f1dec2b0810565b93d33d64dcc9f2570a4c7ebc5db67e413a39365  panvk-g720.apk
d67a326c85c3620da9cc06d694e08d92639ab9cdc067476addf70a478e7f31da  panvk-shim.apk
49418c9d177e9fd982602d5dae4d770b04016cf847760a38783928dbd431da5e  panvk-zenith.apk
62c71f43b2c4b1139813e7ee058c5185f2e65c3583fc0d5629a5daa34cc6119d  restore-good-v26.apk
16266b5e1a8ffabacef76d50d53f932f439660b6e07758301b87014f37b62776  slot-driver.apk
```

（共 **47** 个 `*.apk`；上表覆盖其中主线 39 个 + 其它 8 个。）

---

### E.1 追加：`v51`–`v53` 的实测输出（2026-10-05）

> 上面的 §E 是**当时那一次** `sha256sum *.apk` 的原始输出，**保持原样不改**；
> v51–v53 是其后新增的产物，故单列于此（同机、同法实测）：

```
6e9ce7d261b07b996a6e60c80a5ad598f1e950b813716ff540c451283f8b0313  mgl-panvk-v51.apk
0bbef03083e74cfb77be37efce18853eba0945857fd185ad3d7879e5eef61d23  mgl-panvk-v52.apk
9c99af82d51b54c3ca7f97d1ffd206f5edad0f534490afa7c34be1dfc073d48e  mgl-panvk-v53.apk
58ef996f9cfd5dbd88c37daea4f02bbde4fdee5818561526d21cf9adadefbec4  libvulkan_panfrost.so   (v53 驱动件，另存 /root/dist-v53/)
```


## F 相关文件

- 里程碑与逐版本证据：[`CHANGELOG.md`](CHANGELOG.md)
- 综述论文（含版本与哈希附录）：[`research/paper.md`](research/paper.md) 附录 A
- 源码来源与用途：[`source/README.md`](source/README.md)
- 工程实录：[`docs/09-mobilegl-integration.md`](docs/09-mobilegl-integration.md)
