# mali-panvk-toolkit

**在免 root 的 Android 设备上，让 Mali GPU 用上开源驱动（Mesa PanVK / kbase 后端），
并把它接进 Minecraft 启动器（Zalith Launcher 2 / FCL），最终跑起商业游戏。**

本仓库是这条路**走通的部分**与**走不通的部分**的完整沉淀：
探测/诊断/打包工具、自建 Vulkan 转发垫片源码、失败点的逐一分析与修复、产物台账、
研究论文与结论摘要。所有工具与结论都在
**OPPO PHZ110 / 天玑 9300（MT6989）/ Immortalis-G720 MC12 / Android 16 (SDK 36) / 无 root** 上实测过。

> **本仓库是这条线的唯一权威记录**：工程实录在 [`docs/09-mobilegl-integration.md`](docs/09-mobilegl-integration.md)（25 节），
> 专题研究在 [`research/`](research/)（12 篇正文 + 中文摘要 + [`paper.md`](research/paper.md) 综述论文），
> 产物台账在 [`MANIFEST.md`](MANIFEST.md)，里程碑在 [`CHANGELOG.md`](CHANGELOG.md)。
> 凡"未验证/不一致"的内容，各处均已如实标注。

---

## 1. 已达成的里程碑

### ★ 判据行（游戏日志原文，真机实测）

```
[10:46:13] [Render thread/INFO]: OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)
```

三项**只可能来自我们自编的 PanVK**，厂商 blob 不会出现：

| 判据字段 | 我们的 PanVK | 厂商 blob（反例） |
|---|---|---|
| 设备名 | `Mali-G720 MC12` | `Mali-G720-Immortalis MC12` |
| Vulkan API | `1.4.363` | `1.3.247` |
| Driver | `26.2.99` | `44.1.0` |

**含义**：Minecraft 26.3 的渲染链路
`MobileGL（DirectVulkan / Magma）→ 自建转发垫片 → 我们自编的 Mesa PanVK → /dev/mali0（kbase）`
**已经成立**。达成版本 = 插件 APK **`mgl-panvk-v47`**（sha256 前缀 `bb689838`），
关键差异是 `pojavEnv` 里增加 `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`（[`docs/09` §22](docs/09-mobilegl-integration.md)、[`CHANGELOG.md`](CHANGELOG.md)）。

### 里程碑清单

| # | 里程碑 | 判据/证据 |
|---|---|---|
| 1 | 免 root 驱动 Mali kbase 通路成立 | `/dev/mali0` 为 `crw-rw-rw-`，任何 uid 可开；自检全 PASS（[`docs/02`](docs/02-kbase-bringup.md)） |
| 2 | 自编 G720/v12 PanVK 可用（ICD 形态） | 探针：`Mali-G720 MC12` / API `1.4.363` / **181 扩展**（[`docs/09` §8](docs/09-mobilegl-integration.md)） |
| 3 | MobileGL DirectVulkan 后端在 G720 上跑 26.3 | **FPS 59–60**（当时走厂商 blob，[`docs/09` §7](docs/09-mobilegl-integration.md)） |
| 4 | 渲染器插件被 ZL2 正确识别并选中 | `▷ Renderer: MobileGL Magma + PanVK`（[`docs/09` §7](docs/09-mobilegl-integration.md)） |
| 5 | 自建 **125 入口** Vulkan 转发垫片（导出 125/125，双路自查通过） | [`source/shim/`](source/shim/) + [`docs/09` §12](docs/09-mobilegl-integration.md) |
| 6 | 垫片**静态链入 MGL**（UND `vk*` = 0） | `readelf --dyn-syms`（[`docs/09` §18.1](docs/09-mobilegl-integration.md)） |
| 7 | loader 语义打通：123 项 thunk 表 + 三级分派 | 队列族 `count=1 / flags=0x7 queues=2`、feature `xor` 全 0（[`docs/09` §20.2](docs/09-mobilegl-integration.md)） |
| 8 | `vkCreateDevice` 成功 | MGL 日志中不再出现 `vkCreateDevice FATAL` |
| 9 | **判据行达成**（上面那条） | `mgl-panvk-v47`（[`docs/09` §22](docs/09-mobilegl-integration.md)） |
| 10 | **`VK_ERROR_DEVICE_LOST (-4)` 被钉在「纹理上传的 `vkQueueSubmit`」**（v48 诊断加深；v49 关掉 dma-heap 做反向对照 ⇒ **错误码不变**，排除 dma-heap/dma-buf） | [`docs/09` §23](docs/09-mobilegl-integration.md) |
| 11 | **v50：真 Android 交换链补丁**（05 方案 A + MR !43659 式 LINEAR 推断，3 文件严格加性；新 `.so` md5 `e08e0764…`）+ **构建目录判定闭合**（只有 `build/android-v4` 有产物、`platforms=['android']`、`wsi_x11` 计数 0） | [`docs/09` §24](docs/09-mobilegl-integration.md)、[`research/11`](research/11-wsi-patch-implementation.md) |
| 12 | ★★ **独立探针：驱动渲染被证明**（`render` 像素 64/128/191/255 三点精确、`failures=0`；`win` 的 present 真落到窗口 buffer）**且原假设不能复现**（`win`/`headless`/`winimpdef` 三路 `vkCreateSwapchainKHR` 全 `VK_SUCCESS`）；★ **真正掉线的是「绘制」** ⇒ `tri` 的 `vkQueueSubmit` 成功但 `vkWaitForFences = -4`，kbase 报 **CSF fatal exception `0xc3`** | [`docs/09` §25](docs/09-mobilegl-integration.md)、[`research/12`](research/12-probe-run-results.md) |

### 同样重要的**负面**结论（可复用价值最高）

| # | 结论 | 证据 |
|---|---|---|
| N1 | **Android 系统 Vulkan loader 忽略 `VK_ICD_FILENAMES`** —— 变量确实注入了进程，loader 仍选厂商 blob（用 apiVersion/driverVersion 版本号做铁证，而不是设备名） | [`docs/09` §9①/§11](docs/09-mobilegl-integration.md) |
| N2 | **在插件 lib 目录顶替 `libvulkan.so.1` 会让 JVM 卡死**在 `[DEBUG] Calling JLI_Launch`，与垫片是否"完整"**无关**（裸 ICD 与 125 入口完整垫片症状相同） | [`docs/09` §9②/§13](docs/09-mobilegl-integration.md) |
| N3 | **`/storage`（FUSE）是 noexec** —— 驱动 `.so` 放 `/sdcard` 必然 `Permission denied`；可执行路径只有插件自己的 `nativeLibraryDir` | [`docs/09` §8](docs/09-mobilegl-integration.md) |
| N4 | **插件 lib 目录不在 launcher 命名空间（`clns-9`）搜索路径**（`ld_library_paths=""`）⇒ 裸名 `DT_NEEDED` 永远找不到；正解是 `pojavEnv: DLOPEN=` 预加载 + 垫片**必须带 `DT_SONAME`** | [`docs/09` §15/§17](docs/09-mobilegl-integration.md) |
| N5 | **`/dev/dma_heap/system` 权限 0444**。⚠️ **本条的因果链已更正**：0444 读的是**旧树** `/root/mesa`（`O_RDWR`）；**构建树**用的是 **`O_RDONLY`**，对 0444 节点**能打开成功** ⇒ `kbase_kmod_supports_dmabuf()` 实为 **true**（"`sw_device=true`"不成立），失败被推迟到分配时的 `DMA_HEAP_IOCTL_ALLOC`（**该步是否失败尚未在设备侧取证**） | [`research/06`](research/06-panvk-wsi.md)（更正见 [`docs/09` §23.3/§23.4](docs/09-mobilegl-integration.md)） |
| N6 | **纯 NDK 无法绕开 gralloc**：整个 Android 公开 API 都没有返回 **DRM modifier** 的函数，而 panvk **强制要求** modifier | [`research/03`](research/03-libgralloctypes.md)、[`research/06`](research/06-panvk-wsi.md) |
| N7 | **本机 5 个既有 `u_gralloc` 后端全部不可用**（imapper4/5 在 `-Dandroid-stub=true` 下**根本没编**）⇒ 必须新增后端或打补丁 | [`research/04`](research/04-mesa-ugralloc.md) |

---

## 2. 仓库结构导览

```
mali-panvk-toolkit/
├── README.md                 ← 你在这里：项目总览 + 里程碑 + 复现入口
├── MANIFEST.md               ← 产物台账：v16→v50 APK 的文件名/大小/sha256 前缀/用途
├── CHANGELOG.md              ← 按里程碑的进展台账（每条标注证据位置）
├── NOTICE.md / LICENSE       ← 第三方来源与许可（MIT）
│
├── docs/                     ← 工程实录与分主题文档
│   ├── 09-mobilegl-integration.md   ★★ 25 节，主战场实录（原文日志 + 源码行号；§23–§25 = v48/v49/v50 + 真机探针）
│   ├── 01-why-mali.md … 08-mobilegl-vulkan.md   背景/构建/发现/路线图
│   └── README.md
│
├── research/                 ← 研究论文与结论摘要
│   ├── paper.md              ★ 综述论文（摘要/引言/背景/方法/6 次失败点迁移/证据/讨论/附录）
│   ├── 01-aidl-route.md … 12-probe-run-results.md   12 篇专题正文
│   ├── 11-wsi-patch-implementation.md   ★ v50 WSI 补丁实施 + 构建目录实测判定
│   ├── 12-probe-run-results.md          ★★ 真机探针 8 模式结果（渲染被证明 / CSF 0xc3）
│   ├── summaries/            中文结论摘要（决策用）
│   ├── 00-paper-skeleton.md  论文骨架（写作过程记录）
│   └── README.md             完整索引（正文 + 摘要 + 状态）
│
├── source/                   ← 源码与脚本（我们的转发垫片 + 打包链）
│   ├── shim/                 转发垫片：vkshim_mgl.c / gen_shim.py / shim.c / vkund.txt / build_shim.sh
│   ├── pack/                 打包脚本与插件清单模板：pack_mgl_plugin.sh / AndroidManifest.v46.xml
│   └── README.md             每个文件的来源与用途
│
├── tools/                    ← 设备侧探测/诊断/打包工具
│   ├── kbase_probe.py        免 root 探测 kbase 通路（秒级）
│   ├── kbase_selftest.c       kbase 全链路自检（握手/属性/分配/mmap 校验）
│   ├── vkicd_probe.c          任意驱动 .so 冒烟测试：ICD/HAL 形态 + 设备名/API/扩展
│   ├── zink_check.c           驱动能力 vs Zink 需求逐条对照
│   ├── wsiprobe.c             窗口系统扩展探测（`VK_KHR_android_surface` 等）
│   ├── vkshim_icd.c          ⚠️ 记录一条**已证明走不通**的路（见下方"坑 2"）
│   └── make_driver_apk.sh     把驱动 .so 打包成 FCL/ZL2 可识别的驱动插件 APK
│
├── build/                    ← 构建配方（G720/v12 PanVK）
├── ci/                       ← GitHub Actions 工作流模板（`.example`，见下）
└── fcl-patch/                ← 拆 Adreno 厂商锁的插件构建脚本
```

---

## 3. 快速复现路径

> 完整步骤（含每一步的**判据**、注意事项与未实测标注）见
> [综述论文 附录 B](research/paper.md)。这里给最短路径。

### 3.0 不想自己搭构建机？用 GitHub CI

本仓库带工作流模板 [`ci/build-panvk.yml.example`](ci/build-panvk.yml.example)。
复制成 `.github/workflows/build-panvk.yml` 并提交（GitHub 要求推送该目录的 token 具备 `workflow` 权限，
所以这里以模板形式提供），然后在 **Actions → Build PanVK (kbase / Android) → Run workflow**
里选好 profile（默认 `g720-v12-csf`），跑完在 Artifacts 里下载 `libvulkan_panfrost.so`
—— [`docs/04`](docs/04-build-g720-panvk.md) 里那一长串依赖坑，脚本里已经全部处理好了。

### 3.1 我的设备能用开源 Mali 驱动吗？

```bash
python3 tools/kbase_probe.py
# 判据：/dev/mali0 普通应用可打开（mode 0666）+ 握手 ioctl 返回成功
# 注意：/dev/dri/* 需要 root，不是免 root 路线
```

彻底验证（需 NDK）：

```bash
aarch64-linux-android26-clang -O2 -o kbase_selftest tools/kbase_selftest.c
./kbase_selftest          # 全部 PASS 才说明能真正驱动 GPU
```

### 3.2 拿到一个驱动 `.so`，先验它能不能用

```bash
aarch64-linux-android26-clang -O2 -o vkicd_probe tools/vkicd_probe.c -ldl
./vkicd_probe /path/to/libvulkan_xxx.so
# 输出：能否 dlopen、是 vk_icd*（ICD 形态）还是 HMI（HAL 形态）、GPU 名、API 版本、扩展清单
# 注意：必须从「可执行目录」加载 —— /storage 是 noexec
```

### 3.3 生成垫片 + 打包 + 安装 + 读判据

```bash
# ① 生成 125 入口转发垫片（源码见 source/shim/）
python3 source/shim/gen_shim.py && bash source/shim/build_shim.sh
#   自查：readelf 导出 vk* 数 == 125，且与 vkund.txt 求差为空
# ② 打包渲染器插件（模板：source/pack/AndroidManifest.v46.xml）
bash source/pack/pack_mgl_plugin.sh ./libvulkan_freedreno.so "MobileGL Magma + PanVK" mgl
# ③ 安装（新包名被拦时可复用已安装包名 + 更高 versionCode ⇒ 变成更新）
cmd package install -r -t /data/local/tmp/plugin.apk
# ④ 读判据
grep -a "OpenGL Renderer" \
  "/storage/emulated/0/Android/data/com.movtery.zalithlauncher.v2/files/.minecraft/versions/26.3 Fabric/ZalithLauncher/latest_game.log"
#   期望 Mali-G720 MC12, Vulkan 1.4.363   ← 我们的驱动
#   反例 Mali-G720-Immortalis MC12, Vulkan 1.3.247   ← 厂商 blob
```

辅助日志：`cat /sdcard/MG/vkshim.log`（垫片落盘日志，不受 logcat 环形缓冲影响）、
`tail -5 /sdcard/MG/latest.log`（MGL 侧）。

### 3.4 插件清单里必须有（缺一不可）

```xml
<meta-data android:name="fclPlugin" android:value="true" />
<meta-data android:name="des"       android:value="MobileGL Magma + PanVK" />
<meta-data android:name="renderer"  android:value="magma_panvk:libMobileGL.so:libMobileGL.so" />
<meta-data android:name="pojavEnv"  android:value="LIBGL_ES=3:POJAV_RENDERER=opengles3:
    MOBILEGL_BACKEND_TYPE=DirectVulkan:MOBILEGL_ESPRYT_USE_ANGLE=0:
    MOBILEGL_MAGMA_R11G11B10F_FALLBACK=0:DLOPEN=libvkpanvk_shim.so" />
```

`pojavEnv` 用**冒号**分隔（路径里不能含冒号）；`DLOPEN` **只有 `pojavEnv` 会被解析**，写 `boatEnv` 无效。

---

## 4. 关键坑（踩坑记录）

### ⚠️ 坑 1：启动器的 Adreno 厂商锁

FCL 与 Zalith Launcher 在原生层都有 `checkAdrenoGraphics()`，硬编码只认 Qualcomm Adreno；
自定义驱动注入路径 `loadTurnipVulkan()` **第一步就是这个检查**，Mali 上必然为假 ⇒ 走回系统 `libvulkan.so`（厂商 blob）。
分析、日志证据与三种拆锁方案见 [`docs/03-fcl-adreno-lock.md`](docs/03-fcl-adreno-lock.md)。

### ⚠️ 坑 2：`LD_LIBRARY_PATH` 在进程启动后无效

启动器运行时 `setenv("LD_LIBRARY_PATH", …)` 想让自己插件里的 `libvulkan.so` 盖过系统的 —— **无效**，
Android 的 linker **只在进程启动时**读取该变量。
这条路（[`tools/vkshim_icd.c`](tools/vkshim_icd.c) 就是为它写的）**已实测证明不通**。

### ⚠️ 坑 3：驱动插件必须有 launcher Activity

FCL 用 `queryIntentActivities(Intent(ACTION_MAIN))` 扫描插件。APK 里只有
`<meta-data name="fclPlugin">` 而没有带 `LAUNCHER` 的 Activity，**启动器根本看不到它**。

### ⚠️ 坑 4：`environment` meta-data 是 JVM 参数，不是环境变量

原生库插件的 `environment` 里写 `PANVK_SHIM=1` 会变成 JVM 参数 `PANVK_SHIM=1`
→ `ClassNotFoundException`。要写成 `-Dname=value`。

### ⚠️ 坑 5：驱动形态要匹配加载方式

- 启动器按 **HAL** 加载：`linker_ns_dlopen` + 系统加载器 → 需要 `HMI` 导出
- 按 **ICD** 加载：需要 `vk_icd*` 导出

两者不通用；[`tools/vkicd_probe.c`](tools/vkicd_probe.c) 可以一眼看出是哪种。

### ⚠️ 坑 6：没有可观测性就什么都查不出来

**Android 会丢弃 app 的 `stderr`** —— 垫片里的 `fprintf(stderr, …)` 在真机上 `logcat` 里 **0 行**；
`VK_LOADER_DEBUG=all` 的输出**同样进不了 logcat**。
必须走 `__android_log_print` **并且**落盘双写（`/sdcard/MG/vkshim.log`）。
本项目后期的每一次定位都建立在这条通道上（[`docs/09` §13/§18.4](docs/09-mobilegl-integration.md)）。

---

## 5. 当前状态与未决问题

### 5.1 已经确定的

`vkCreateInstance` ✓ · `vkCreateDevice` ✓ · 队列族 `flags=0x7 queues=2` ✓ ·
feature 逐位一致（`xor` 全 0）✓ · surface/交换链参数全部合法 ✓ · **判据行达成** ✓ ·
**驱动渲染被独立证明** ✓（探针 `render` / `ahb` / `win` 三模式 `failures=0`，像素精确回读）

### 5.2 当前首要未决问题

> ★ **首要未决问题 = kbase CSF fatal exception `0xc3`**。
> 真机探针证明：**命令提交 / 内存 / 导入链路是通的，坏在图形管线的实际光栅化执行** ——
> `tri` 模式 `vkQueueSubmit` 返回成功、`vkWaitForFences` 得 `-4`，kbase 报三个 CSF group
> `fatal error: status 0x7dc002c3 (exception 0xc3)`；而**不含 draw 的 clear+copy 全部正常**
> （[`docs/09` §25.6/§25.7](docs/09-mobilegl-integration.md)、[`research/12`](research/12-probe-run-results.md)）。
> ⇒ **WSI 不是最终瓶颈，CSF 绘制执行才是**；原来的"修 WSI"目标已降级
> （探针还证明"交换链创建必然失败"这个原假设**不能复现**：`win`/`headless`/`winimpdef` 三路全 `VK_SUCCESS`）。

| # | 问题 | 现状 | 依据 |
|---|---|---|---|
| U1 | ★ **CSF exception `0xc3`**（= `VK_ERROR_DEVICE_LOST (-4)` 的真身）：真正执行 draw 时三个 CSF group 同时 fatal | **首要目标**；取证方向见 §25.7（`logcat -b all` / `dmesg` 抓 mali/kbase fault + 最小化 draw 对照） | [`docs/09` §25](docs/09-mobilegl-integration.md)、[`research/12`](research/12-probe-run-results.md) |
| U2 | **双栈隐患**：MGL 直连符号走我们的 ICD，而 ZL2 自己的 `load_vulkan()` 又 `dlopen("libvulkan.so")`（系统 loader → blob）并把句柄交给 MGL ⇒ "swapchain/surface 由 blob 建、却塞给我们的 ICD" | 需消掉双栈（改 MGL 本体 / 自建 launcher / 写 WSI 判别探针） | [`docs/09` §17](docs/09-mobilegl-integration.md) |
| U3 | **"出画面"仍依赖 WSI 修复**：v47/v50 之前用的是 `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`（headless 交换链，`queue_present` 空操作）——它让判据行出现，但**本身不是出画面的方案**；v50 已删掉该开关、改走真交换链，但**未上机** | 三条路线对比见 [`research/paper.md` §6](research/paper.md)；v50 待上机 | [`research/07`](research/07-mobilegl-wsi.md)、[`research/05`](research/05-bypass-patch.md)、[`docs/09` §24](docs/09-mobilegl-integration.md) |
| U5 | **`driverVersion` 两处记录不一致**：探针报 `26.2.24.3`，真机日志报 `26.2.99` | 未核对是否同一份 `.so` | [`docs/09` §8 vs §11/§17/§22](docs/09-mobilegl-integration.md) |
| U6 | **机型写法不一致**：`docs/09` §7 写 `OPPO PHX110`，§8 与实测写 `PHZ110` | 取 **PHZ110**，PHX110 视为笔误 | [`research/10`](research/10-verify-probe.md) |
| U7 | **`research/09`（真实世界先例）没有中文摘要** | 缺口 | [`research/README.md`](research/README.md) §3 |
| U9 | [`docs/09` §15](docs/09-mobilegl-integration.md) 关于"被 `patchelf` 改过 `DT_NEEDED` 的 MGL 被提前加载"是**假设**，当轮**未被证实** | 需按原文给的验证方法做 A/B | [`docs/09` §15](docs/09-mobilegl-integration.md) |
| U10 | **v50（真 Android 交换链补丁）从未在真机运行** ⇒ 只有静态/链接层验证，没有运行时证据；`-4` 也不能声称已被它消除（该链不经过本补丁） | 待上机（覆盖安装即可，同包名同签名） | [`docs/09` §24.5](docs/09-mobilegl-integration.md)、[`research/11`](research/11-wsi-patch-implementation.md) §7 |
| U11 | **唯一可复现的 `VK_ERROR_INVALID_EXTERNAL_HANDLE (-1000072003)` 只在 AHB 用 `IMPLEMENTATION_DEFINED(0x22)` 分配时出现**（MESA `Failed to get u_gralloc_buffer_basic_info`），而真实 App 的 Surface 用的正是该格式 | 与 U1 是**两条独立线**，不要合并看 | [`docs/09` §25.5](docs/09-mobilegl-integration.md)、[`research/12`](research/12-probe-run-results.md) §6 |
| U12 | **`O_RDONLY` 打开的 dma-heap fd 下 `DMA_HEAP_IOCTL_ALLOC` 是否成功 / `kbase_kmod_supports_dmabuf()` 的实际返回值** | 仅**源码推断**（"必然失败"），未在设备侧取证；`research/06` 的注释给出**相反**推断 | [`docs/09` §23.3/§23.4](docs/09-mobilegl-integration.md) |

**✅ 本轮已闭合（从"未决"移出）**

| 原 # | 事项 | 闭合依据 |
|---|---|---|
| ~~U4~~ | **构建目录矛盾**（[`research/05`](research/05-bypass-patch.md) vs [`research/06`](research/06-panvk-wsi.md)） | ✅ **已闭合**：4 个 build 目录中**只有 `/root/zenithblue/build/android-v4` 有产物**；`strings \| grep -c wsi_x11` = **0**、`build.ninja` 中 x11 = **0** 次、meson `platforms=['android']` ⇒ **05 号对、06 号的"出厂件含 x11 WSI"不成立**（`android-deps-x11` 只是 include 目录名）。见 [`docs/09` §24.4](docs/09-mobilegl-integration.md)、[`research/11`](research/11-wsi-patch-implementation.md) §1 |
| ~~U8~~ | 探针未上机；方案 A 未上机 | ✅ **探针已上机**（8 模式，[`docs/09` §25](docs/09-mobilegl-integration.md)）；**方案 A 已实施并编入 v50**（[`docs/09` §24](docs/09-mobilegl-integration.md)）。⚠️ 但 v50 本身仍未上机（保留为 **U10**） |
| ~~U10(旧)~~ | v48/v49/v50 的运行结果无记录 | ✅ **已闭合**：[`docs/09`](docs/09-mobilegl-integration.md) 新增 **§23（v48/v49）/§24（v50）/§25（探针）**，[`CHANGELOG.md`](CHANGELOG.md) 新增 **M12** |

> ⚠️ 另有一条**硬约束**贯穿始终：**驱动二进制不进本仓库**（体积 + 许可）。
> 本仓库只收**知识 / 源码 / 清单**；APK/`.so` 只以**文件名 + 大小 + sha256 前缀 + 一句话用途**的形式
> 记入 [`MANIFEST.md`](MANIFEST.md)。

---

## 6. 约定：每次进展都提交 git

本项目把 git 当作**唯一的进度载体**。约定如下：

1. **每一步进展都提交**，不攒大提交。一次实验（哪怕失败）就是一次提交：
   提交信息里写清**做了什么、看到了什么、下一步是什么**。
   > 现实例证：[`docs/09`](docs/09-mobilegl-integration.md) 的记录方式就是"一节一次提交"
   > （例如 `docs(09): 补实测结果 —— MobileGL Direct(Vulkan) 在 G720 上 59-60FPS 验证通过`）。
2. **失败也必须进仓库**。本文档里价值最高的若干条（N1–N4）都是**负面结论**；
   只记录成功会导致后来者重复踩同一个坑。
3. **提交信息里带证据定位**（`docs/09 §N` 或 `research/NN`），便于回溯。
4. **产物只进清单不进仓库**：APK/`.so`/`.a`/build 目录由 [`.gitignore`](.gitignore) 排除，
   改为在 [`MANIFEST.md`](MANIFEST.md) 登记（文件名 / 大小 / sha256 前缀 / 用途 / 验证目标）。
5. **台账与正文同步更新**：新增一份产物 ⇒ 更新 `MANIFEST.md`；新增一个里程碑 ⇒ 更新 `CHANGELOG.md`；
   新增一篇研究 ⇒ 更新 `research/README.md` 的索引表。
6. **未验证的写"未验证"**，不一致的写"不一致"，**不补数字、不圆场**。

---

## 7. 实测环境（可作为对照基线）

| 项 | 值 |
|---|---|
| 设备 | OPPO **PHZ110**，Android 16 (SDK 36)，**无 root** |
| SoC | 联发科天玑 9300（**MT6989**），`ro.hardware = mt6989` |
| GPU | Immortalis-G720 MC12，`PRODUCT_ID 0xc870`，1.3 GHz |
| 内核 | 6.1.157-android14 |
| kbase | `/dev/mali0`（**0666**，免 root 可开），CSF，**uAPI 1.21** |
| 内存模型 | `RAW_JS_PRESENT=0`、`RAW_COHERENCY_MODE=0`（非一致） |
| L2 | 512 KB / 4 slices |
| VNDK | `ro.vndk.version = 34` |
| 启动器 | Zalith Launcher 2（`com.movtery.zalithlauncher.v2`），游戏 `26.3 Fabric` |
| 厂商 blob（对照） | `Mali-G720-Immortalis MC12` / Vulkan `1.3.247` / Driver `44.1.0` |
| 我们的 PanVK | `Mali-G720 MC12` / Vulkan `1.4.363` / 扩展 181 |

---

## 8. 相关项目与致谢

- [Mesa3D](https://gitlab.freedesktop.org/mesa/mesa) —— PanVK 本体
- [zenithblue-oss/panvk-kbase-android](https://github.com/zenithblue-oss/panvk-kbase-android) —— PanVK kbase 后端基线（本项目构建配方的来源）
- [wonderkast02/panvk-g720-kbase-csf](https://github.com/wonderkast02/panvk-g720-kbase-csf) —— G720 / v12 CSF 参考实现
- [MobileGL](https://github.com/MobileGL-Dev/MobileGL) —— DirectVulkan（Magma）后端
- [Zalith Launcher 2](https://github.com/ZalithLauncher/ZalithLauncher2) / [Fold Craft Launcher](https://github.com/FCL-Team/FoldCraftLauncher) —— 目标启动器

## 9. 许可

工具与文档：**MIT**（见 [LICENSE](LICENSE)）。第三方来源与各自许可见 [NOTICE.md](NOTICE.md)。

> 注意：**不附带**任何 FCL / ZL2 的二进制产物，也**不附带** PanVK / MobileGL 驱动本体。
> 本仓库只提供**工具、脚本、源码、文档与清单**，由使用者自行获取上游产物。
