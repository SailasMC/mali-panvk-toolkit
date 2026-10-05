# 在免 root 的 Android 设备上让 Minecraft 启动器跑在自编开源 Mali 驱动上
## —— Mesa PanVK + MobileGL DirectVulkan 的端到端实现、失败点迁移与修复

> **写作口径（必读）**
> 本文**只写会话中已真实确立的事实**。每一条关键结论后面都标注来源：
> - **`docs/09 §N`** = 工程实录 [`../docs/09-mobilegl-integration.md`](../docs/09-mobilegl-integration.md) 第 N 节（含逐条原文日志与源码行号）；
> - **`research/NN`** = 本目录 [`NN-*.md`](.) 研究报告第 NN 篇；
> - **`summaries/xxx.md`** = 中文结论摘要。
>
> 凡属推断、未上机、或两处记录互相矛盾的，本文**一律标注**为「推断」「未验证」或「不一致，未核对」，
> 不做美化，不补数字。

---

## 摘要

在**免 root** 的消费级 Android 手机（OPPO PHZ110 / 联发科天玑 9300 MT6989 / Immortalis-G720 MC12 /
Android 16 SDK 36）上，我们把 Minecraft 的渲染链路
`Minecraft 26.3 → MobileGL（DirectVulkan / Magma 后端）→ Vulkan 入口 → 自编 Mesa PanVK → /dev/mali0（kbase）`
逐层打通，并在真机游戏日志中拿到了判定性证据（下称**判据行**）：

```
OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)
```

其中 `Mali-G720 MC12` / `Vulkan 1.4.363` / `Driver 26.2.99` 三项**只可能来自我们自编的 PanVK**
（厂商 blob 对应为 `Mali-G720-Immortalis MC12` / `1.3.247` / `44.1.0`），因此这是"自编开源 Mali 驱动
真的在跑商业游戏"的直接证据（`docs/09` §22）。

本文的贡献不在"跑通"本身，而在**这条路上被逐个钉死的失败点**。我们记录并分析了 **6 次错误点迁移**：
`WSI 段错 → MGL 内 NULL 函数调用 → vkCreateDevice = -3 → vkCreateDevice 成功 → 交换链 VK_ERROR_INVALID_EXTERNAL_HANDLE → 绘制执行（CSF exception 0xc3）`，
每一步都定位到**具体函数/具体源码行**并给出修法（`docs/09` §20.5、§23–§25）。其中若干结论是**否定性的**，
但可复用价值最高，例如：

- **Android 系统 Vulkan loader 忽略 `VK_ICD_FILENAMES`** —— 环境变量确实注入进程，但 loader 仍选厂商 blob；
  我们用 **apiVersion/driverVersion 版本号**（而非仅设备名）给出了铁证（`docs/09` §9①、§11）。
- **在插件 lib 目录里顶替 `libvulkan.so.1` 会让 JVM 卡死**在 `JLI_Launch`，与垫片是否"完整"无关
  （裸 ICD 与 125 入口完整垫片症状完全相同）（`docs/09` §9②、§13）。
- **`/storage`（FUSE）是 noexec**，任何把驱动 `.so` 放在 `/sdcard` 的方案都是死路；
  ICD 的 `library_path` **只能**指向插件自身的 `nativeLibraryDir`（`docs/09` §8、§17(a)）。
- **插件 lib 目录不在 launcher 命名空间（`clns-9`）的搜索路径里**（`ld_library_paths=""`），
  裸名 `DT_NEEDED` **永远找不到**；正解是 ZL2 的 `pojavEnv: DLOPEN=<lib名>` 预加载键，
  且垫片**必须**带 `DT_SONAME`（`docs/09` §15、§17(b)(c)）。

通路达到"判据行成立"后，我们**没有停在"下一关是 `VK_ERROR_DEVICE_LOST (-4)`"这句话上**：
v48/v49 把它钉在**纹理上传的 `vkQueueSubmit`**，而一台**独立探针**（8 模式，`research/12`）随后
**推翻了"交换链建不起来"这个前提**，并把 `-4` 的真身定为 **kbase CSF fatal exception `0xc3`**
——它发生在**真正执行 draw** 时（`docs/09` §23–§25）。**本文的最终结论因此是：WSI 不是最终瓶颈，
CSF 的绘制执行才是**（第 7 节）。

**一句话**：免 root 消费级 Android 上，"自编开源 Mali 驱动驱动商业游戏"在**渲染器链路层面已经成立**；
失败的边界从"能不能加载"一路后移到"gralloc / WSI / 队列提交"，如今停在"**GPU 固件在执行绘制时掉线**"。

---

## 1 引言

### 1.1 动机

免 root 的 Android 上，图形栈对普通应用只有一个入口：厂商的 Vulkan blob 驱动。
在本项目面对的**新版本 Minecraft**（ZL2 上的 `26.3 Fabric`）场景里，厂商驱动的能力**不够用**：

| | 厂商 blob | 我们自编的 Mesa PanVK |
|---|---|---|
| `deviceName` | `Mali-G720-Immortalis MC12` | `Mali-G720 MC12` |
| `apiVersion` | **1.3.247** | **1.4.363** |
| `driverVersion` | **44.1.0** | 26.2.24.3 / 26.2.99（两处记录，见 §3.5 注） |
| 扩展数 | — | **181**（独立探针实测） |

（数据来源：`docs/09` §11 的对照表、§8 的探针输出；差异不是"版本号好看"，而是**新版本 MC 需要的
Vulkan 能力只在 1.4 线**上。这条判断本身来自项目前期的路线选择，见 `docs/06-roadmap.md`。）

而开源 Mesa 的 **PanVK** 恰好提供这条线：它是纯 ICD 形态，且它的 **kbase 后端**让我们可以**绕过 DRM**，
直接走 `/dev/mali0`（`docs/02-kbase-bringup.md`）。

### 1.2 目标与约束

**目标**：让 Minecraft 启动器（首次目标是 ZL2 = Zalith Launcher 2）**用上**我们自编的 PanVK。

**硬约束**（全部来自现场实测，不是假设）：

1. **无 root**。可用的内核入口只有 `/dev/mali0`（`crw-rw-rw-`，任何 uid 可开）（`summaries/10`、`research/06`）。
2. **`/storage`（FUSE）是 noexec** —— 从 `/sdcard` 加载驱动必然 `Permission denied`（`docs/09` §8）。
3. **`/dev/dri` 不可用** —— 本机有 `card0`（0660 system:system）但**无 `renderD*`**，
   而 panvk 要求 DRM render node（`research/06` §10）；实测 `/dev/dri/*` 全 EACCES。
4. **gralloc 是 binderized 服务**，走不到"旧式 `hw_get_module` + `/dev/dri`"那条传统路径（`research/04`、`research/10`）。
5. **启动器侧有厂商锁与契约差异** —— FCL / ZL2 的原生层 `checkAdrenoGraphics()` 硬编码只认 Qualcomm Adreno
   （`docs/03-fcl-adreno-lock.md`）；且两者的渲染器插件契约**并不相同**（`docs/09` §3）。
6. **`LD_LIBRARY_PATH` 在进程启动后无效** —— Android linker 只在进程启动时读取它，
   所以"运行时 setenv 让自己插件里的 `libvulkan.so` 盖过系统库"这条路**已实测证明不通**（仓库根 `README.md` 坑 2、
   `tools/vkshim_icd.c` 就是为它写的）。

约束 1–4 决定了"必须有一个能在 app 域、无 root、无 DRM 的 Vulkan 驱动"，
约束 5–6 决定了"必须有一个**不依赖 Android 系统 loader 配合**的注入方式"。

### 1.3 本文的贡献与组织

本文不是"成功故事"，而是一份**失败点的完整台账**：

- 第 2 节给出设备与系统的**事实基线**（供他人对照复现）；
- 第 3 节给出**方法**：渲染器插件注入 → 纯 ICD 与 loader 的差距 → 自建转发层 → 可观测性；
- 第 4 节按**错误点迁移顺序**逐步分析并给出修复（共 6 次迁移）；
- 第 5 节把结论**逐条挂到证据**（`docs/09` 各节 + `research/01–12`）；
- 第 6 节对比**三条剩余路线**（修 WSI / 切 gallium 桥 / shim 层绕过）；
- 第 7 节结论与后续工作；附录给出**版本与哈希、复现步骤、参考文献**。

---

## 2 系统与设备背景

### 2.1 设备事实

| 项 | 值 | 来源 |
|---|---|---|
| 设备 | OPPO **PHZ110** | `research/10` 真机只读实测；`docs/09` §8 |
| SoC | 联发科天玑 9300（**MT6989**），`ro.hardware = mt6989` | `docs/09` §21.3、`research/04` |
| GPU | **Immortalis-G720 MC12** | 仓库根 `README.md` 实测基线 |
| 系统 | **Android 16（SDK 36）** | `research/10` |
| VNDK | `ro.vndk.version = 34` | `research/02` |
| kbase | `/dev/mali0` 为 **`crw-rw-rw- system:graphics`** ⇒ **uid 2000 无 root 也能建 instance/device** | `research/10`；`docs/09` §8 |

> **不一致记录（如实标注）**：`docs/09` §7 的标题把机型写作 `OPPO PHX110`，而 §8、§10-verify 与
> `research/10` 的实测均写作 **PHZ110**。本文取 **PHZ110**。"PHX110" 视为笔误，未再核对。

### 2.2 两类"驱动指纹"

本项目的全部判据都建立在**厂商 blob 与自编 PanVK 的可区分性**上：

| | blob | 我们的 PanVK |
|---|---|---|
| deviceName | `Mali-G720-Immortalis MC12` | `Mali-G720 MC12`（**不带 `-Immortalis`**） |
| apiVersion | `1.3.247` | `1.4.363` |

仅凭 deviceName 可能被巧合欺骗，**版本号才是铁证**（`docs/09` §11）。
这条方法论在第 4.3 节直接用于判定"loader 没加载我们的 ICD"。

### 2.3 运行时环境的三处硬限制

1. **`/storage` = noexec**（`docs/09` §8）：
   ```
   ✘ dlopen failed: couldn't map ".../libvulkan_freedreno.so" segment 2: Permission denied
   ```
   ⇒ 可执行路径只有**插件自己的 `nativeLibraryDir`**
   （`/data/app/~~XXXX==/<包名>-YYYY==/lib/arm64/`）；
   `/data/data/<pkg>/files` 自 Android 10 起也是 noexec，`/data/local/tmp` 仅对 shell 可执行。
2. **`/dev/dma_heap/system` 权限 0444**（`research/06` §〇）：
   实测 `O_RDWR` 失败、`O_RDONLY` 成功；而 `kbase_kmod.c:1269` 用 **`O_RDWR`** 打开 ⇒ 失败
   ⇒ `kbase_kmod_supports_dmabuf() = false` ⇒ panvk 落 `sw_device = true`、`supports_modifiers = false`
   ⇒ DRI3 / raw-fd 路径**一行都没走到**。
   > 这是本项目的**决定性事实之一**：它解释了"WSI 起不来"，并与 gralloc 侧的失败**同属一条根因链**。
3. **launcher 命名空间不含插件目录**（`docs/09` §17(a)）：
   ```
   WARNING: linker: ... not accessible for the namespace:
    [name="clns-9", ld_library_paths="",
     default_library_paths="<ZL2 自己的 lib/arm64>:<base.apk!/lib/arm64-v8a",
     permitted_paths="/data:/mnt/expand:/data/data/com.movtery.zalithlauncher.v2"]
   ```
   `ld_library_paths=""` ⇒ **绝对路径可 dlopen，裸名 `DT_NEEDED` 永远找不到**；
   且 `/storage/emulated/0/...` 不在 `permitted_paths` 里 ⇒ 再次封死"把驱动放 /sdcard"。

---

## 3 方法

### 3.1 渲染器插件注入

ZL2 的渲染器插件契约由 **ZL2 源码**给出（权威，`docs/09` §3；`research/08`）：

```kotlin
if (metaData.getBoolean("fclPlugin", false) ||
    metaData.getBoolean("zalithRendererPlugin", false)) {
    val rendererString = metaData.getString("renderer")     // "id:GL库:EGL库"，启动器会 dlopen
    val des            = metaData.getString("des")
    val pojavEnvString = metaData.getString("pojavEnv")      // "K=V:K=V"（冒号分隔），逐个 setenv
    minMCVer / maxMCVer
}
```

我们按该契约打的插件（`source/pack/AndroidManifest.v46.xml` 是模板）具备：

```
renderer  = magma_panvk:libMobileGL.so:libMobileGL.so     # 用独立 id，避免与社区版 magma 撞车
pojavEnv  = LIBGL_ES=3:POJAV_RENDERER=opengles3:MOBILEGL_BACKEND_TYPE=DirectVulkan:
            MOBILEGL_ESPRYT_USE_ANGLE=0:MOBILEGL_MAGMA_R11G11B10F_FALLBACK=0
```

三条关键契约事实：

1. `LIB_MESA_NAME` / `MESA_LIBRARY` 的值会被**自动补全插件的 nativeLibraryDir**，
   而**其它键原样传入**（`docs/09` §3；`docs/09` §15 的源码引用）—— 我们正是靠这一点注入任意 env。
2. **只有 `pojavEnv` 被解析**；写 `boatEnv` 在 ZL2 全树 **0 命中**（`research/08` §3、`docs/09` §17(b)）。
3. `DLOPEN=<lib名>`（逗号分隔）的语义是**纯预加载**：ZL2 拼成 `"$nativeLibraryDir/<lib>"`，
   在 dlopen 渲染器库**之前**调 `ZLBridge.dlopen(path, RTLD_GLOBAL|RTLD_LAZY)`；
   **它不修改任何库搜索路径**（`docs/09` §15 的 `RendererPluginManager.kt:110-125` /
   `RendererPlugin.kt:48` / `GameLauncher.kt:208-222` 原文）。
   同时 `getRuntimeLibraryPath()` 只返回 `super + jnaDir`，**不含插件目录**（`GameLauncher.kt:274-280`）。

**踩坑：静默安装被拦时的绕过**（`docs/09` §4）。新包名的 `cmd package install` 在收紧后的策略/ROM 上
返回 `Failure [-99]`。技巧：**把插件打成"已安装的包名"**，安装即变成**更新**，从而 `Success`：

```bash
# manifest: package="<某个已安装的包名>" android:versionCode="<更高>"
cmd package install -r -t /data/local/tmp/plugin.apk     # → Success
```

**踩坑：改别人签名的插件装不上**（`docs/09` §7）。重打包社区插件的包名 + 用我们的 key 签名
⇒ 因**签名不符**失败。可行做法是**复刻它的 meta-data 到自己能更新的包名里**。

### 3.2 纯 ICD 与 loader 的差距：自建 125 入口转发层

**问题**：Android loader 不认 `VK_ICD_FILENAMES`（第 4.3 节给铁证），且 `DT_NEEDED` 换驱动不可行：

| | 导出 | 谁调用 |
|---|---|---|
| 系统 `libvulkan.so`（loader） | **125 个** loader 风格入口 | MGL 链接期直接引用 |
| 我们的 `libvulkan_freedreno.so`（ICD） | 只有 `vk_icdGetInstanceProcAddr` / `vk_icdNegotiateLoaderICDInterfaceVersion` | loader 通过 `vk_icd*` 调用 |

实测（`readelf -sW --dyn-syms`，`docs/09` §10）：`libMobileGL.so` 未定义（UND）的 `vk*` 符号 **125 个**，
而我们的驱动**不导出** `vkCreateInstance` / `vkGetInstanceProcAddr` / `vkCreateDevice`。

**解法**：写一个导出这 125 个名字、内部 `dlopen` 我们的 ICD 并**逐一分发**的转发层，
再把它**以 `libvulkan.so` 之名**（或按 §4.4 的方式）交给 MGL。要点：

1. 从 MGL 取未定义符号清单 ⇒ `source/shim/vkund.txt`（**实测 125 行**，与 §10 的 125 吻合）；
2. **写生成器** `source/shim/gen_shim.py`：从 NDK 的 `vulkan/vulkan_core.h` + `vulkan_android.h`
   解析 `VKAPI_ATTR <ret> VKAPI_CALL <name>(<params>);`，自动生成转发函数
   （生成报告 `source/shim/gen_report.txt`：
   `总符号 125 · 泛型转发 119 · 手写特殊 6 · 解析路径分布 {device:61, device_global:42, instance:3, physdev:11, instance_create:2}`）；
3. 分派规则（`docs/09` §12）：
   - `VkInstance` / `VkPhysicalDevice` → `g_gipa(<首参>, "<name>")`
   - `VkDevice` / `VkQueue` / `VkCommandBuffer` → `g_gdpa(<设备>, "<name>")`（命令缓冲没有 device 时用**缓存的 `g_dev`**）
   - 无参数或首参不是可调度句柄（`vkCreateInstance` 等）→ `g_gipa(NULL, "<name>")`
   - `vkEnumerateInstanceLayerProperties` **自己实现**（0 层、`VK_SUCCESS`）
4. `g_gipa` / `g_gdpa` 来源：`dlopen(我们的 ICD)` → `vk_icdNegotiateLoaderICDInterfaceVersion(&7)`
   → `dlsym("vk_icdGetInstanceProcAddr")`；device 级经 `gipa(NULL,"vkGetDeviceProcAddr")` 取。
   最终源码见 `source/shim/vkshim_mgl.c`。

**生成器里两个必须避开的坑（都实际踩过，`docs/09` §12）**：

1. **正则不能跨 typedef**。`typedef\s+(.+?)\s*\(VKAPI_PTR\s*\*\s*PFN_<name>\)`（`re.S`）会从**文件里最靠左的 `typedef`**
   开始吞，把整段 struct/flag 定义当成"返回类型" —— 症状是 `vkCreateInstance` 的返回类型变成几 KB 的块，
   `vkCreateAndroidSurfaceKHR` 变成 `VkFlags VkAndroidSurfaceCreateFlagsKHR; …`。
   **修法**：返回类型组限制为 `[^;{}]*?`，并加 `;{}`/长度上限断言。
2. **`vkCreateInstance` / `vkEnumerateInstanceExtensionProperties` 不是无参** —— 它们有参数，
   只是首参**不是可调度句柄**；分类规则必须用"**首参类型**"而不是"有没有参数"。

**自查（一条命令，`docs/09` §12）**：

```bash
# 导出清单（必须排除 STT_FILE 伪符号，否则 vkshim.c 会被算进去凑成 126）
readelf -sW --dyn-syms libvkpanvk_shim.so | awk '$4!="FILE" && $7!="UND" && $8~/^vk/{print $8}' \
  | sort -u | wc -l      # 期望 125
# 与需求求差（期望空）
comm -23 <(sort -u need.txt) <(readelf -sW --dyn-syms libvkpanvk_shim.so \
  | awk '$4!="FILE" && $7!="UND" && $8~/^vk/{print $8}' | sort -u)
```

### 3.3 loader 语义：三级分派与 thunk 表

纯"三源回退"不够，还必须**实现 loader 的语义**（`docs/09` §18.2）：

- **三源回退**：`gp_inst` / `gp_dev` 依次尝试 instance GIPA → device GIPA → physical-device ProcAddr，
  保证任何句柄域都能取到入口；
- **123 项 thunk 表**：`vkGetInstanceProcAddr` / `vkGetDeviceProcAddr` **只返回自家 thunk**，
  而不是把驱动的函数指针直接透出。这样做的收益是"**句柄域统一 + 日志可观测**"
  （所有调用都经过我们这一层，才能计数与打点）—— 这正是后来能定位到"取到 NULL"的前提；
- **物理设备级优先**：`gipa_pd()` 必须**先**问 `g_pdpa`（`vk_icdGetPhysicalDeviceProcAddr`），
  再退回 instance GIPA。早期写成"instance GIPA 优先"，导致
  `vkGetPhysicalDeviceFeatures` / `…QueueFamilyProperties` / `…Properties` 拿到**错的函数指针**，
  症状是 `apiVersion = 540.1018.2112`、`viewport limit = 1852401253`、`timestampPeriod ≈ 2.7e26`
  这类**垃圾值**（`docs/09` §18.3、§19）。

### 3.4 可观测性：日志双写 `/sdcard/MG/vkshim.log`

两条**实测得到的观测性事实**决定了日志策略（`docs/09` §13、§18.4）：

1. **Android 会丢弃 app 的 stderr** —— 垫片里的 `fprintf(stderr, "[vkshim] …")` 在真机上
   **`logcat` 里 0 行**；
2. `VK_LOADER_DEBUG=all` 的输出**同样没进 logcat**（app 的 stderr 被丢），
   所以 loader 的内部诊断在这个环境下**拿不到**。

因此自 v38 起改为**双写**：`__android_log_print`（logcat）+ 追加写 `/sdcard/MG/vkshim.log`
（不受 logcat 环形缓冲影响，能稳定拿到 `vkCreateDevice` 的入参）
（`source/shim/vkshim_mgl.c` 的 `FLOG` 宏；`docs/09` §20.4）。
垫片还提供 `[vkshim] MISSING entrypoint: <name>`（最多 32 条）作为"驱动没交出某个入口"的最快线索（`docs/09` §12）。

### 3.5 驱动侧：PanVK 的构建与验证

- **构建**：MobileGL 用 CMake + C++23，四个编译坑（递归 clone 卡死、glslang 缺 SPIRV-Tools、
  `spirv.hpp11` 抢占包含路径、`BUILD_SHARED_LIBS=ON` 把 glslang 编成 `.so` 导致链接期
  `undefined symbol: glslang::SetThreadPoolAllocator…`）与解法见 `docs/09` §2；
  产物 308 MB（带调试符号）→ `llvm-strip --strip-unneeded` 后 **16.9 MB**。
- **PanVK 的独立验证**：从**插件 `nativeLibraryDir`** dlopen 成功，
  `deviceName = Mali-G720 MC12`、`apiVersion = 1.4.363`、**扩展 181 个**；
  且 ICD 形态齐全（`vk_icdNegotiateLoaderICDInterfaceVersion` / `vk_icdGetInstanceProcAddr` / `HMI`）
  （`docs/09` §8）。
- **Mesa 版本基准**：`26.3.0-devel`，HEAD `6598829`
  （= `funnymdzz/mesa@6598829019c`，与构建脚本里的 `MESA_COMMIT` 一致）（`research/04`）。
- **MobileGL 基线**：`4.6.0 MobileGL 26.09-dev, Direct (Vulkan) Backend, GIT@cbbaf77`
  （`docs/09` §7 日志原文）。

> **`driverVersion` 的两处记录不一致（如实标注，未核对）**：
> `docs/09` §8 的独立探针输出为 **`26.2.24.3`**，而 §11 的对照表与 §17/§22 的真机日志为 **`26.2.99`**
> （`drv=26.2.99`）。本文的**判据行**采用真机日志值 `26.2.99`。
> 两者是否指向同一份 `.so`、或 `driverVersion` 字段口径不同，**本仓库未做核对**。

---

## 4 失败点分析与修复（按错误点迁移顺序，共 6 次迁移）

### 4.0 迁移总表

以下六次迁移是**净进步**序列，每一次都前进一格（`docs/09` §20.5；`§18.2` 记录了其中前四步的早期形态；
第 ⑥ 次见 `docs/09` §23–§25）：

```
① libvulkan_freedreno.so  wsi_GetSwapchainImagesKHR+0x20   驱动 WSI 段错
② libMobileGL.so+0x961380                                  MGL 内 NULL 函数调用
③ vkCreateDevice = -3                                      队列族 count=0（转发层取指针失败）
④ vkCreateDevice 成功、队列族 flags=0x7、feature xor 全 0     ✓✓
⑤ 交换链：VK_ERROR_INVALID_EXTERNAL_HANDLE                  u_gralloc 转换失败 → 已被 headless WSI 绕开 ✓
                                                           （v50 又给出真交换链补丁，但未上机）
⑥ 绘制执行：VK_ERROR_DEVICE_LOST (-4) = CSF exception 0xc3  ★ 当前拦路者（探针定位，非 WSI 问题）
```

第 4.1–4.5 节逐条展开；第 4.6 节给出⑤之后的目标迁移（`DEVICE_LOST`），
**第 4.7 节给出第 ⑥ 次迁移的完整证据与"原假设被推翻"的过程**。

### 4.1 ① 驱动 WSI 段错：`wsi_GetSwapchainImagesKHR+0x20`

**证据**（JVM 崩溃报告原文，`docs/09` §16）：

```
#  SIGSEGV (0xb) at pc=0x000079b57af630, pid=11776, tid=11859
# Problematic frame:
# C  [libvulkan_freedreno.so+0xd7b630]  wsi_GetSwapchainImagesKHR+0x20
```

**这一步的意义是正向的**：`libvulkan_freedreno.so` 就是**我们自己编译的开源 Mali PanVK**，
它在这个游戏进程里**被加载 → 初始化 → 建 instance/device → 走到创建交换链**才崩。
也就是说，**整条链路已经成立**，只是最末端（WSI）出错。

**修复**：补 `vk_icdGetPhysicalDeviceProcAddr` 的**分派**（`docs/09` §18.2 的第一次修）。

### 4.2 ② MGL 内 NULL 函数调用：`libMobileGL.so+0x961380`

**现象**：崩点从"我们的驱动"移到了 **MGL 内部** —— 调用了一个 NULL 函数指针（`docs/09` §18.2）。

**修复**：**三源回退**（`gp_inst` / `gp_dev`）+ **123 项 thunk 表**（loader 语义，见 §3.3）。
修后出现 `Required extension found: VK_KHR_swapchain ✓` —— **扩展枚举正确了**（`docs/09` §18.2）。

### 4.3 ③ `vkCreateDevice = VK_ERROR_INITIALIZATION_FAILED (-3)`

**现场日志**（`docs/09` §19）：

```
[WARN] No graphics queue found on physical device. Picking a device that doesn't do graphics?
[FATAL] vkCreateDevice  →  VK_ERROR_INITIALIZATION_FAILED (-3)
```

**Mesa 源码级根因**（`docs/09` §19，源码原文）：

```c
/* src/panfrost/vulkan/panvk_vX_device.c:376 —— 创建队列时 */
switch (create_info->queueFamilyIndex) {
case PANVK_QUEUE_FAMILY_GPU:  return panvk_per_arch(create_gpu_queue)(...);   /* ✓ */
case PANVK_QUEUE_FAMILY_BIND: return panvk_create_bind_queue(...);            /* ✓ */
default:                      return panvk_error(dev, VK_ERROR_INITIALIZATION_FAILED);  /* ← -3 */
}
```

⇒ **`-3` 的真身 = MGL 请求的队列族不是 GPU/BIND**。
而"MGL 看不到图形队列"的根因，是**物理设备级查询走了 instance GIPA**，被返回了**错的函数指针**
（`docs/09` §19；同一现象的其它表征就是 §18.3 里的垃圾值）。

**逐步修法**（`docs/09` §19 的因果表）：

| 修法 | 版本 | 作用 |
|---|---|---|
| `gipa_pd()` **物理设备级优先** | v39 / `3.9-pdpa-first` / sha 前缀 `1e4c231f` | 让 `vkGetPhysicalDeviceQueueFamilyProperties` / `…Features` / `…Properties` 拿到**正确**函数 ⇒ MGL 能看到图形队列与真实 feature |
| **123 项 thunk 表** | v36 / `3.6-loader-semantics` | `vkGet*ProcAddr` 只返回自家 thunk ⇒ 句柄域统一、日志可观测 |
| **feature 屏蔽**（建 device 前 AND 掉驱动不支持的位） | v41 / `4.1-featmask` / sha 前缀 `acf64880` | 避免投机性提交被拒 |
| **日志双写** `/sdcard/MG/vkshim.log` | v38 / `3.8-filelog` | 不受 logcat 环形缓冲影响，能拿到 `vkCreateDevice` 入参 |

**A/B 二分定案（真正的最后一击）**：v43 在 shim 里同时打印"经 `gipa_pd` 解析的指针"与
"直接问 instance GIPA 的指针"，**一次运行即定论**（`docs/09` §20.1）：

```
QFam-diag: fn=0x0  viaInstanceGIPA=0x79bad17d28  pdpa=0x79bad793b0  gipa=0x79ba9c4d0c  g_inst=0x7b6cf91680
           ↑ NULL ✗   ↑ 有效 ✓    ⇒ 结论：转发层取指针取空了，驱动没问题
QueueFamilyProperties: count=0      ← 因为 fn=NULL，转发器跳过调用，count 保持 0
```

**根因（生成器的一处误判）**：生成器把"**首参含 `VkPhysicalDevice`**"的函数误判成设备级
（`VkPhysicalDevice` 里含子串 `VkDevice`），于是它们走 `gp_inst(physicalDevice, …)`
—— **把物理设备当成 instance 传给 ICD 的 GIPA**，查表必然落空。**共修正 4 处**（`docs/09` §20.1）：

```c
/* 之前 */ PFN_vkX fn=(PFN_vkX)gp_inst(physicalDevice,"vkX");
/* 之后 */ PFN_vkX fn=(PFN_vkX)gipa_pd(physicalDevice,"vkX");   /* 先 pdpa，再退回【真实 instance】 */
```

### 4.4 ④ 修复生效：队列族与 feature 全部对齐，`vkCreateDevice` 成功

**修正后的实测结果**（`docs/09` §20.2，质的飞跃）：

```
SurfaceCaps: min=2 max=4 cur=2376x1080 usage=0x17 alpha=0x9        ← surface 完全有效
QueueFamilyProperties: count=1
  family[0] flags=0x7 queues=2                                     ← GRAPHICS|COMPUTE|TRANSFER ✓
vkCreateDevice: feature mask applied, masked-out bits=0x00000000
  feat[0] want=0x1 sup=0x1 xor=0x0 …                               ← 请求位 == 支持位，零分歧
vkCreateDevice → 成功（MGL 日志中不再出现 vkCreateDevice FATAL）
```

同一时期 MGL 侧的交叉证据（`docs/09` §18.1）：
`Enabled optional device extension: VK_EXT_vertex_attribute_divisor`
（早先走 blob 时它是 `missing`）；转发层确被真正调用的证据是
`/sdcard/MG/vkshim.log` 里的 `vkCreateDevice: ext=11` + 11 个扩展名 + `features: 0x7b885f72b4`，
且这 11 个扩展在 panvk 里**全部可用**。转发层被**静态链进 MGL** 的直接证据是
`readelf --dyn-syms` 显示 UND `vk*` = **0**。

### 4.5 ⑤ 交换链：`VK_ERROR_INVALID_EXTERNAL_HANDLE`（Mesa `u_gralloc` 转换失败）

**现象**：MGL 传的交换链参数**全部合法**，仍被拒（`docs/09` §20.3、§21.1）：

```
CreateSwapchain: surf=0x7b8b3f9690 usage=0x13 fmt=37 cs=0 pm=1 alpha=0x1 layers=1 old=0x0
                 usage=0x13 ⊆ caps 0x17 ✓  fmt=37 ✓  pm=1(MAILBOX) ✓  图像数 3 ∈ [2,4] ✓
→ VK_ERROR_INVALID_EXTERNAL_HANDLE (-1000072003)
```

**源码定位到一行**（`docs/09` §20.3、§21.1，Mesa `src/vulkan/runtime/vk_android.c`）：

```c
struct u_gralloc *u_gralloc = vk_android_get_ugralloc();
if (u_gralloc_get_buffer_basic_info(u_gralloc, in_hnd, &info) != 0)
   return VK_ERROR_INVALID_EXTERNAL_HANDLE;          /* ← 就是这里 */
```

**为什么我们的 panvk 必然失败（源码级判定，`docs/09` §21.2；`research/04`）**：

`u_gralloc.c` 的后端尝试顺序 `CROS → [GRALLOC4 若编入] → LIBDRM → QCOM → FALLBACK`，逐个都不通：

| 后端 | 为什么不通 | 证据 |
|---|---|---|
| GRALLOC4 | **没有编入**（构建时 `dependency('android.hardware.graphics.mapper')` 未找到） | `strings libvulkan_panfrost.so` 里 `android.hardware.graphics.mapper` 计数为 **0** |
| LIBDRM | `u_gralloc_libdrm_create()` 要求 `hw_get_module("gralloc")` 返回的模块名严格等于 **`"gbm"`**，本机 `gralloc.default.so` 不是 gbm 模块 ⇒ `goto fail` | `docs/09` §21.2 |
| QCOM | 高通专用 ⇒ 失败 | 同上 |
| CROS | 要求模块名 `"CrOS Gralloc"` | `research/04` §一 |
| FALLBACK | 靠 `hw_get_module(GRALLOC_HARDWARE_MODULE_ID)` 拿到 `/vendor/lib64/hw/gralloc.default.so`，但 **Android 16 上它只是空壳**，`get_buffer_basic_info` 返回非 0 | `docs/09` §21.2；`research/10` |

⇒ **实际落到 FALLBACK ⇒ 必然报 `VK_ERROR_INVALID_EXTERNAL_HANDLE`**。
这与项目开头的老结论**闭环**：本机 `/dev/dri/*` 全部 `EACCES`，而 Mesa `u_gralloc` 的默认后端正是走 `/dev/dri`。

**这一步的两种修法**（本文 6.1 / 6.3 展开）：
(A) 把 **GRALLOC4(imapper4) 后端编进去**（技术要求已摸清，`docs/09` §21.3；`research/02`、`research/05`）；
或 (C) 在 shim/Mesa 层**绕过** —— 最终我们采用的是它的**诊断版**：

```
pojavEnv += MESA_VK_WSI_HEADLESS_SWAPCHAIN=1
```

其作用（Mesa `wsi_common.c`）是把**任意 surface（含 android）**换成 Mesa 的 headless 交换链，
`queue_present` 是**空操作返回 `VK_SUCCESS`** ⇒ **绕开了"Android WSI 转 DRM 描述"这一环**（`docs/09` §22）。
该开关的发现来自 `research/07`（"一行杀招"），并被 `research/07` 明确标注其定位是
**判定"坏的只有 WSI"**（把"建链失败→崩"降级为"干净跑完→黑屏"）。

### 4.6 ⑤之后：判据行达成，但**下一关是 `VK_ERROR_DEVICE_LOST`**

**判据行（游戏日志原文，`docs/09` §22）**：

```
[10:46:13] [Render thread/INFO]: OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)
```

达成版本：插件 APK **`mgl-panvk-v47`**（versionCode 47，sha256 前缀 **`bb689838`**）
= `panel v46` 的载荷 + `pojavEnv` 增加 `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`；
同一次运行的其它证据（`/sdcard/MG/vkshim.log`）：surface 有效、队列族 `flags=0x7 queues=2`、
两次 `CreateSwapchain`（MAILBOX 与重试 FIFO）（`docs/09` §22）。

**尚存的下一关（如实记录，`docs/09` §22）**：MGL 随后在**纹理上传**阶段报 `-4`：

```
[10:46:15] FATAL: Vulkan error VK_ERROR_DEVICE_LOST (-4) at VkTextureManager.cpp
[10:46:15] FATAL: vkQueueSubmit(texture upload batch)
[10:46:15] ERROR: WaitForSubmitIndex: vkWaitForFences returned -4
```

⇒ **判据行已拿到（渲染器链路成立）✓，但这一版还不能稳定游玩**。

### 4.7 ⑥ 第六次迁移：从「WSI」转到「绘制执行」（`-4` = CSF exception `0xc3`）

**⑤ 的修复（headless WSI）让判据行出现，但并没有解决 `-4`。** 转折由两件事构成：

**(a) v48/v49：把 `-4` 钉在纹理上传的 `vkQueueSubmit`，并排除 dma-heap**（`docs/09` §23）

- v48（`4.8-diag-deep`，sha256 `e511f980…`）把诊断面铺满（`MESA_DEBUG=1` / `PANVK_DEBUG=1` /
  `LIBGL_DEBUG=1` / `EGL_LOG_LEVEL=debug` + `MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log`）⇒
  `-4` 的**首次出现**被钉在 MGL 的**纹理上传批次**：
  `FATAL: Vulkan error VK_ERROR_DEVICE_LOST (-4) at VkTextureManager.cpp` /
  `vkQueueSubmit(texture upload batch)` / `vkWaitForFences returned -4`。
- v49（`4.9-nodmaheap`，sha256 `f1389427…`）追加 `PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent`
  （关掉 dma-heap ⇒ 退回 kbase 原生分配）做**反向对照** ⇒ **错误码不变**
  ⇒ **dma-heap / dma-buf 不是该 `-4` 的成因**。
- 同轮复核源码：构建树 `kbase_kmod.c` 用 **`O_RDONLY`** 打开 `/dev/dma_heap/system`（:1285），
  却用该 fd 做 `DMA_HEAP_IOCTL_ALLOC`（:1569）；本机 dma_heap 节点全 **0444**、`/dev/mali0` 为 **0666**。
  "ALLOC 必然失败"目前是**源码推断**（`docs/09` §23.3 如实标为**未验证**）。

**(b) 独立探针 8 模式：原假设被推翻，`-4` 的真身是 CSF `0xc3`**（`docs/09` §25、`research/12`）

探针（`panvk_wsi_probe`，md5 `f735e1f4…`，加载的是**补丁前**出厂件 `4417b369…`）在
OPPO PHZ110 / MT6989 / Android 16 / **无 root** 上**一模式一进程**运行，三条决定性发现：

| 发现 | 证据 |
|---|---|
| **驱动渲染被独立证明** | `render`（无 surface / 无 Activity / 无 root）`vkQueueSubmit → 0`、`vkWaitForFences → 0`，clear 色 `64/128/191/255` 在 (0,0)/(32,32)/(63,63) **三点精确**、`failures=0`；`ahb(0x1)` 同样全绿；`win(0x1)` 进一步证明 **present 的像素真落到了窗口 buffer**（`WINDOW PIXEL PASS`） |
| **原假设不能复现** | `win` / `headless` / `winimpdef` 三条 surface 路径下 **`vkCreateSwapchainKHR` 全部 `VK_SUCCESS`**；唯一可复现的 `-1000072003` 只在 AHB 用 `IMPLEMENTATION_DEFINED(0x22)` 分配时出现（MESA `Failed to get u_gralloc_buffer_basic_info`） |
| ★ **掉线真身 = CSF fatal** | `tri` 模式整条管线全 0、`vkQueueSubmit(draw) → 0`，但 `vkWaitForFences → -4`；kbase 报 `CSF group 0/1/2 fatal error: status 0x7dc002c3 (exception 0xc3)`。**不含 draw 的 clear+copy 全正常** |

⇒ **分界线很清楚**：**命令提交、内存与导入链路是通的；坏在图形管线实际的光栅化执行。**
这同时解释了两件事：(i) 为什么"修 WSI"走到 headless 只能是"干净跑完但黑屏"——
它绕开的从来不是真正的瓶颈；(ii) 为什么 v49 关掉 dma-heap 无效。

> ⚠️ **探针操作告诫**：`--mode=all` **不可用** —— `tri` 的 CSF 掉线会**污染同进程后续所有步骤**
> （`tri` 之后连 `render` 的 `vkQueueSubmit` 都变成 `-4`）⇒ **必须一模式一进程**。

**第 ⑥ 次迁移的意义**：前五次迁移都在"让链路通"，第六次是第一次把矛头指向**驱动/固件自身的行为** ——
`vkQueueSubmit` 返回 `VK_SUCCESS` 却等来 `-4`，这是 GPU 固件（CSF）侧的致命异常，
**不可能靠 WSI / gralloc 侧的补丁解决**。取证方向见 §7.2 的 P0。

### 4.8 与六次迁移**并列**的四个"封路"实验（同为本文的净产出）

这四条不是"迁移"，而是**排除法得到的边界**，价值不低于上面的修复：

| # | 假设 | 实测结果 | 证据 |
|---|---|---|---|
| P1 | 用 `VK_ICD_FILENAMES` 让系统 loader 加载我们的 ICD | **loader 忽略它**，仍选 blob | `docs/09` §9①、§11 |
| P2 | 用我们的 `libvulkan.so.1` 顶替 loader | **JVM 卡死**在 `[DEBUG] Calling JLI_Launch`（6 分钟无输出） | `docs/09` §9② |
| P3 | 用**完整 125 入口垫片**改名为 `libvulkan.so` / `.so.1` | `.so` 单独放：游戏正常但**走 blob**（名字没被命中）；`.so` + `.so.1` 双名：**同样卡死** ⇒ **与垫片完整度无关** | `docs/09` §13（v18/v19） |
| P4 | `patchelf --replace-needed` 换 `DT_NEEDED` 到唯一名垫片 | `library "libvkpanvk_shim.so" not found: needed by … in namespace clns-9` ⇒ **裸名不可解析** | `docs/09` §14、§17(a) |

**P2/P3 的合并结论**（`docs/09` §13）：只要插件目录里的 `libvulkan.so.1` 顶替了系统 loader，
ZL2 的 JVM 就卡死在启动路径上 —— 说明 **ZL2/LWJGL 在 JVM 启动早期就会解析/使用 Vulkan**，
而"垫片 → PanVK"链在那个时机**不能完成**（最可能是 PanVK 在 app 域做 kbase 初始化的**时机问题**）。

**P4 的正解**（`docs/09` §15、§17(b)(c)）：
① `pojavEnv: DLOPEN=<lib名>` 预加载（**只有 pojavEnv 被解析**）；
② 垫片**必须带 `DT_SONAME=<同名>`**（`-Wl,-soname,libvkpanvk_shim.so`），否则即便 DLOPEN 成功，
`DT_NEEDED` 仍报 `not found`（这正是某一版 65080 B / sha `91c4053e` 失败的原因）；
③ **不要**删 `DT_NEEDED` 只靠 `RTLD_GLOBAL` 全局组，会 `cannot locate symbol`（设备原生探针实测）。

**为什么 `RTLD_GLOBAL` 预加载就是"劫持点"**（`docs/09` §16）：
ZL2 在 `dlopenEngine()` 里先 `ZLBridge.dlopen("<nativeLibraryDir>/libvkpanvk_shim.so")`（**RTLD_GLOBAL**）
再 dlopen 渲染器；垫片进入**全局符号组**后，它导出的 **`vkGetInstanceProcAddr`** 会在全局查询里**抢先命中**，
于是 MGL 运行期取到的入口**全部来自垫片** → 垫片转发给我们的 ICD。
⇒ 这与 §15 的源码分析**完全一致**：`DLOPEN=libvkpanvk_shim.so` 是**正确且必要**的一步。

**另有一次"怀疑被推翻"的过程**（`docs/09` §14 → §15）：v22/v23 把 `DLOPEN` 写进清单后
LWJGL 出现 `liblwjgl.so: unknown type` 崩溃，一度怀疑是 `DLOPEN` 造成的；
读 ZL2 源码定案后确认 **`DLOPEN` 机制正确、无害**（它不修改任何搜索路径），
真正的可疑点是**被 `patchelf` 改过 `DT_NEEDED` 的 `libMobileGL.so` 被提前加载**
（此时垫片尚未进入命名空间 ⇒ 加载失败并留下破状态）—— 该假设在 `docs/09` §15 中
明确标注为"**下一步应查/验证方法**"，并**未被当轮证实**。

---

## 5 证据

本节把全文关键结论**逐条挂到证据位置**，便于复核。

### 5.1 来自 `docs/09` 各节

| 结论 | `docs/09` 位置 |
|---|---|
| 判据行达成（`Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99`） | §22 |
| `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1` 的作用与来源 | §22 |
| 下一关 `VK_ERROR_DEVICE_LOST (-4)` @ `VkTextureManager.cpp` | §22 |
| v48：`-4` 的**首次出现**钉在纹理上传的 `vkQueueSubmit`（错误码/位置与 v47 相同） | §23.1 |
| v49：关掉 dma-heap（`PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent`）做反向对照 ⇒ **无效** | §23.2 |
| dma_heap 源码事实：`O_RDONLY` 打开的 fd 被用于 `DMA_HEAP_IOCTL_ALLOC`；本机 dma_heap 全 0444、`/dev/mali0` 0666 | §23.3 |
| ★ **更正**：`O_RDWR` 那句来自**旧树** `/root/mesa`（md5 `0f4d40ca…`）；**构建树**（md5 `e2e92db6…`）是 `O_RDONLY` ⇒ `kbase_kmod_supports_dmabuf()` 实为 **true** | §23.4 |
| v50：05 方案 A + MR !43659 式 LINEAR 推断（3 个文件**严格加性**）+ 产物哈希（`e08e0764…`/20,005,320） | §24.1、§24.2 |
| v50 与 v49 的**唯一自变量差异** = 删掉 `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1` | §24.3 |
| ★ **构建目录实测判定（闭合 U4）**：4 个 build 目录只有 `android-v4` 有产物；`wsi_x11` 计数 0、`platforms=['android']` ⇒ 06 号的"含 x11 WSI"不成立 | §24.4 |
| ★★ **真机探针 8 模式**：驱动渲染被独立证明；三路 `vkCreateSwapchainKHR` 全 `VK_SUCCESS`（原假设不能复现）；`-4` = CSF exception `0xc3` | §25 |
| ★ **v50 上机：首次真交换链 + 真画面**（`Swapchain created, extent = 2376x1080, swapchain imageCount = 3`；主界面含 3D 全景干净渲染约 10 秒，之后黑屏崩溃 `present`/`acquire` → `-4`） | §28.4 |
| v51 空载荷 APK（`unzip` 匹配失败）⇒ **废弃**；教训：打包后必须数载荷条目 + `unzip -p … \| sha256sum` 比对 | §26.1 |
| v52 = v50 载荷 + 全套调试 env；**logcat 环形缓冲会冲掉证据** ⇒ 必须后台落盘 `logcat -b all -v time > /tmp/cap.txt`；`pkill -f` 会自杀 ⇒ 用 `pkill -x logcat` | §26.2–§26.4 |
| ★ **v52 抓到决定性一行** `E/MESA: kbase: CSF group 0 tiler heap OOM notification`（判据行 → +5~10 s 该行 → +9 s `-4`） | §28.5 |
| ★ **v53（P1）补 uAPI 1.18 档 + `csi_handlers` ⇒ 真机无效 ✗**（仍 OOM、无 `TILER_OOM CSI handler (1.18 layout, ioctl 58)`）⇒ 推断版本门挡住 1.18 分支；**下一步 P2** | §27.3、§28.6 |
| **6 次**错误点迁移总表 | §20.5（§18.2 为前四步早期形态）、§23–§25（第 ⑥ 次） |
| 队列族 `flags=0x7` / feature `xor=0` / `vkCreateDevice` 成功 | §20.2 |
| `-3` 的 Mesa 源码根因（`panvk_vX_device.c:376`） | §19 |
| "生成器把物理设备当 instance"的 4 处修正 | §20.1 |
| A/B 二分日志 `QFam-diag: fn=0x0 …` | §20.1 |
| `VK_ERROR_INVALID_EXTERNAL_HANDLE` 的参数合法性 + 源码行 | §20.3、§21.1 |
| u_gralloc 后端逐个不通（含 `strings` 计数 0） | §21.2 |
| 路线 B（imapper4）的准确技术要求与 meson 探测方式 | §21.3 |
| 设备侧已具备的运行时库清单（`ro.hardware = mt6989`） | §21.3 |
| `/storage` noexec + 正确分工表 | §8 |
| loader 忽略 `VK_ICD_FILENAMES`（注入成功但对不上） | §9① |
| `libvulkan.so.1` 顶替 → JVM 卡死 | §9② |
| 125 UND 符号 vs 纯 ICD 导出（`readelf` 原文） | §10 |
| loader 忽略 `VK_ICD_FILENAMES` 的**版本号铁证** | §11 |
| 自建垫片的生成步骤与两个正则/分类坑 | §12 |
| 完整垫片 v18 走 blob、v19 卡死 | §13 |
| `clns-9` 命名空间 `ld_library_paths=""` 与 `permitted_paths` | §17(a) |
| `DLOPEN` 只解析 `pojavEnv`；垫片必须带 `DT_SONAME` | §17(b)(c) |
| 双栈（MGL 直连我们的 ICD + ZL2 又 dlopen 系统 loader）导致 `wsi_GetSwapchainImagesKHR` 解引用外来句柄 | §17 |
| `gipa_pd` 物理设备优先 + 123 thunk 表 + feature 屏蔽 + 日志双写 | §19 |
| `-4` 之前的正向证据：`Enabled optional device extension: VK_EXT_vertex_attribute_divisor`；UND `vk*`=0；`vkCreateDevice: ext=11` | §18.1 |
| 渲染器插件契约源码（`RendererPluginManager.kt` / `RendererPlugin.kt` / `GameLauncher.kt` 原文） | §3、§15 |
| 静默安装绕过（复用已装包名 ⇒ 更新） | §4 |
| 改别人签名的插件装不上 | §7 |
| MobileGL 走 blob 时的 59–60 FPS 与两条自动化点按通道 | §7 |
| 四个编译坑与 16.9 MB 产物 | §2 |
| 驱动 WSI 段错（`wsi_GetSwapchainImagesKHR+0x20`） | §16 |
| 探针实测：从插件 lib 目录 dlopen 成功、`Mali-G720 MC12`、API 1.4.363、扩展 181 | §8 |

### 5.2 来自 `research/01–16`

| 编号 | 本文引用的结论 |
|---|---|
| `research/01` | mapper **没有** AIDL 接口（v5 是 native stable-C `AIMapper`）；`u_gralloc_imapper5_api.cpp` 已在不建 AOSP 下编译链接成功（49,016 B，导出 `u_gralloc_imapper_api_create`）；libc++ `std::__1` 硬阻塞的绕过法 |
| `research/02` | `prebuilts/vndk` v29–v34 预生成 HIDL 头（**v35/v36 不存在**）；`hidl-gen` 预编译二进制可用；`std::__1` vs `std::__ndk1` 的确定性修法；imapper4 路线 0.5–1.5 人天 |
| `research/03` | `Gralloc4.h` 被 Mesa 实际使用的只有 4–5 个 decode 函数；字节流唯一来源是 IMapper@4.0 HIDL `get()`；**全 Android 公开 API 都没有返回 DRM modifier 的函数** |
| `research/04` | `u_gralloc` ops 契约（`offsets[0]` 必须为 0、`modifier=INVALID` 对 panvk 非法）；本机 5 个既有后端全部不可用；`-Dandroid-stub=true` 使 imapper4/5 **没参与编译**；`hardware_stub.cpp` 的 `hw_get_module` 是桩 |
| `research/05` | 方案 A：只改 `vk_android.c` 约 90–105 行、严格加性、`PANVK_GRALLOC_NO_FALLBACK=1` 开关、重编 30–60 秒、成功率估计 70–80%；**回滚陷阱**：`work/mesa` 有 45 个未提交改动，禁止 `git checkout --` |
| `research/06` | `/dev/dma_heap/system` **0444** ⇒ `kbase_kmod_supports_dmabuf()=false` ⇒ `sw_device=true`；panvk 只认 AFBC/U-interleaved/interleaved_64k/AFRC/LINEAR；`vk_gralloc_to_drm_explicit_layout()` **无 LINEAR 回退** ⇒ 未知 modifier 是 **SIGSEGV 而非 VkResult**；`/dev/dri` 有 card0 但**无 renderD\***；R4 杠杆：ARM gralloc 尊重 `no_afbc_usage` 位 |
| `research/07` | 默认 FBO **物理上就是交换链图像**，不存在"默认FBO→交换链 blit"；唯一 present 入口 `eglSwapBuffers → BackendObject.cpp:396 → vkQueuePresentKHR`；全树**无** HEADLESS/NO_WSI 开关；★ `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`；`MOBILEGL_BACKEND_TYPE` 默认不是 DirectVulkan |
| `research/08` | 用哪个桥**完全由 `POJAV_RENDERER` 字符串决定**；`custom_gallium`/`gallium_panfrost`/`vulkan_zink` ⇒ OSMesa 桥（无 EGL、无 VkSurface）⇒ 一次都不调 `vkCreateSwapchainKHR`；`boatEnv` 在 ZL2 全树 0 命中；`renderer[0]` 不在六字符串内 ⇒ `br_init` 为 NULL ⇒ 启动即崩 |
| `research/09` | `VK_ERROR_INVALID_EXTERNAL_HANDLE` / `u_gralloc` 的公开案例与补丁（作为本仓库补丁方向的旁证） |
| `research/10` | 7 模式探针 `probe10/`（`panvk_wsi_probe.c`；**已上机，8 模式结果见 `research/12`**）；卡点**精确化**为 `vkCreateSwapchainKHR` 第 4 步 `vkCreateSwapchainKHR → panvk_android.c:405/293/226 → vk_android_get_ahb_layout() → vk_gralloc_to_drm_explicit_layout() → u_gralloc_get_buffer_basic_info()`；`R8G8B8A8` 的 `GetAndroidHardwareBufferPropertiesANDROID` **不碰** u_gralloc；判据 logcat tag = `MESA` |
| `research/11` | **v50 WSI 补丁实施**：3 个文件（`u_gralloc_fallback.c` 的 `-EINVAL→-EAGAIN` + `panvk_infer_linear_modifier()`；`vk_android.c` 的 AHB 自描述回退约 150 行；`nativewindow_stub.cpp` 的 `lockPlanes` 桩）**严格加性**；①a 对 WSI 自身 AHB **不生效**（`format=1` 不是 YUV）⇒ 真正放行交换链的是 ①b；新 `.so` md5 `e08e07645c16d8ebaa11ca70a09884fd`/20,005,320 B/sha256 `a0b2451e…`；★ **构建目录判定**：只有 `build/android-v4` 有产物、`wsi_x11` 计数 0、`platforms=['android']` ⇒ **05 对、06 的"含 x11 WSI"不成立**；⚠️ 补丁**从未在真机运行**、`-4` **不能声称已消除** |
| `research/12` | ★★ **真机探针 8 模式结果**（探针 md5 `f735e1f4…`，加载补丁前出厂件 `4417b369…`）：① `render` 无 surface/无 root，`failures=0`、像素 `64/128/191/255` **三点精确** ⇒ **驱动渲染被独立证明**；② `ahb(0x1)` 全绿（MESA `init how=SPHAL rc=0 version=5`、metadata `*_rc=0 fourcc=0x34324241 alloc=16384 layers=1`）；③ `win(0x1)` 真交换链 + present + **`WINDOW PIXEL PASS`**；④ **原假设不能复现**：`win`/`headless`/`winimpdef` 三路 `vkCreateSwapchainKHR` 全 `VK_SUCCESS`，唯一可复现的 `-1000072003` 只在 AHB `IMPLEMENTATION_DEFINED(0x22)`；⑤ ★ **真正掉线的是绘制**：`tri` 的 `vkQueueSubmit→0` 但 `vkWaitForFences=-4`，kbase 报 **CSF group 0/1/2 fatal `0x7dc002c3` (exception `0xc3`)**；不含 draw 的 clear+copy 全正常 ⇒ 首要目标转为**查 CSF `0xc3`**；⚠️ `--mode=all` 不可用（`tri` 污染同进程） |
| `research/13` | ★ **CSF `0x7dc002c3`/`0xc3` 定位**：`0x7dc002c3` 是 GPU MMU 的 `AS_FAULTSTATUS` 原值 —— `EXCEPTION_TYPE=0xC3`（**TRANSLATION_FAULT_3**）、`ACCESS_TYPE=READ`、`SOURCE_ID=0x7DC0` = **CSF 固件自己的 LSU**；`sideband 0x0000005fffe1e000` 就是故障 GPU VA；「三 group 同时 fatal」是同一份 payload 被复制给该 kctx 所有在位 CSG（**只有一次**错误访问）；触发面 = 只有真 `vkCmdDraw` 才碰 **tiler heap**（clear/copy 走 `vk_meta` 全屏 fragment，不消费 chunk）；给出 S1/S2/S3 三步最小验证（⚠️ 全部只读，未改只读树） |
| `research/14` | ★ **tiler heap OOM 定案**：OOM 通知是「验尸报告」（kbase 送通知**之前**已 `term_queue_group()`）；★ 真相 = **堆被顶到天花板且无人重置** —— `initial_chunks=10`/`max_chunks=400`/`chunk_size=1 MiB`，而 Mesa 唯一重置手段 `kbase_renew_tiler_heap()` **是死代码**（`submit->tiler_work_estimate` **全树无写入点**）⇒ 堆单调涨 → `-ENOMEM` → 杀组；★ **10 秒黑洞对上了**：OOM `11:25:37.423` → DEVICE_LOST `11:25:47.413` = **9.99 s** = `KBASE_WAIT_TIMEOUT_NS`；dma_heap 的 `O_RDONLY` **不是**本案凶手；两处最小修法 **(A)** 1.18 布局 + `csi_handlers`、**(B)** 接上 renew（最小 2 行） |
| `research/15` | **与已跑通先例的彻底 diff**：`/root/panvk-mtk` 只是**补丁仓库**（单 commit，`patches/panvk_mtk.patch` + 构建脚本），源码真身 = `/root/mesa`（`funnymdzz/mesa@6598829`，未打补丁原始态）；两树**同源同作者血脉**（注释逐字相同）⇒ **先例的成功不能归因于任何一处 tiler 参数/workaround**；**6 条**可移植差异，**H1（`csi_handlers` 从未送达内核）最高**、**H2（`tiler_work_estimate` 生产者被整段删掉 ⇒ renew 永不执行）已定论** |
| `research/16` | ★ **P1 落地（v53）**：版本阶梯 `1.25/1.6` → **`1.25/1.18/1.6`**（新增 1.18：ioctl **`0xc028803a`**、40 B、`csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG`；uAPI 判断保留、`<1.18` 不变）；新 `.so` size **20,005,600** / md5 **`7f3a0e8f…b404`** / sha256 **`58ef996f…bec4`**；**反汇编确认新分支进二进制**；v53.apk sha256 **`9c99af82…d48e`**；★ **真机无效 ✗**（仍 OOM、无 `TILER_OOM CSI handler (1.18 layout, ioctl 58)`）；P2 未实施（v52 就是现成对照） |

### 5.3 来自中文结论摘要

| 摘要 | 本文引用的结论 |
|---|---|
| `summaries/01-结论摘要-突破.md` | 前提纠正 + imapper5 已编出的突破 + libc++ 双 ABI 绕过 + 设备侧 mapper 实况（`mapper.mediatek.so` stable-C 5.0、`libui.so` 导出全部所需符号） |
| `summaries/02-结论摘要-可行且便宜.md` | imapper4 工作量与 62 个 UND 中仅 9 个来自 libc++ 且全为无命名空间 ABI 符号 |
| `summaries/03-结论摘要.md` | "缺口核心就是 modifier"；纯 NDK 连 dma-buf fd 都拿不到 |
| `summaries/04-结论摘要与行动计划.md` | 5 个后端不可用表；ops 契约；直连 mapper AIDL（`AServiceManager_openDeclaredPassthroughHal("mapper", "<vendor>")` → `AIMapper_loadIMapper`）**全部依赖运行时 dlopen，不需要 hidl-gen / AIDL 编译器** |
| `summaries/05-07-08-关键结论.md` | 三条路线对比表（见本文 6） |
| `summaries/06-结论摘要与决定性事实.md` | 0444 根因链；路线 R1–R5 排名；**05 与 06 的一处未解矛盾**（见 5.4） |
| `summaries/10-结论摘要-可上机探针.md` | 探针 7 模式表与独立判据；`/dev/mali0` 为 `crw-rw-rw- system:graphics` |

### 5.4 本仓库**如实标注**的未解项与矛盾

**本轮已闭合的两条**：

1. ~~**05 与 06 的构建目录矛盾**~~ ⇒ ✅ **已闭合**（`docs/09` §24.4、`research/11` §1）：
   实测 4 个 build 目录中**只有 `/root/zenithblue/build/android-v4` 有产物**；
   `strings <so> | grep -c wsi_x11` = **0**、`build.ninja` 中 `wsi_common_x11|wsi_x11` = **0** 次、
   meson `platforms=['android']`、`vulkan-drivers=['panfrost']`、`gallium-drivers=[]`
   ⇒ **`research/05` 正确、`research/06` 的"出厂 `.so` 含 x11 WSI（`platforms=[android,x11]`）"不成立**；
   06 的误判来源已定位：编译命令里的 `-I/root/zenithblue/work/android-deps-x11/include` 只是**依赖 include 目录名**。
   （与"实机确实成功创建过 swapchain"不再矛盾 —— 纯 android 构建**有** swapchain，只是 WSI 后端不同。）
2. ~~**`research/10` 的探针未在真机运行**~~ ⇒ ✅ **已上机**（8 模式，`docs/09` §25、`research/12`）；
   ~~**`research/05` 的方案 A 未上机**~~ ⇒ ✅ **已实施并编入 v50**（`docs/09` §24、`research/11`）——
   但 **v50 本身仍未上机**（见下第 6 条）。

**仍然未解/未验证（如实记录）**：

3. ~~**v50 从未在真机运行**~~ ⇒ ✅ **已上机**（`docs/09` §28.4）：真交换链建成、主界面干净渲染约 10 秒；⚠️ `-4` 仍在。补丁本身仍只有静态/链接层验证
   （两次增量编译 `exit=0`、`SONAME`/`NEEDED` 与旧件逐条一致、APK 载荷 sha256 逐位相同），
   **没有运行时证据**；且 `-4` **不能声称已被它消除**（该链不经过本补丁）。
4. **`O_RDONLY` 下 `DMA_HEAP_IOCTL_ALLOC` 是否成功 / `kbase_kmod_supports_dmabuf()` 的实际返回值**
   （`docs/09` §23.3/§23.4）：本轮只给出**源码推断**（"必然失败"），未在设备侧取证；
   `research/06` 的注释给出的是**相反**推断（"ALLOC ioctl 不要求写权限"）。**至少一条是错的。**
5. **`-4`（= CSF `0xc3`）尚未修**，只是被定位到"绘制执行"（`docs/09` §25.7）——这是**当前首要目标**。
6. **唯一可复现的 `-1000072003` 只在 AHB `IMPLEMENTATION_DEFINED(0x22)` 上**（`docs/09` §25.5），
   而真实 App 的 Surface 用的正是该格式 ⇒ 与第 5 条是**两条独立线**。
7. **`driverVersion` 两处记录不一致**（见 3.5 注）。
8. **机型写作不一致**（PHX110 / PHZ110，见 2.1 注）。
9. **`research/09` 没有中文摘要**（见 `research/README.md` §3）。
10. **`docs/09` §15 的"被 patch 的 MGL 被提前加载"是假设**，当轮**未被证实**。

11. ★ **实验 P1（v53）已上机验证无效 ✗**（`docs/09` §27.3/§28.6、`research/16`）：
    补了 uAPI 1.18 档并置 `csi_handlers` 后，**仍**出现 `kbase: CSF group 0 tiler heap OOM notification`，
    且**没有**出现期望的 `TILER_OOM CSI handler (1.18 layout, ioctl 58)` ⇒ 推断 **1.18 分支被版本门挡住**。
    按 `research/14` 的定案，OOM 的直接成因是 **「堆只涨不落」** ⇒ **下一步 = P2**（§7.2 的 P7）。

> ⚠️ **一处必须同时读的更正**（`docs/09` §23.4、`CHANGELOG.md` M12.4）：本文 2.3 节/§6 沿用的
> "`/dev/dma_heap/system` 0444 ⇒ `O_RDWR` 打开失败 ⇒ `supports_dmabuf()=false` ⇒ `sw_device=true`"
> 读的是**旧树** `/root/mesa`（md5 `0f4d40ca…`，`O_RDWR`）；**构建树**（md5 `e2e92db6…`，出厂件来源）
> 用的是 **`O_RDONLY`**，对 0444 节点**能打开成功** ⇒ `supports_dmabuf()` 实为 **true**。
> 该条的因果链**对出厂件不成立**（详见 `docs/09` §23.4）。

---

## 6 讨论

### 6.1 三条路线的对比

`research/05`、`research/07`、`research/08` 三篇合起来给出了**剩余三条路线**的选择依据
（`summaries/05-07-08-关键结论.md` 的原文表，本文引述并补充实测状态）：

| 路线 | 内容 | 代价 | 原著结论 | 本项目实测状态 |
|---|---|---|---|---|
| **A（原著推荐）** | **修 WSI**：05 号方案 A —— 改 `vk_android.c` 约 90 行（AHB 自描述回退） | **低**（60 秒重编） | **保留 GPU 加速**，符合"效率高"的目标 ✓✓；成功率估计 70–80% | **方案已成文，未上机**（`research/05`） |
| **B** | **切 gallium 桥**：`custom_gallium` / `gallium_panfrost` ⇒ OSMesa 桥**绕开交换链** | 需编 zink/OSMesa + **每帧 CPU 合成** | 与"效率高"**矛盾** ✗ | 机理已定论（`research/08`）；**未实测** |
| **C** | **shim 层绕过**：在 shim 层重定向 surface / 自行实现 `vkCreateSwapchainKHR`/`vkGetSwapchainImagesKHR` | 中高 | 备选（`research/07` 的 ④）；`vkshim_mgl.c` 里 **WSI 四个入口都已挂钩**，是天然落点 | 我们实际用的是 C 的**轻量变体**：`MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`（**env 开关，非自写代码**）⇒ **判据行达成** |

### 6.2 关于"我们实际走的是哪条"

必须说清楚：**判据行的达成不等于路线 A 的成功**。

- 我们实际生效的是 **C 的轻量变体**（`MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`），
  它在 Mesa 侧把任意 surface 换成 headless 交换链，`queue_present` **空操作返回 `VK_SUCCESS`**
  （`docs/09` §22）。`research/07` 对该开关的定位本来就写得清楚：**诊断用**，
  把"WSI 建链失败→崩"降级为"干净跑完→黑屏"，用于**判定坏的只有 WSI**。
- 也就是说：**渲染器链路（instance/device/队列族/feature/交换链参数）已经被证明是全对的**，
  这正是判据行出现的意义；但**"出画面"仍依赖路线 A 或 B**。
- 剩余失败点 `VK_ERROR_DEVICE_LOST (-4)` 出现在**纹理上传**阶段（`docs/09` §22），
  说明接下来的瓶颈已经**不在 WSI 建链**，而在**提交/内存/格式**一侧。

### 6.3 为什么路线 A 仍是首选，以及它的两个前置

**路线 A 的逻辑**：问题被 `research/10` 精确化到**一个函数** ——
`vkCreateSwapchainKHR` 第 4 步内部 `u_gralloc_get_buffer_basic_info()` 失败（`research/10`）。
而 `research/04` 已经把 `u_gralloc` 的 ops 契约与 6 个后端逐个解剖清楚，
`research/05` 给出的补丁是**严格加性**的（只在既有调用已失败时生效，正常设备零影响），
且**不需要 meson 改动**（`AHardwareBuffer_describe()` 已经是 `.so` 的 UND 符号）。

**但 A 有两个必须处理的前置**（`research/05` 明列）：

1. 新 u_gralloc 后端必须插在 **FALLBACK 之前**，否则是死代码；
2. 构建带 `-Wl,--no-undefined` ⇒ 若用 `lockPlanes`，必须在 `src/android_stub/nativewindow_stub.cpp` 补桩；
3. **回滚陷阱**：`work/mesa` 有 **45 个未提交改动**，`vk_android.c` 在其中 ⇒ **禁止 `git checkout --`**，只能 cp 备份/覆盖。

### 6.4 与"正统解法"的关系：imapper4 / imapper5

真正"干净"的解法是让 panvk 使用 Mesa 的 **Android gralloc 后端**（`u_gralloc_imapper4/5`：经
`libgrallocmapper` / mapper HAL，不依赖 `/dev/dri`）。本仓库在这条线上有**两份实打实的进展**：

- `research/02`（imapper4 / **HIDL**）：可行且便宜（0.5–1.5 人天），
  VNDK v34 恰好等于本机 `ro.vndk.version=34`，头文件与 `hidl-gen` 都有现成来源；
- `research/01`（imapper5 / **native stable-C**）：**前提被纠正**（mapper 从来没有 AIDL 接口），
  但 `u_gralloc_imapper5_api.cpp` **已在不建 AOSP 的前提下编译+链接成功**
  （复用 VNDK 树，导出 `u_gralloc_imapper_api_create`）。

**而"纯 NDK 自算"这条路被 `research/03` 判死**：整个 Android 公开 API
（NDK 与 VNDK）都没有返回 **DRM modifier** 的函数，而 panvk 恰恰**强制要求** modifier
（`research/06`：`vk_gralloc_to_drm_explicit_layout()` 直接照抄 `info.modifier`，**无 LINEAR/未知回退**
⇒ 未知 modifier 会走到 `pan_mod_get_handler()` 返回 NULL ⇒ **SIGSEGV，不是 VkResult**）。
⇒ 所以"绕开 gralloc"只能得到**线性、无压缩、平面可枚举**这一受限子集，等价于 Mesa 现成的 FALLBACK 后端。

### 6.5 一条更省事的杠杆（`research/06` R4）

`research/06` 指出本机是 **ARM gralloc**（`ro.vendor.arm.gralloc.*` 全套 + `libarm_gralloc_properties_sysprop.so`），
它**尊重 `no_afbc_usage` 位**（`0x200000000000000`）⇒ 让上层申请 buffer 时带该 usage，
**很可能直接拿到 LINEAR** ⇒ panvk **不改也能用**。
这条杠杆的代价最低，但**依赖"上层愿意带这个位"**，属于**未验证**（`research/06` §三 亦标注）。

### 6.6 ★ 第六次迁移后的再判断：**WSI 不是最终瓶颈，CSF 绘制执行才是**

§4.7 的探针发现**改变了本文对"瓶颈在哪"的判断**，也**修正了 §6.1–6.3 的优先级**：

| 本文此前的判断 | 探针之后 |
|---|---|
| "剩余失败点是 `VK_ERROR_DEVICE_LOST (-4)`，出现在纹理上传阶段，说明瓶颈已经**不在 WSI 建链**，而在**提交/内存/格式**一侧"（§6.2 末） | 更精确：`-4` = **CSF exception `0xc3`**，出现在**执行 draw** 时；**`vkQueueSubmit` 本身返回 `VK_SUCCESS`**（§4.7） |
| "路线 A（修 WSI）仍是首选，成功率 70–80%"（§6.3） | **前提被削弱**：`win`/`headless`/`winimpdef` 三路的 `vkCreateSwapchainKHR` **本来就全成功** ⇒ 交换链创建**不是**无条件坏的。路线 A 的价值从"修好一个必然坏的东西"变成"让 AHB 自描述在 `IMPLEMENTATION_DEFINED(0x22)` 等**边界格式**上更稳"（仍然有用，但**不再是首要**） |
| "`MESA_VK_WSI_HEADLESS_SWAPCHAIN=1` 把'建链失败→崩'降级为'干净跑完→黑屏'，用于**判定坏的只有 WSI**"（`research/07`） | **该判定的结论要反过来读**：既然 clear+copy 路径完全正常、只有 draw 掉线，那么"干净跑完"**不能**证明"坏的只有 WSI"——它只说明**坏的与 WSI 无关** |

⇒ 本文 §6.1 的三路线对比（A/B/C）**在"能不能出画面"这个目标上仍是有效的工程选项**，
但**没有哪一条能解决 `-4`**：`-4` 是 GPU 固件（CSF）侧的致命异常，
必须从**驱动/固件与绘制的交互**（命令流、描述符、shader、几何提交）去查。
路线 A/C 的成果（v50 的"真交换链"补丁）**是必要的**（出画面的前提），但**不充分**。

**由此得到一条方法论**：判断"瓶颈在哪"**不能**只靠"把某一环绕开之后程序是否还能跑完"——
因为**绕开 WSI 之后剩下的路径里根本没有 draw**（headless + clear+copy）。
真正把瓶颈暴露出来的是**独立探针**：它把 pipeline **一环一环单独执行**，
于是"**提交成功但等不到完成**"这条**只有 draw 才会触发**的分界线才显示出来（§4.7）。

### 6.7 ★ P1（v53）实验之后：`-4` 这条线仍未修，且下一步已明确

§6.6 的判断（「WSI 不是最终瓶颈，CSF 绘制执行才是」）在真机侧**进一步收窄**：
真实 App 现场抓到的是 **tiler heap OOM** 这条线（`docs/09` §28.5）——
`E/MESA: kbase: CSF group 0 tiler heap OOM notification`，判据行 → **+5~10 s** 该行 → **+9 s** `-4`。
针对它的 **P1（v53：CSF group create 走 uAPI 1.18 布局并置 `csi_handlers`）已上机验证无效 ✗**
（`docs/09` §27.3/§28.6、[`16-p1-p2-implementation.md`](16-p1-p2-implementation.md)）；
按 [`14-tiler-heap-oom.md`](14-tiler-heap-oom.md) 的定案，OOM 的直接成因是 **「tiler heap 只涨不落」**
（`kbase_renew_tiler_heap()` 因 `submit->tiler_work_estimate` 全树无生产者而**从未执行**），
⇒ **下一步明确为 P2**（去掉该前置条件的最小 2 行补丁，见 §7.2 的 **P7**）。

> 口径提醒：`research/13` 的 **CSF exception `0xc3`**（探针 `tri`，绘制路径的 MMU TRANSLATION_FAULT_3）
> 与本文这条 **tiler heap OOM** 是**两条独立病灶**，**不要合并看**。

---

## 7 结论与后续工作

### 7.1 结论

1. **免 root 的消费级 Android 上，"自编开源 Mali 驱动（Mesa PanVK / kbase）驱动商业 Minecraft"的
   渲染器链路已经成立**，判据是游戏日志里的
   `OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)`
   （`docs/09` §22）。
2. 成立的前提是**三条互相独立的负面结论被绕开**，而不是被推翻：
   Android loader 不认 `VK_ICD_FILENAMES`（`docs/09` §11）、
   顶替 `libvulkan*` 名字会卡死 JVM（`docs/09` §13）、
   插件目录不在 `clns-9` 搜索路径（`docs/09` §17(a)）。
   绕开的手段是：**自建 125 入口转发层**（`source/shim/`）+
   **`pojavEnv: DLOPEN=` 预加载**（`RTLD_GLOBAL` 成为劫持点）+ **`DT_SONAME`**。
3. **失败点定位的价值高于成功本身**。**6 次迁移**（`docs/09` §20.5、§23–§25）中，
   两次是**转发层自己的 bug**（取指针失败、把物理设备当 instance），
   一次是 **Mesa 侧的 u_gralloc 后端缺编**，一次是 **WSI 的 gralloc 转换**，
   最后一次把矛头指向 **GPU 固件（CSF）在执行 draw 时的致命异常**。
   其中"生成器把首参含 `VkPhysicalDevice` 的函数误判为设备级"这类 bug，
   只有靠 **A/B 双打印**（同时打印"经我们解析的指针"与"直接问 GIPA 的指针"）才能一次定论（`docs/09` §20.1）。
4. **可观测性是硬约束，不是附加项**。Android 丢弃 app 的 `stderr`，
   `VK_LOADER_DEBUG=all` 因此**拿不到**；必须 `__android_log_print` + 落盘双写（`docs/09` §13、§18.4）。
   本项目后期的每一次定位都建立在这条通道上。
5. ★ **最重要的单条结论：WSI 不是最终瓶颈，CSF 的绘制执行才是。**
   独立探针证明驱动**能渲染**（`render` 的 `failures=0`、三点像素精确），
   且**交换链创建本来就能成功**（`win`/`headless`/`winimpdef` 三路 `VK_SUCCESS`）；
   真正的拦路者是 **`tri` 模式下 `vkQueueSubmit` 返回成功却 `vkWaitForFences` 得 `-4`**，
   对应 kbase 的 **CSF fatal exception `0xc3`**（`docs/09` §25、`research/12`）。
   6 次迁移的终点因此落在**绘制执行**上，而**不是** WSI / gralloc 上（§4.7、§6.6）。
   同时 `docs/09` §23.4 **更正**了"0444 ⇒ `supports_dmabuf()=false`"这条被反复引用的因果链
   （**构建树用的是 `O_RDONLY`**，实为 `true`）——这条链**从来没有真正拦住过 WSI**，
   这与 v49"关掉 dma-heap 无效"的实验正好吻合。

### 7.2 后续工作（按本文证据给出的优先级）

| 优先级 | 动作 | 依据 | 风险/代价 |
|---|---|---|---|
| **P0** | ★ **查 CSF exception `0xc3`**：`logcat -b all -d` / `dmesg` 抓 mali/kbase fault 记录、CSF 固件通知与 Mesa 侧对应时刻的 pipeline 状态 | `docs/09` §25.7、`research/12` §4 | 低（只读）；**当前唯一拦路者** |
| P1 | **最小化 draw 对照**：把 `tri` 拆成"只提交顶点 / 只绑描述符 / 只画 1 个三角形 / 换附件格式"，定位是哪一类动作触发 `0xc3` | `docs/09` §25.6/§25.7 | 低–中 |
| P2 | **v50 上机**（覆盖安装即可，同包名同签名）看 WSI 补丁是否按预期生效（找 `AHB layout fallback:` / `[PANVK-LINEAR-INFER]`），并确认 `-4` 是否仍在 | `docs/09` §24 | 低；⚠️ **注意 v50 不解决 `-4`** |
| P3 | 用设备侧探针判定 **`O_RDONLY` 下 `DMA_HEAP_IOCTL_ALLOC` 是否成功** / `supports_dmabuf()` 实际返回值 | `docs/09` §23.3/§23.4 | 低（~30 行 C，`research/06` R5 已给出写法） |
| P4 | **真实 App（ZL2 + MobileGL）端到端复现**，确认探针结论可推广到商业游戏进程 | `docs/09` §25.8 | 中（需允许真实屏 UI 操作的会话） |
| P5 | 走**正路**：把 `u_gralloc_imapper4`（`research/02`，最便宜）或 `imapper5`（`research/01`，已编出）接进构建 | `research/01/02/04` | 中；imapper5 已解决最难的编译/链接 |
| P6 | 消掉 §4.8 提到的**双栈**（ZL2 的 `load_vulkan()` 也走我们的 ICD），彻底消除"外来句柄"隐患 | `docs/09` §17 | 中高（需重签/自建 launcher，或改 MGL 本体） |
| **P7** | ★ **P2 实验：接上 tiler heap renew**（去掉 `submit->tiler_work_estimate` 前置条件，最小 2 行）—— 治「只涨不落」这个 OOM 的直接成因；对照组 = v52（无 flag）/ v53 | [`research/14`](14-tiler-heap-oom.md) Fix B1、[`research/16`](16-p1-p2-implementation.md) §6 | 低（2 行 + 增量重编）；**P1 失效后的当前最高优先级** |

> ✅ **本轮已完成的旧优先级**：旧 **P1**（05/06 构建目录核对）已闭合（§5.4 第 1 条、`docs/09` §24.4）；
> 旧 **P6**（跑 7 模式探针）已完成并扩到 **8 模式**（`docs/09` §25）；
> 旧 **P3**（落 05 方案 A）已实施并编入 v50，**且 v50 已上机**（`docs/09` §28.4：真交换链 + 真画面约 10 秒）；
> ★ **本轮新增的实验 P1（v53）已上机验证无效 ✗**（`docs/09` §27.3）⇒
> 落点转到新 **P7（= P2：接上 tiler heap renew，治「只涨不落」）**。
> 旧 P2（R4 杠杆 `no_afbc_usage`）**未做**，且因 §6.6 的再判断而**降级**。

---

## 附录 A 版本与哈希

### A.1 判据行达成版本

| 项 | 值 |
|---|---|
| APK | `mgl-panvk-v47.apk` |
| versionCode / versionName | `47` / `4.7-headless-wsi` |
| 大小 | 10,183,215 B |
| sha256（前缀） | `bb689838` |
| sha256（全文） | `bb6898389f9e791d51dd4e04d2fe6684de3c6b3846968ef3737c70f15a988abc` |
| 关键差异 | `pojavEnv` 增加 `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1` |
| 判据行 | `OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)` |

（`docs/09` §22 只给出 sha256 前缀 `bb689838`；全文哈希为本仓库在服务器 `/root/final/` 上
以 `sha256sum` 实测所得，**与 §22 的前缀一致**。）

### A.2 迁移链上的关键版本

| 版本 | versionName | sha256 前缀 | 说明 | 证据 |
|---|---|---|---|---|
| v16 | `1.6-loaderdebug` | `c26ba62d` | 带 `VK_LOADER_DEBUG=all`，用于§11 的铁证实验 | `docs/09` §11 |
| v18 | `1.8-shim-as-loader` | `1c990617` | 完整垫片以 `libvulkan.so` 名放入 ⇒ **走 blob**（名字没被命中） | `docs/09` §13 |
| v19 | `1.9-shim-both` | `5aed9bf9` | `.so` + `.so.1` 双名 ⇒ **卡死在 `JLI_Launch`** | `docs/09` §13 |
| v36 | `3.6-loader-semantics` | `f96884d9` | 123 项 thunk 表（loader 语义） | `docs/09` §18.2/§19 |
| v38 | `3.8-filelog` | `383184bd` | **日志双写** `/sdcard/MG/vkshim.log` | `docs/09` §20.4 |
| v39 | `3.9-pdpa-first` | `1e4c231f` | `gipa_pd()` 物理设备级优先 | `docs/09` §18.3（原文即给出前缀 `1e4c231f`，与本仓库实测一致 ✓）|
| v41 | `4.1-featmask` | `acf64880` | 建 device 前屏蔽驱动不支持的 feature 位 | `docs/09` §19（原文给出前缀 `acf64880`，与本仓库实测一致 ✓）|
| v43 | `4.3-a-test` | `f1d6a3b7` | `QFam-diag` A/B 二分（定案"转发层取空了"） | `docs/09` §20.1/§20.4 |
| v47 | `4.7-headless-wsi` | `bb689838` | ★ **判据行达成** | `docs/09` §22（原文给出前缀，与本仓库实测一致 ✓）|
| v48 | `4.8-diag-deep` | `e511f980` | 诊断加深；`-4` 的**首次出现**钉在纹理上传的 `vkQueueSubmit` | `docs/09` §23.1、[`../MANIFEST.md`](../MANIFEST.md) |
| v49 | `4.9-nodmaheap` | `f1389427` | 关掉 dma-heap（`PANVK_KBASE_DMA_HEAP`）做反向对照 ⇒ **错误码不变** | `docs/09` §23.2、[`../MANIFEST.md`](../MANIFEST.md) |
| v50 | `5.0-wsi-patched` | `677d81eb` | ★ **真 Android 交换链补丁**（新 `.so` md5 `e08e07645c16d8ebaa11ca70a09884fd` / 20,005,320 B）；**从未上机** | `docs/09` §24、[`11-wsi-patch-implementation.md`](11-wsi-patch-implementation.md) |
| v51 | `5.1-wsi-patched-debug` | `6e9ce7d2` | **空载荷 APK**（`unzip` 匹配失败）⇒ **废弃，勿用**（size 8,595 B） | `docs/09` §26.1、[`../MANIFEST.md`](../MANIFEST.md) §B |
| v52 | `5.2-wsi-patched-debug` | `0bbef030` | v50 载荷 + 全套调试 env；**落盘 logcat 抓到 `kbase: CSF group 0 tiler heap OOM notification`**（判据行 → +5~10 s → +9 s `-4`） | `docs/09` §26.2/§28.5、[`../MANIFEST.md`](../MANIFEST.md) §B |
| v53 | `5.3-p1-tiler-oom-csi` | `9c99af82` | ★ **P1**：1.18 档 + `csi_handlers`（新 `.so` md5 `7f3a0e8f…` / 20,005,600 B）⇒ ★ **真机无效 ✗** | `docs/09` §27、[`16-p1-p2-implementation.md`](16-p1-p2-implementation.md) |

> 说明：`docs/09` 只在 §18.3 / §19 / §22 三处**直接给出过 sha256 前缀**（v39 / v41 / v47），
> 三者与本仓库在服务器 `/root/final/` 上实测的 `sha256sum` **全部一致**，可作为台账可信度的交叉验证；
> 其余行的哈希由本仓库实测得出。完整台账（含大小与逐版本用途）见 [`../MANIFEST.md`](../MANIFEST.md)。

### A.3 关键源码/内容块哈希

| 文件 | 说明 |
|---|---|
| `source/shim/vkund.txt` | MGL 的 125 个 UND `vk*` 符号清单（**实测 125 行**） |
| `source/shim/gen_shim.py` | 转发层生成器 |
| `source/shim/gen_report.txt` | 生成报告（125 总符号 / 119 泛型 / 6 手写特殊） |
| `source/shim/vkshim_mgl.c` | **最终**转发垫片源码（静态链入 MGL） |

（各项 sha256 见 `MANIFEST.md` §C 与 `source/README.md`。）

### A.4 设备与软件基线

| 项 | 值 |
|---|---|
| 设备 | OPPO PHZ110 / MT6989 / Immortalis-G720 MC12 / Android 16 (SDK 36) / 无 root |
| 启动器 | Zalith Launcher 2（`com.movtery.zalithlauncher.v2`），游戏版本 `26.3 Fabric` |
| MobileGL | `4.6.0 MobileGL 26.09-dev, Direct (Vulkan) Backend, GIT@cbbaf77` |
| Mesa（PanVK 源） | `26.3.0-devel`，HEAD `6598829`（`funnymdzz/mesa@6598829019c`） |
| NDK | `android-ndk-r27c`（`aarch64-linux-android35-clang` 用于 panvk；`…26-clang` 用于探针） |
| 厂商 blob（对照） | `Mali-G720-Immortalis MC12` / Vulkan `1.3.247` / Driver `44.1.0` |
| 我们的 PanVK | `Mali-G720 MC12` / Vulkan `1.4.363` / Driver `26.2.24.3`（探针）或 `26.2.99`（真机日志） |

---

## 附录 B 复现步骤

> 步骤按"最小可判定"排序；每步都给出**判据**。所有命令都在本项目已实测过的形态下给出，
> 未实测的步骤均已标注。

### B.0 前置（一次性）

```bash
# 1) 确认设备有免 root 的 kbase 通路
python3 tools/kbase_probe.py                       # 判据：/dev/mali0 可开（mode 0666）+ 握手成功
# 2) 彻底验证（需 NDK）
aarch64-linux-android26-clang -O2 -o kbase_selftest tools/kbase_selftest.c
./kbase_selftest                                    # 判据：全部 PASS
```

### B.1 验证一份 PanVK 能当 ICD 用

```bash
aarch64-linux-android26-clang -O2 -o vkicd_probe tools/vkicd_probe.c -ldl
./vkicd_probe /path/to/libvulkan_freedreno.so
# 判据：dlopen 成功 + 导出 vk_icd* + deviceName: Mali-G720 MC12 + apiVersion 1.4.363
```

> **注意**：**必须**从**可执行目录**加载 —— `/storage` 是 noexec，
> 只有插件的 `nativeLibraryDir`（`/data/app/~~…/lib/arm64/`）与 `/data/local/tmp`（shell 侧）可用（`docs/09` §8）。

### B.2 重生成转发垫片（125 入口）

```bash
# 输入：125 个 UND 符号（本项目实测值见 source/shim/vkund.txt）
# 环境：NDK 的 sysroot（vulkan_core.h / vulkan_android.h）
python3 source/shim/gen_shim.py                    # → shim_gen.inc + gen_report.txt
bash    source/shim/build_shim.sh                  # → libvkpanvk_shim.so（记得 -Wl,-soname,libvkpanvk_shim.so）
# 判据（必须两条都过）：
readelf -sW --dyn-syms libvkpanvk_shim.so | awk '$4!="FILE" && $7!="UND" && $8~/^vk/{print $8}' | sort -u | wc -l   # = 125
comm -23 <(sort -u source/shim/vkund.txt) <(readelf -sW --dyn-syms libvkpanvk_shim.so \
  | awk '$4!="FILE" && $7!="UND" && $8~/^vk/{print $8}' | sort -u)                                                  # 空
```

### B.3 打包渲染器插件

```bash
bash source/pack/pack_mgl_plugin.sh ./libvulkan_freedreno.so "<插件显示名>" <短名>
# 模板：source/pack/AndroidManifest.v46.xml（renderer / pojavEnv / fclPlugin / des / minMCVer）
```

**清单里必须有**（缺一不可，`docs/09` §17）：

```xml
<meta-data android:name="fclPlugin" android:value="true" />
<meta-data android:name="renderer"  android:value="magma_panvk:libMobileGL.so:libMobileGL.so" />
<meta-data android:name="pojavEnv"  android:value="LIBGL_ES=3:POJAV_RENDERER=opengles3:
    MOBILEGL_BACKEND_TYPE=DirectVulkan:MOBILEGL_ESPRYT_USE_ANGLE=0:
    MOBILEGL_MAGMA_R11G11B10F_FALLBACK=0:DLOPEN=libvkpanvk_shim.so" />
```

> 注意：`pojavEnv` 用**冒号**分隔，路径里**不能含冒号**（`docs/09` §5）；
> 且 `DLOPEN` **只有 `pojavEnv` 会被解析**，写 `boatEnv` 无效（`docs/09` §17(b)）。
> 另需一个带 `LAUNCHER` 的 Activity，否则 FCL 扫描不到插件（仓库根 `README.md` 坑 3）。

### B.4 安装与读取判据

```bash
# 安装（新包名被拦时，复用已安装包名 + 更高 versionCode ⇒ 变成更新）
cmd package install -r -t /data/local/tmp/plugin.apk
# 回滚也要递增 versionCode（降级安装会被 INSTALL_FAILED_VERSION_DOWNGRADE 拒绝，docs/09 §9）
```

**判据行**（一条命令，`docs/09` §18.4）：

```bash
grep -a "OpenGL Renderer" \
  "/storage/emulated/0/Android/data/com.movtery.zalithlauncher.v2/files/.minecraft/versions/26.3 Fabric/ZalithLauncher/latest_game.log"
# 期望：Mali-G720 MC12, Vulkan 1.4.363   ← 我们的驱动
# 反例：Mali-G720-Immortalis MC12, Vulkan 1.3.247, Driver 44.1.0  ← 厂商 blob
```

**辅助日志**：

```bash
cat /sdcard/MG/vkshim.log        # 垫片落盘日志（不受 logcat 环形缓冲影响）
tail -5 /sdcard/MG/latest.log    # MGL 自己的日志（看是否还有 vkCreateDevice FATAL）
```

### B.5 （未实测）探针与 WSI 补丁

```bash
bash /root/research/probe10/build.sh        # 已实测 BUILD OK（research/10）
bash /root/research/probe10/run-device.sh   # 在有 adb 的主机上：自动 push / 校验 md5 / logcat 取证
# 判据：logcat tag = MESA（默认 INFO 即会打）
#   mapper load failed / init how= rc= version=(须 ≥5) / import rc= /
#   metadata *_rc= / refusing guessed layout / Failed to get u_gralloc_buffer_basic_info
```

> ⚠️ **不要在 `.so` 旁建 `android_stub/`**（`research/10` 明确警告）。

---

## 附录 C 参考文献

### C.1 本仓库内部（唯一权威记录）

| 引用 | 内容 |
|---|---|
| [`../docs/09-mobilegl-integration.md`](../docs/09-mobilegl-integration.md) | ★ 工程实录（25 节，含逐条原文日志与源码行号） |
| [`../docs/01-why-mali.md`](../docs/01-why-mali.md) | 背景：为什么 Mali 玩家需要这个项目 |
| [`../docs/02-kbase-bringup.md`](../docs/02-kbase-bringup.md) | 免 root 驱动 Mali kbase 的原理与实测 |
| [`../docs/03-fcl-adreno-lock.md`](../docs/03-fcl-adreno-lock.md) | FCL / ZL2 的 Adreno 厂商锁与拆锁方案 |
| [`../docs/04-build-g720-panvk.md`](../docs/04-build-g720-panvk.md) | 构建 G720/v12 PanVK 的完整流程 |
| [`../docs/05-findings.md`](../docs/05-findings.md) | 上游成品驱动实测对比与崩溃栈分析 |
| [`../docs/06-roadmap.md`](../docs/06-roadmap.md) | 路线图与测试顺序 |
| [`../docs/07-our-build-and-results.md`](../docs/07-our-build-and-results.md) | 我们自己的构建结果与实测卡点 |
| [`../docs/08-mobilegl-vulkan.md`](../docs/08-mobilegl-vulkan.md) | MobileGL 两个后端与渲染器插件契约 |
| [`01-aidl-route.md`](01-aidl-route.md) – [`10-verify-probe.md`](10-verify-probe.md) | 专题研究报告 01–10 |
| [`summaries/`](summaries/) | 各篇的中文结论摘要 |
| [`../MANIFEST.md`](../MANIFEST.md) | 产物台账（APK 文件名/大小/sha256 前缀/用途） |
| [`../CHANGELOG.md`](../CHANGELOG.md) | 按里程碑的进展台账 |
| [`../source/README.md`](../source/README.md) | 源码清单与每个文件的来源/用途 |

### C.2 上游源码（本文引用到的具体位置）

| 项目 | 引用位置 | 用途 |
|---|---|---|
| Mesa | `src/panfrost/vulkan/panvk_vX_device.c:376` | `-3` 的队列族根因 |
| Mesa | `src/panfrost/vulkan/panvk_physical_device.c:1408` | `VK_ERROR_INITIALIZATION_FAILED` 的 arch 分派初值 |
| Mesa | `src/vulkan/runtime/vk_android.c`（`u_gralloc_get_buffer_basic_info` 调用点、`:141/:149/:174/:218/:670/:1066`） | `VK_ERROR_INVALID_EXTERNAL_HANDLE` 根因链 |
| Mesa | `src/util/u_gralloc/u_gralloc.c:25-34` | AUTO 后端尝试顺序 |
| Mesa | `src/util/u_gralloc/u_gralloc_fallback.c:427-431` | FALLBACK 主动 fail-closed |
| Mesa | `src/vulkan/wsi/wsi_common.c` | `MESA_VK_WSI_HEADLESS_SWAPCHAIN` 的作用 |
| Mesa | `src/panfrost/vulkan/panvk_image.c:307-331`、`pan_layout.c:69`、`pan_mod.c:807-815` | modifier 强制的后果（NULL 解引用/assert） |
| Mesa | `src/panfrost/vulkan/panvk_android.c:46-58`、`:226/:293/:405` | `get_fd_mem_type_bits` 静默返回 0 |
| Mesa | `src/panfrost/vulkan/panvk_wsi.c:79-88/:109` | `wsi_device_init` 与 kbase 开关 |
| Mesa | `src/android_stub/hardware_stub.cpp:6-11`、`nativewindow_stub.cpp` | `hw_get_module` 是桩（SIGSEGV 隐患） |
| Mesa | `src/util/u_gralloc/u_gralloc_imapper4_api.cpp` / `u_gralloc_imapper5_api.cpp` | imapper4/5 后端 |
| AOSP | `frameworks/native/libs/gralloc/types/Gralloc4.cpp` / `include/gralloctypes/Gralloc4.h` | libgralloctypes 的编解码 |
| AOSP | `hardware/interfaces/graphics/mapper/stable-c/README.md` | "v5 是 native stable-C 而非 AIDL/HIDL" |
| AOSP | `platform/prebuilts/vndk/v29…v34` | 预生成 HIDL 头 + AIDL graphics/common + `gralloctypes/Gralloc4.h` |
| ZL2 | `game/plugin/renderer/RendererPluginManager.kt:110-125`、`RendererPlugin.kt:48`、`game/launch/GameLauncher.kt:208-222`、`:274-280` | `pojavEnv`/`DLOPEN` 语义与运行期库搜索路径 |
| ZL2 | `egl_bridge.c:80/:134-197/:194` | Surface 契约与桥选择（`POJAV_RENDERER`） |
| MobileGL | `MG_Backend/DirectVulkan/Renderer/VulkanRenderer.cpp:2983/:12979`、`VkRenderPassManager.cpp:1077-1096`、`VkTextureManager.cpp` | 默认 FBO / present 入口 / `-4` 崩点 |

### C.3 第三方项目与致谢

- [Mesa3D](https://gitlab.freedesktop.org/mesa/mesa) —— PanVK 本体
- [zenithblue-oss/panvk-kbase-android](https://github.com/zenithblue-oss/panvk-kbase-android) —— PanVK kbase 后端基线
- [wonderkast02/panvk-g720-kbase-csf](https://github.com/wonderkast02/panvk-g720-kbase-csf) —— G720 / v12 CSF 参考实现
- [MobileGL](https://github.com/MobileGL-Dev/MobileGL) —— DirectVulkan（Magma）后端
- [Zalith Launcher 2](https://github.com/ZalithLauncher/ZalithLauncher2) / [Fold Craft Launcher](https://github.com/FCL-Team/FoldCraftLauncher) —— 目标启动器
