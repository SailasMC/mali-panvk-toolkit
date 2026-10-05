# MobileGL（DirectVulkan）接入我们的 PanVK：实录

> 目标：`Minecraft 26.x → MobileGL(DirectVulkan/Magma) → Vulkan loader → 我们的 PanVK → kbase`
> 本文记录**实际怎么做的、踩了哪些坑、卡在哪一步**。诚实标注哪些已验证、哪些还没。

---

## 1. 结论速览

| 项 | 状态 |
|---|---|
| MobileGL 源码编译出 `libMobileGL.so` | ✅ 成功（含 `DirectGLES` + **`DirectVulkan`(Magma)** 两个后端）|
| 打成 FCL/ZL2「渲染器插件 APK」 | ✅ 成功（10.2MB）|
| 装进设备 | ✅ 成功（见 §4 的静默安装绕过技巧）|
| 启动器识别为渲染器 | **ZL2 ✅ / FCL ✗**（契约不同，见 §5）|
| 在 ZL2 里选中 `MobileGL (Vulkan)` | ✅ 成功（配置已保存）|
| 真机跑起来验证 | ⏳ **尚未完成**（需在设备上点「启动游戏」）|

## 2. 编译：需要的子模块与四个坑

MobileGL 用 CMake + C++23。**只需这些子模块**（`CMakeLists.txt` 里 `add_subdirectory` 的）：

```
3rdparty/glslang  SPIRV-Cross  VulkanMemoryAllocator  Vulkan-Headers
Vulkan-Utility-Libraries  SPIRV-Reflect  asio  xxHash  include/ska
（DiligentCore 在 CMakeLists 里是注释掉的，不需要！
 apitrace 只有 trace flavor 需要，插件不需要）
```

```bash
# 用 tarball 而不是 git clone（实测 codeload 6MB/s vs git clone ~100KB/s）
curl -sL https://codeload.github.com/<owner>/<repo>/tar.gz/<commit> | tar xz -C <目标> --strip-components=1

cmake -S MobileGL -B build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \    # ★ 必须 OFF，见坑4
  -DMOBILEGL_BUILD_TEST=OFF -DMOBILEGL_BUILD_BENCHMARK=OFF \
  -DMOBILEGL_BUILD_TRACE_REPLAY=OFF -DMOBILEGL_ENABLE_TRACY=OFF
ninja -C build-android -j$(nproc)
```

| # | 坑 | 现象 | 解法 |
|---|---|---|---|
| 1 | 递归 `git clone --recursive` 卡死 | 子模块目录一直 8K，网络其实很好 | 改用 codeload **tarball** 精确拉取 |
| 2 | glslang 缺 SPIRV-Tools | `ENABLE_OPT set but SPIR-V tools not found` | 从 SPIRV-Tools 的 **DEPS** 里读出它要求的 `spirv_headers_revision`，按那个 commit 拉（乱配会报 `no member named 'OpGraphARM'`）|
| 3 | MGL 自带旧 `spirv.hpp11` 抢了包含路径 | 同样报 `OpGraphARM`/`OpMemberDecorateIdEXT` 缺失 | 把 SPIRV-Headers 的 `spirv.hpp11/spirv.h/spirv.hpp` **覆盖**到 `MobileGL/include/glslang/SPIRV/` |
| 4 | **`BUILD_SHARED_LIBS=ON` 把 glslang 编成 .so** | 链接期一堆 `undefined symbol: glslang::SetThreadPoolAllocator…` | 改回 **OFF**（MGL 注释明确写它「statically embeds glslang/SPIRV-Tools/SPIRV-Cross」）|

产物 308MB（带调试符号）→ `llvm-strip --strip-unneeded` 后 **16.9MB**。

## 3. 渲染器插件契约（关键！两个启动器不一样）

从 **ZL2 源码** `game/plugin/renderer/RendererPluginManager.kt` 读到（这是权威）：

```kotlin
if (metaData.getBoolean("fclPlugin", false) ||
    metaData.getBoolean("zalithRendererPlugin", false)) {
    val rendererString = metaData.getString("renderer")   // "id:GL库:EGL库"，启动器会 dlopen
    val des            = metaData.getString("des")
    val pojavEnvString = metaData.getString("pojavEnv")   // "K=V:K=V"（冒号分隔），逐个 setenv
    minMCVer / maxMCVer
}
```
其中 `pojavEnv` 里的键：
- `POJAV_RENDERER` → 渲染器 id
- `LIB_MESA_NAME` / `MESA_LIBRARY` → 值会被**自动补全插件的 nativeLibraryDir**
- **其它键原样传入** ✓（我们正是靠这个塞 `VK_ICD_FILENAMES`）

另外还有一套 `fclPlugin_V2`（`@string/config` 资源）的新格式。

**实测**：我们按上面 v1 契约打的 APK，**ZL2 能识别** ✓（列表出现 `MobileGL (Vulkan)`，还标注「来自 … 插件」）；
但 **FCL 1.3.3.5 不认** ✗（渲染器列表只有内置 3 项：Krypton Wrapper / Holy GL4ES / VirGLRenderer）——
说明 FCL 这一版的契约与 ZL2 不同（很可能要求 V2 格式）。**想用 FCL 必须改用 V2 格式重打。**

## 4. 静默安装被拦时的绕过（实测有效）

新包名的 `cmd package install` 在收紧后的策略/ROM 上会被拦（返回 `Failure [-99]`）。
**技巧：把插件打成「已安装的包名」** → 安装变成**更新**，即可 `Success`：

```bash
# manifest: package="<某个已安装的包名>" android:versionCode="<更高>"
cmd package install -r -t /data/local/tmp/plugin.apk     # → Success
```
（我们复用了自己以前装的驱动插件包名，同时保留渲染器 meta-data，一个包两个身份。）

## 5. Vulkan 走我们 PanVK 的方案：ICD

MobileGL 的 Magma 后端用**标准 Vulkan loader**，其 ICD 搜索可用
`VK_ICD_FILENAMES` **覆盖**（排他，不再看 `/vendor/etc/vulkan/icd.d`）✓。

而 `pojavEnv` 的值是**字面字符串**（不像 `MESA_LIBRARY` 会自动补全路径），
拿不到「插件自身目录」这种运行时路径。**解决办法**：把 ICD 放到一个固定可读路径，
在插件清单里写死：

```xml
<meta-data android:name="pojavEnv" android:value="
   MOBILEGL_BACKEND_TYPE=DirectVulkan:
   VK_ICD_FILENAMES=/storage/emulated/0/mali-icd/panvk_icd.json" />
```

```
/storage/emulated/0/mali-icd/
├── panvk_icd.json              { "ICD": { "library_path": "…/libvulkan_freedreno.so" } }
└── libvulkan_freedreno.so      我们的 PanVK（G720/v12，20MB）
```
（注意：`pojavEnv` 用冒号分隔，路径里**不能含冒号**；用纯 ASCII 路径更稳。）

## 6. 卡住的地方 & 下一步

**卡点**：需要在设备上点启动器的「启动游戏」才能验证。自动化点击受两个环境因素影响：
屏幕自动锁定很快（防烧屏设置），且 DSH 会把前台抢回去 → 点击落不准。

**下一步**：
1. 在 ZL2（版本 `26.3 Fabric`，渲染器已选 `MobileGL (Vulkan)`）点「启动游戏」
2. 看日志 `…/files/.minecraft/versions/<ver>/ZalithLauncher/latest_game.log`，期望：
   - 出现 **`MobileGL`** 与 **`Direct (Vulkan)`**
   - Vulkan 设备名为 **`Mali-G720 MC12`**（我们的 PanVK）而非 `Mali-G720-Immortalis MC12`（厂商 blob）
3. 若 Magma 初始化失败：用 `MOBILEGL_BACKEND_TYPE=DirectGLES` 做对照（区分是「插件没生效」还是「Magma 在我们的驱动上有问题」）

**已排除**：`libMobileGL.so` 的 Vulkan 后端确实编进去了（`strings` 命中 115 处 `DirectVulkan|Magma`）✓

---

## 7. 实测结果（2026-10-05 凌晨，OPPO PHX110 / 天玑9300 / Immortalis-G720 MC12 / Android 16）

### ✅ 已验证：MobileGL 的 Direct (Vulkan) 后端在本机可用

ZL2（版本 `26.3 Fabric`）启动日志：

```
▷ Renderer: MobileGL Magma_1001_Vulkan(OpenGL 4.6,1.17+)
▷ Renderer Summary: 来自 MobileGL Magma 插件
▷ POJAVEXEC_EGL = libMobileGL.so
[DEBUG] DLOPEN: …/com.mio.plugin.renderer.MGL.Magma-…/lib/arm64/libMobileGL.so , success
[06:29:36] Using graphics backend OpenGL, using drivers:
           4.6.0 MobileGL 26.09-dev, Direct (Vulkan) Backend, GIT@cbbaf77
```

游戏内 **FPS 稳定 59–60** ✓（`FPS: 59` / `FPS: 60` 出现在 ZL2 的悬浮层）。

**⇒ 结论：MobileGL 的 Magma = Direct(Vulkan) 路径在这台 Mali 设备上完全能跑，帧率满血。**

### ⚠️ 当时用的驱动是厂商 blob，不是我们的 PanVK

同一份日志里出现 **`Mali-G720-Immortalis MC12`**（厂商 blob 的设备名）。
我们编的 PanVK 设备名是 **`Mali-G720 MC12`**（不带 `-Immortalis`）—— 所以「是否用上我们的驱动」有一个**一眼可判的指纹** ✓。

### 🔧 让 MGL 用上我们 PanVK 的做法（已就绪，待最终验证）

社区插件（`com.mio.plugin.renderer.MGL.Magma`）的清单里 **没有** `VK_ICD_FILENAMES`（所以走 blob）。
本仓库的 v12 插件复刻了它的全部契约并**追加**了 ICD 指向：

```
renderer = magma_panvk:libMobileGL.so:libMobileGL.so     # 用独立 id，避免与社区版 magma 撞车
pojavEnv = LIBGL_ES=3:POJAV_RENDERER=opengles3:MOBILEGL_BACKEND_TYPE=DirectVulkan
           :MOBILEGL_ESPRYT_USE_ANGLE=0:MOBILEGL_MAGMA_R11G11B10F_FALLBACK=0
           :VK_ICD_FILENAMES=/storage/emulated/0/mali-icd/panvk_icd.json   ← 关键
```
（`libMobileGL.so` 直接采用**社区版那个已验证的**构建，唯一变量隔离到「驱动来源」上。）

### 经验：改别人签名的插件装不上

重打包社区插件的包名 + 用我们的 key 签名 → 安装会因**签名不符**失败。
**可行做法**：复刻它的 meta-data 到自己能更新的包名里（本文的 v12）。

### 经验：自动化点按的两条通道

- **无障碍通道**（`android_ui_*`）：能读语义树，但每次调用可能把 Harness 自己的前台抢回来，导致点按落空。
- **ADB 注入**（本仓库环境里的 `android_act_input`）：不经过无障碍、**不会抢前台**，成功点中了启动器的「启动游戏」；
  但它注入的 `HOME`/`BACK`/手势会被**沉浸式游戏**吞掉。

---

## 8. ★ 致命坑：`/storage` 是 noexec —— ICD 必须指向「可执行路径」

把 `libvulkan_freedreno.so` 放在 `/storage/emulated/0/...` 并在 ICD JSON 里引用它，**必然失败**：

```
✘ dlopen failed: couldn't map ".../libvulkan_freedreno.so" segment 2: Permission denied
```

因为 Android 的 `/sdcard`(FUSE) 挂载带 **noexec**。ICD 的 `library_path` **必须指向可执行目录**。

**唯一可靠的可执行路径 = 插件自己的 `nativeLibraryDir`**：

```
/data/app/~~XXXX==/<你的包名>-YYYY==/lib/arm64/       ← 可执行 ✓（且能放我们的 .so）
```
（`/data/data/<pkg>/files` 自 Android 10 起也是 noexec；`/data/local/tmp` 只对 shell 可执行，app 受 SELinux 限制。）

**正确分工**：
| 文件 | 放哪 | 为什么 |
|---|---|---|
| `panvk_icd.json` | `/storage/emulated/0/mali-icd/` | 只需**可读**（app 有存储权限即可）|
| `libvulkan_freedreno.so` | **插件 APK 的 `lib/arm64-v8a/`** | 只有这里**可执行** ✓ |
| `VK_ICD_FILENAMES` | 插件 `pojavEnv` 里的字面量 → 指向上面那个 json | 路径里不能含冒号 |

**路径怎么来**：装好插件后读 `pm path <包名>`，取 `/lib/<abi>` 前缀并写进 json 的 `library_path`
（注意：只要不重装，这个 `/data/app/~~hash==/…-hash==/` 路径是**稳定**的）。

验证命令（shell 侧即可，`/dev/mali0` 是 0666，任何 uid 都能开）：

```bash
VK_ICD_FILENAMES=/storage/emulated/0/mali-icd/panvk_icd.json ./vkprobe <插件lib目录>/libvulkan_freedreno.so
# →  deviceName : Mali-G720 MC12      ← 我们的 PanVK
#    apiVersion : 1.4.363
#    driverVersion : 26.2.24.3
```

### 实测（本机 OPPO PHZ110 / 天玑9300 / Immortalis-G720 MC12 / Android 16）

| 检查 | 结果 |
|---|---|
| 我们的 PanVK dlopen（从 `/data/local/tmp`）| ✅ ICD 形态：`vk_icdNegotiateLoaderICDInterfaceVersion` / `vk_icdGetInstanceProcAddr` / `HMI` 全有 |
| 从 `/storage/...` dlopen | ❌ Permission denied（noexec）|
| 从插件 `nativeLibraryDir` dlopen | ✅ **`deviceName: Mali-G720 MC12`**，API 1.4.363，扩展 181 个 |
| MobileGL `Direct (Vulkan)` 后端（跑厂商 blob）| ✅ 26.3 进游戏，**FPS 59–60** |

---

## 9. ★★ 两个关键负面结论（2026-10-05 实测）

把 PanVK 真正接进 MobileGL 的两次尝试都**失败**了，但原因非常明确、可复用：

### ① Android 的系统 `libvulkan.so` **忽略** `VK_ICD_FILENAMES`

实测（ZL2 26.3 启动日志），环境变量**确实注入成功**了：

```
▷ Renderer: MobileGL Magma + PanVK
▷ MOBILEGL_BACKEND_TYPE = DirectVulkan
▷ VK_ICD_FILENAMES = /storage/emulated/0/mali-icd/panvk_icd.json     ← 已生效 ✓
[06:38:16] Using graphics backend OpenGL, using drivers: 4.6.0 MobileGL 26.09-dev, Direct (Vulkan) Backend
```
但同一次运行里 Vulkan 设备名依然是 **`Mali-G720-Immortalis MC12`**（厂商 blob）✗。

对 `libMobileGL.so` 做字符串分析：

```
VK_ICD_FILENAMES          ← 它认得这个变量 ✓
dlopen: libvulkan.so / libvulkan.so.1     ← 走的是系统 loader
volk 字样: 0 / 内嵌 loader: 无
```

**⇒ 它走系统 loader，而 AOSP 的 Vulkan loader 出于安全不开放在进程里改 ICD 路径**
（`/vendor/etc/vulkan/icd.d` 是它唯一认的入口，且只读）。所以 `VK_ICD_FILENAMES`
在桌面上好用、**在 Android 上不可依赖**。

### ② 用 `libvulkan.so.1` 顶替 loader 会**卡死 App**

思路：插件 nativeLibraryDir 在 linker 搜索路径靠前，把我们的 `libvulkan.so.1` 放进去，
让 MGL 的 `dlopen("libvulkan.so.1")` 命中它。**结果**：

```
[INFO]  SDL_Hook: Successfully initialized SDL hooks …
[DEBUG] Found JLI lib
[DEBUG] Calling JLI_Launch          ← 之后 6 分钟无输出，JVM 卡死 ✗
```

**⇒ 这个替换是进程级的**：JVM/LWJGL 进程里**所有** `libvulkan.so.1` 的解析都被改掉了，
初始化直接挂住。**别这么做。**

（回滚：`versionCode` 必须**递增**，降级安装会被 `INSTALL_FAILED_VERSION_DOWNGRADE` 拒绝——
所以回滚也要重新签一个更高 versionCode 的包。）

### ③ 那正确的路是什么？

不要让 MGL 走 loader，**让 MGL 直接 dlopen 我们的驱动**：

- MGL 是开源且**已有 `VK_ICD_FILENAMES` 字符串**，说明它内部有一层薄封装；
- 最小改动方案：在 MGL 的 Vulkan 初始化处，优先 `dlopen("<插件自身目录>/libvulkan_freedreno.so")`
  并用 `vk_icdGetInstanceProcAddr` 取入口（ICD 可直接当"loader"用 ✓ 本仓库探针已验证这一点）；
- 插件自身目录可用 `dladdr()` 在运行时求得，无需硬编码。

**前提已验证** ✓：我们的 PanVK 作为 ICD 形态可加载可初始化：

```
✔ dlopen 成功（从 /data/local/tmp 与插件 nativeLibraryDir 均可）
✔ vk_icdNegotiateLoaderICDInterfaceVersion / vk_icdGetInstanceProcAddr / HMI 齐全
  deviceName : Mali-G720 MC12      apiVersion : 1.4.363     扩展 181 个
```

### ④ 这轮得到的**可用成果**（已生效）

| 项 | 状态 |
|---|---|
| MobileGL `Direct (Vulkan)` 后端在 G720 上跑 26.3 | ✅ **FPS 59–60** |
| 我们的插件被 ZL2 正确识别与选中 | ✅ `▷ Renderer: MobileGL Magma + PanVK` |
| `VK_ICD_FILENAMES` 注入链路 | ✅ 环境变量确实进到了游戏进程 |
| 我们的 PanVK 作为 ICD | ✅ `Mali-G720 MC12`（独立探针验证）|
| 让 MGL 用上它 | ❌ 被 Android loader 挡住（见 ① ②），需改 MGL 源码（见 ③）|

---

## 10. 为什么"垫片"路线不现实 + 下一步诊断

把 `libMobileGL.so` 的 `DT_NEEDED` 从 `libvulkan.so` 换成我们的驱动（`patchelf --replace-needed`）看似优雅，
但**行不通**，因为两边 ABI 角色不同：

| | 导出 | 谁调用 |
|---|---|---|
| 系统 `libvulkan.so`（loader）| **125 个** loader 风格入口 | MGL 链接期直接引用 |
| 我们的 `libvulkan_freedreno.so`（ICD）| 只有 `vk_icdGetInstanceProcAddr` / `vk_icdNegotiateLoaderICDInterfaceVersion` | loader 通过 `vk_icd*` 调用 |

实测（`readelf -sW --dyn-syms`）：

```
libMobileGL.so 未定义(UND)的 vk* 符号: 125 个
  vkCreateInstance, vkGetInstanceProcAddr, vkCreateDevice, vkAllocateMemory, vkCmdDraw, …
我们驱动导出的: vk_icdGetInstanceProcAddr ✔ | vk_icdNegotiateLoaderICDInterfaceVersion ✔
                vkCreateInstance ✘ | vkGetInstanceProcAddr ✘ | vkCreateDevice ✘
```

⇒ 要么写一个转发 **125 个**入口的垫片（不现实），要么让**系统 loader 真的去加载我们的 ICD**。

### 下一步：让 loader 自己说为什么没加载我们的 ICD

Android 的 loader **是否**忽略 `VK_ICD_FILENAMES` 还没有直接证据（我们的现象只是"最终用了 blob"）。
让 loader 打印它的搜索与加载过程即可确诊：

```
# 在插件的 pojavEnv 里追加：
VK_LOADER_DEBUG=all
# 然后读 logcat：
logcat -d | grep -iE "vk_icd|VK_ICD|ICD .*(load|found|skip)|libvulkan"
```

可能的结果与对策：
- 若打印 `ICD … not found / skipped` → 是**路径/权限/ABI** 问题，可对症修（例如路径不可执行、缺依赖）；
- 若**完全没提** `VK_ICD_FILENAMES` → 确认是 AOSP 的 loader 安全限制，只能回到"改 MGL 源码直接 dlopen 驱动"；
- 若打印 `loading … ok` 但最终仍选 blob → 是 **loader 内部策略**（如只信任 vendor 目录），需查该 ROM 的 loader 实现。

---

## 11. ★★ 铁证：Android loader 确实忽略 `VK_ICD_FILENAMES`

2026-10-05 07:03 实测（ZL2 `26.3 Fabric`，插件 `MobileGL Magma + PanVK` v16 带 `VK_LOADER_DEBUG=all`）：

```
[07:03:38] [Render thread/INFO]: Using graphics backend OpenGL, using drivers:
           4.6.0 MobileGL 26.09-dev, Direct (Vulkan) Backend, GIT@cbbaf77
[07:03:41] [Render thread/INFO]: OpenGL Renderer: Magma (MobileGL Core)
           (Mali-G720-Immortalis MC12, Vulkan 1.3.247, Driver 44.1.0)
```

**版本号就是铁证**（设备名还可能是巧合，版本号不是）：

| | deviceName | apiVersion | driverVersion |
|---|---|---|---|
| **实际生效（厂商 blob）** | `Mali-G720-Immortalis MC12` | **1.3.247** | **44.1.0** |
| **我们的 PanVK**（独立探针实测） | `Mali-G720 MC12` | **1.4.363** | 26.2.24.3 |

⇒ 即使 `VK_ICD_FILENAMES` 已确认注入进程（日志里有 `▷ VK_ICD_FILENAMES = …`），
loader 依然选了 vendor blob。**Android 上这条路封死。**

另外：`VK_LOADER_DEBUG=all` 的输出**没进 logcat** —— Android app 的 stderr 默认被丢弃，
所以 loader 的内部诊断在这个环境下拿不到。

### 结论：只剩两条可行路

**(A) 写一个导出 Vulkan 核心 API 的垫片 .so**（推荐、不动 MGL 源码）
- MGL 的 `libMobileGL.so` 有 **125 个** `vk*` UND 符号（loader 风格），我们的 ICD 只导出 `vk_icd*`；
- 垫片要导出这 125 个名字，内部 `dlopen` 我们的驱动 → `vk_icdGetInstanceProcAddr` → 逐一分发；
- **可自动生成**：从 `vulkan_core.h` + `vulkan_android.h` 解析函数签名，脚本生成 C 转发层；
- 然后把 `libMobileGL.so` 的 `DT_NEEDED: libvulkan.so` 用 `patchelf --replace-needed` 指向垫片
  （垫片名要**唯一**，例如 `libvkpanvk_shim.so` —— 千万不能叫 `libvulkan.so.1`，那会进程级污染并卡死 JVM，见 §9②）。

**(B) 改 MobileGL 源码**：在它的 Vulkan 初始化处优先用 `vk_icdGetInstanceProcAddr`，
把 125 个入口换成从 ICD 取（改动量比 A 大，但更"干净"、也更容易上游化）。

两条路都**不需要** Android loader 配合，因此都能绕开本节的封堵。

---

## 12. ★★ 解法：自建「完整 Vulkan 转发垫片」，冒充 `libvulkan.so`

### 为什么可行
Android 的系统 loader 不认 `VK_ICD_FILENAMES`（§11 铁证），但**插件目录在 linker 搜索里优先于系统目录**
（§9② 的"卡死事故"恰好证明了这种优先级的真实存在）。
所以：**用我们自己的完整实现顶掉 `libvulkan.so` 这个名字**，只影响这一个进程。

与 §9② 那次失败的关键区别：
| | §9② 失败版本 | 本节正确版本 |
|---|---|---|
| 放置的东西 | 我们的**裸 ICD** | **完整转发层** |
| 导出 | 只有 `vk_icd*` | **125 个 loader 风格入口** |
| 结果 | JVM 找不到入口 → 卡死在 `JLI_Launch` | 任何 Vulkan 使用方都能正常工作 |

### 垫片怎么来（可复现）
1. 取 MobileGL 的未定义符号清单（`libMobileGL.so` 里 UND 的 `vk*`）——**125 个**；
2. **写生成器**：从 NDK 的 `vulkan/vulkan_core.h` + `vulkan_android.h` 解析
   `typedef <ret> (VKAPI_PTR *PFN_<name>)(<params>);`，自动生成转发函数；
3. 每类按首参分派：
   - `VkInstance` / `VkPhysicalDevice` → `g_gipa(<首参>, "<name>")`
   - `VkDevice` / `VkQueue` / `VkCommandBuffer` → `g_gdpa(<设备>, "<name>")`（命令缓冲没有 device 时用**缓存的 `g_dev`**）
   - 无参数或首参不是可调度句柄（`vkCreateInstance`、`vkEnumerateInstanceExtensionProperties`）→ `g_gipa(NULL, "<name>")`
   - `vkEnumerateInstanceLayerProperties` **自己实现**（0 层、`VK_SUCCESS`）
4. `g_gipa` / `g_gdpa` 的来源：`dlopen(我们的 ICD)` → `vk_icdNegotiateLoaderICDInterfaceVersion(&7)` →
   `dlsym("vk_icdGetInstanceProcAddr")`；device 级经 `gipa(NULL,"vkGetDeviceProcAddr")` 取。
5. 编译：`aarch64-linux-android28-clang -shared -fPIC -O2 -o libvkpanvk_shim.so vkshim.c -ldl`
6. **把垫片以 `libvulkan.so` 之名放进插件 APK 的 `lib/arm64-v8a/`**，MGL 原件**一个字节都不用改**。

### ★ 生成器里两个必须避开的坑（都实际踩过）
1. **正则不能跨 typedef**：
   `typedef\s+(.+?)\s*\(VKAPI_PTR\s*\*\s*PFN_<name>\)`（`re.S`）会从**文件里最靠左的 `typedef`** 开始吞，
   把整段 struct/flag 定义当成"返回类型"——症状是 `vkCreateInstance` 的返回类型变成几 KB 的块，
   `vkCreateAndroidSurfaceKHR` 变成 `VkFlags VkAndroidSurfaceCreateFlagsKHR; …`。
   **修法**：返回类型组限制为 `[^;{}]*?`（或等价地只允许 标识符/空白/星号），并加断言 `;{}`/长度上限。
2. **`vkCreateInstance` / `vkEnumerateInstanceExtensionProperties` 不是无参**——
   它们有参数，只是首参**不是可调度句柄**；分类规则要用"首参类型"而不是"有没有参数"。

### 自查（一条命令）
```bash
# 导出清单（注意必须排除 STT_FILE 伪符号，否则 vkshim.c 会被算进去凑成 126）
readelf -sW --dyn-syms libvkpanvk_shim.so | awk '$4!="FILE" && $7!="UND" && $8~/^vk/{print $8}' | sort -u | wc -l   # 期望 125
# 与需求求差
comm -23 <(sort -u need.txt) <(readelf -sW --dyn-syms libvkpanvk_shim.so | awk '$4!="FILE" && $7!="UND" && $8~/^vk/{print $8}' | sort -u)   # 期望空
```

### 真机排障钩子
垫片在 stderr 打两类日志（进 `logcat` 即可见）：
- `[vkshim] icd=… gipa=… gdpa=…` —— 驱动 dlopen 与入口获取是否成功；
- `[vkshim] MISSING entrypoint: <name>` —— 我们的 ICD 没交出某个入口（最多 32 条），
  黑屏/闪退时这是**定位到具体函数**的最快线索。

---

## 13. ★★ 最终结论：顶替 `libvulkan*` 名字这条路**彻底封死**（实测两种垫片都卡死）

在 §12 的完整垫片（导出 125/125 个 loader 风格入口，双路符号自查通过）基础上，做了两组**对照实验**：

| 版本 | 插件 `lib/arm64-v8a/` 里放的 | 结果 |
|---|---|---|
| **v18** | `libMobileGL.so`（原件未改）+ **`libvulkan.so`** = 完整垫片 | 游戏**正常启动**（纹理图集都建好了）—— 但日志里是
`Mali-G720-Immortalis MC12, Vulkan 1.3.247, Driver 44.1.0` = **厂商 blob** ✗
（说明 MGL 试的第一顺位不是 `.so`，`.so` 那个名字**没被命中**）|
| **v19** | 同上，但 **`.so` 与 `.so.1` 双名**都放完整垫片 | 游戏**卡死在 `[DEBUG] Calling JLI_Launch`** ✗（与 §9② 的裸 ICD 事故**同一症状**）|

**⇒ 结论：只要插件目录里的 `libvulkan.so.1` 顶替了系统 loader，ZL2 的 JVM 就会卡死在启动路径上**
—— 与垫片是否"完整"**无关**（v14 是裸 ICD 卡死，v19 是 125 入口的完整垫片**同样**卡死）。
也就是说：**ZL2/LWJGL 在 JVM 启动早期就会解析/使用 Vulkan**，而我们的 `垫片 → PanVK` 链在那个时机**不能完成**
（最可能是 PanVK 在 app 域做 kbase 初始化的时机问题）。

顺带确认了一个**观测性**事实：Android 会**丢弃 app 的 stderr**，所以垫片里 `fprintf(stderr, "[vkshim] …")`
的排障钩子在真机上**看不到**（`logcat` 里 0 行）—— 要看得改成走 `__android_log_print`。

### 因此只剩最后一条路：**改 MobileGL 源码，让它直连我们的 ICD**

为什么这条路**反而更稳**：
- 不动任何系统库名字 → JVM 启动路径**完全不受影响**（v19 的卡死正是死在这）；
- 我们的驱动**只在 MGL 真正要建 Vulkan instance 时**才被加载（游戏跑到渲染器阶段，不是 JVM 启动阶段）；
- ICD 可直接当"loader"用：`dlopen(驱动)` → `vk_icdNegotiateLoaderICDInterfaceVersion(&7)` →
  `vk_icdGetInstanceProcAddr`，§3/§8 的探针已经证明这一步在这台设备上**能成功**。

具体改动（都在 `MobileGL/MG_Backend/DirectVulkan/Renderer/VulkanRenderer.cpp` 附近）：
1. 用 `dladdr()` 求出**插件自身目录**；
2. 优先 `dlopen("<自身目录>/libvulkan_freedreno.so")`，取 `vk_icdGetInstanceProcAddr`；
3. 把该文件里的 Vulkan 入口调用（`vkCreateInstance`/`vkGetInstanceProcAddr`/…）替换为从 ICD 取函数指针
   （§12 的生成器已经把 125 个函数**按参数类型分好类**，可直接复用它的分派逻辑）；
4. 编译环境是热的：`ninja -C build-android` 增量重建 + 重链约 **5 分钟**。

---

## 14. ★★ `DLOPEN` 键的正确用法与它的副作用（未解，留给后续）

### 根因（原文证据，来自子代理的设备实测）
把「完整垫片」按**唯一名**放进插件 lib 目录、再用 `patchelf --replace-needed` 把
`libMobileGL.so` 的 `DT_NEEDED` 指向它，**仍然失败**：

```
dlopen failed: library "libvkpanvk_shim.so" not found:
  needed by /data/app/~~…/com.dsh.plugin.driver.g720-…/lib/arm64/libMobileGL.so
  in namespace clns-9
```

⇒ MGL 是被 ZL2 用**绝对路径** dlopen 的，它所在的 classloader 命名空间（`clns-9`）
**搜索路径里没有插件 lib 目录**（绝对路径能开，**裸名不行**）⇒ 裸名 `DT_NEEDED` 必然失败。

### 已知的正确方向
ZL2 的 `RendererPluginManager` 支持 `pojavEnv` 里的特殊键 **`DLOPEN=<lib名>`**（逗号分隔）：
`RendererPlugin.getDlopenLibrary()` 会解析成 `"$nativeLibraryDir/$lib"`，
并在 dlopen 渲染器库**之前**调 `ZLBridge.dlopen(path, RTLD_GLOBAL|RTLD_LAZY)`。
子代理**在设备上用原生探针实测通过**这条链：
预加载垫片 → dlopen(patched libMobileGL.so) → `vkCreateInstance` → 垫片 → ICD
→ **`ICD device[0] name='Mali-G720 MC12' api=1.4.363`** ✔

同时它还实测否掉了一条路：**不写 `DT_NEEDED`、只靠 `RTLD_GLOBAL` 解析 UND 符号**会失败
（`cannot locate symbol "dep_value"`）⇒ 垫片仍必须被 `DT_NEEDED` 引用。

### ⚠️ 但把 `DLOPEN` 写进插件清单后，**ZL2 启动游戏会失败**（两次实测）
现象（v22 把 `DLOPEN` 同时写进 `pojavEnv` 和 `boatEnv`；v23 只写 `pojavEnv` —— **都一样**）：

```
Contents of org.lwjgl.librarypath:            ← 空
    liblwjgl.so: unknown type
    liblwjgl_opengl.so: unknown type
    …（LWJGL 自身的全部原生库都"认不出"）
Stacktrace:
    at com.mojang.blaze3d.platform.NativeLibrariesBootstrap.loadLibrary(…:200)
    at … NativeLibrariesBootstrap.loadLibraries(…:104)
    at net.minecraft.client.main.Main.main(…:142)
    at mio.Wrapper.main(Wrapper.java:21)
```

⇒ **`DLOPEN` 键不只做"预加载"**：它显然还影响了 LWJGL 原生库路径的构建/查找
（`org.lwjgl.librarypath` 变空、`liblwjgl*.so: unknown type`）。

### 下一步（明确的排查点）
去 ZL2 源码看 **`RendererPlugin.getDlopenLibrary()` 的调用点**：
- 若它只被 `ZLBridge.dlopen(path, RTLD_GLOBAL)` 消费 → 问题在别处（可能是它同时改写了 libraryPath）；
- 若它被用来**拼接 `libraryPath`** → 应把 **LWJGL 原生库目录 + 垫片**一起列出，或改用别的手段预加载
  （例如把垫片**改名为 MGL 恰好会找的名字**并放到 **LWJGL 能找到的目录**，或直接改 MGL 源码用
  `dladdr` + `vk_icdGetInstanceProcAddr` 内部直连 —— 见 §13）。

### 本轮已做的兜底
`mgl-panvk-v24.apk`（versionCode 24，内容 = 已知可跑的 v16 配置）已装回设备，
保证 ZL2 能正常启动 26.3（MobileGL Direct(Vulkan) 满帧，但 Vulkan 仍走厂商 blob）。

---

## 15. ★★ 读 ZL2 源码定案：`DLOPEN` 机制正确、无害；LWJGL 崩另有原因

直接读 `/root/zl2src/ZalithLauncher2-main` 的源码（原文如下，可复核）：

```kotlin
// game/plugin/renderer/RendererPluginManager.kt:110-125   —— pojavEnv 解析
pojavEnvString.split(":").forEach { envString ->
    if (envString.contains("=")) {
        val key = envString.split("=")[0]; val value = envString.split("=")[1]
        when (key) {
            "POJAV_RENDERER" -> rendererId = value
            "DLOPEN" -> value.split(",").forEach { lib -> dlopenList.add(lib) }   // 按逗号拆
            "LIB_MESA_NAME", "MESA_LIBRARY" -> envList[key] = "$nativeLibraryDir/$value"
            else -> envList[key] = value                                          // 其它键原样进 env
        }
    }
}

// game/plugin/renderer/RendererPlugin.kt:48                     —— v1 插件的 dlopen 列表
override fun getDlopenLibrary(): Lazy<List<String>> = lazy { dlopen.map { lib -> "$path/$lib" } }  // ★ 补成绝对路径
override fun getRendererLibrary(): String = "$path/$glName"

// game/launch/GameLauncher.kt:208-222                           —— 预加载时机
override fun dlopenEngine() {
    super.dlopenEngine()
    RendererPluginManager.selectedRendererPlugin?.let { renderer ->
        val libs by renderer.getDlopenLibrary()
        libs.forEach { libPath -> ZLBridge.dlopen(libPath) }      // ★ 先预加载（绝对路径）
    }
    val rendererLib = getRendererLibrary() ?: return
    if (!ZLBridge.dlopen(rendererLib) && !ZLBridge.dlopen(findInLdLibPath(rendererLib))) { … }  // 再 dlopen 渲染器
}

// game/launch/GameLauncher.kt:274-280                           —— 运行期库搜索路径
override fun getRuntimeLibraryPath(): String {
    val parent = super.getRuntimeLibraryPath()
    return jnaDir?.absolutePath?.let { dirPath -> "$parent:$dirPath" } ?: parent   // ★ 不含插件目录
}
```

**定案**：
1. `DLOPEN=<名字>` 的语义是**纯预加载**：ZL2 把它拼成 `"$nativeLibraryDir/<名字>"`（绝对路径），
   在 dlopen 渲染器之前调 `ZLBridge.dlopen(...)`。**它不修改任何库搜索路径**。
   ⇒ §14 里"v22/v23 的 LWJGL 崩是 `DLOPEN` 造成的"这个怀疑**不成立**（v23 只写 `pojavEnv` 也崩，但机制上它无害）。
2. **`getRuntimeLibraryPath()` 确实不含插件目录**（只有 `super + jnaDir`）。
   ⇒ 这正是 §14 那条 `library "libvkpanvk_shim.so" not found … in namespace clns-9` 的根源：
   **裸名 `DT_NEEDED` 不会被解析**，而 `DLOPEN` 预加载恰好能补上（预加载后同名 soname 已在命名空间里）。
   ⇒ 所以 **`DLOPEN=libvkpanvk_shim.so` 是正确且必要的做法**。

**那 LWJGL 为什么崩？** 观察到的原文是：
```
Contents of org.lwjgl.librarypath:            ← 空
    liblwjgl.so: unknown type  … libshaderc.so: unknown type
at com.mojang.blaze3d.platform.NativeLibrariesBootstrap.loadLibrary(...)
```
这更像**原生库目录没被正确设置/解压**（`org.lwjgl.librarypath` 为空、`java.library.path` 显示 `<not a directory>`），
而不是 `DLOPEN` 引起的。**下一步应查**：v22/v23 与可用版本（v16/v18/v24）之间**唯一的差异** ——
**被 `patchelf` 改过 `DT_NEEDED` 的 `libMobileGL.so`**。
最可能的情形：ZL2 在**预加载之前**（例如渲染器列表/设置页）就会尝试加载 `libMobileGL.so`，
此时 `libvkpanvk_shim.so` 尚未进入命名空间 ⇒ 加载失败并留下破状态。
**验证方法**：把 `DT_NEEDED` 改回 `libvulkan.so`（原件），只保留 `DLOPEN=libvkpanvk_shim.so` + 垫片；
若游戏能起来（Vulkan 仍走 blob，因为没人用垫片）⇒ 说明崩在"被 patch 的 MGL 被提前加载"。
或者干脆走 §13 的路线（**改 MGL 源码内部直连 ICD**，完全不产生新的 `DT_NEEDED`）。

---

## 16. 🎉🎉🎉 全线打通！MobileGL 真的跑在我们的 PanVK 上了（只剩 WSI 崩溃）

### 铁证（JVM 崩溃报告的原文）

```
#  SIGSEGV (0xb) at pc=0x000079b57af630, pid=11776, tid=11859
# Problematic frame:
# C  [libvulkan_freedreno.so+0xd7b630]  wsi_GetSwapchainImagesKHR+0x20
```

`libvulkan_freedreno.so` **就是我们自己编译的开源 Mali PanVK**。它在这个游戏进程里
**被加载 → 初始化 → 建 instance/device → 走到创建交换链**，才在 **`wsi_GetSwapchainImagesKHR`** 里段错误。

⇒ **整条链路成立**：
```
Minecraft 26.3
  → MobileGL（DirectVulkan / Magma 后端）
  → 垫片（导出 125 个 loader 风格入口）
  → 我们的 PanVK（libvulkan_freedreno.so）
  → /dev/mali0（kbase）
     ✗ 崩在 wsi_GetSwapchainImagesKHR（Android WSI / VK_KHR_swapchain 那条路）
```

### 关键机理：`RTLD_GLOBAL` 预加载就是"劫持点"
ZL2 在 `dlopenEngine()` 里先 `ZLBridge.dlopen("<nativeLibraryDir>/libvkpanvk_shim.so")`（**RTLD_GLOBAL**），
再 dlopen 渲染器。垫片被放进**全局符号组**后，它导出的 **`vkGetInstanceProcAddr`** 会在全局查询里**抢先命中**，
于是 MGL 在运行期通过 `vkGetInstanceProcAddr` 取到的入口**全部来自垫片** → 垫片转发给我们的 ICD。
⇒ 因此 **`DLOPEN=libvkpanvk_shim.so` 是正确且必要的一步**（§15 的源码分析与此完全一致）。

### 还差的一步（很小）
只剩 **PanVK 自己的 Android WSI 崩溃**：`wsi_GetSwapchainImagesKHR`。
可能的方向：
1. 试与 WSI 相关的 env（WSI 平台/后端选择）；
2. 给 PanVK 打一个 WSI 侧补丁（该函数附近的平台分支）；
3. 换一份 PanVK 构建（例如其它针对 G720 的构建）对比 —— 注意之前测过的另一份 G720 构建
   在 `eglInitialize` 路径就段错，说明 WSI 这块在不同构建间差异很大。

**判据**：游戏日志里
`OpenGL Renderer: Magma (MobileGL Core) (…)` 括号内出现 **`Mali-G720 MC12`**
（不再是 `Mali-G720-Immortalis MC12`）。

---

## 17. 🏆 皇冠证据：`Mali-G720 MC12` 真的出现在真机日志里 + 崩溃的最终根因

### 铁证（真机运行日志原文，垫片 + patchelf + DLOPEN 路线）
```
[vkshim] constructor: libvkpanvk_shim.so LOADED
[DEBUG] DLOPEN: .../lib/arm64/libvkpanvk_shim.so , success
[DEBUG] DLOPEN: .../lib/arm64/libMobileGL.so , success
[vkshim] dlopen OK    (dir) .../lib/arm64/libvulkan_freedreno.so
[vkshim] negotiate(...) -> 0, ver=7
[vkshim] ICD READY  gipa=0x79b53f7d0c gpdpa=0x79b57ac3b0
[vkshim] vkCreateInstance -> 0
WARNING: panvk is not a conformant Vulkan implementation, testing use only.
[vkshim] ICD device[0] name='Mali-G720 MC12' api=1.4.363 drv=26.2.99      ★★★ 我们的驱动
```
⇒ **MobileGL 的 Vulkan 后端确实建立在我们自编的开源 Mali PanVK 之上**（设备名/apiVersion/driverVersion 全是我们的）。

### 崩溃的最终根因：进程里**同时存在两套 Vulkan**
1. MGL 的**直连符号** → 垫片 → **我们的 ICD** ✓
2. ZL2 自己的 `load_vulkan()` 又 `dlopen("libvulkan.so")`（**系统 loader → 厂商 blob**），
   并用 `set_vulkan_ptr()` 把那份句柄交给 MGL ✗
   （日志佐证：`EGLBridge: LWJGL-side Vulkan loader requested the Vulkan handle`；
   JVM 参数里有 `-Dorg.lwjgl.vulkan.libname=libvulkan.so`）

⇒ **swapchain/surface 是 blob 那边建的**，却被塞给我们的 ICD ⇒
`wsi_GetSwapchainImagesKHR` 解引用**外来句柄**而 SIGSEGV：
```
# C  [libvulkan_freedreno.so+0xd7b630]  wsi_GetSwapchainImagesKHR+0x20
# C  [libMobileGL.so+0x8d5678] … C  [libSDL3.so+0x1cf004]  SDL_GL_CreateContext+0xc0
```
（读 Mesa 源码印证：该函数第 2 行就是 `VK_FROM_HANDLE(wsi_swapchain, swapchain, _swapchain);`，
`+0x20` 正是解引用最开始 ⇒ 传入的 `VkSwapchainKHR` 无效。）

### 三个必须知道的机制事实（本路线最值钱的产出）
**(a) 插件 lib 目录不在 launcher 命名空间搜索路径里** —— 日志把参数打全了：
```
WARNING: linker: ... not accessible for the namespace:
 [name="clns-9", ld_library_paths="",
  default_library_paths="<ZL2 自己的 lib/arm64>:<base.apk!/lib/arm64-v8a",
  permitted_paths="/data:/mnt/expand:/data/data/com.movtery.zalithlauncher.v2"]
```
`ld_library_paths=""` ⇒ **绝对路径可 dlopen，裸名 `DT_NEEDED` 永远找不到**。
（所以"只做 patchelf"必失败：`library "libvkpanvk_shim.so" not found … in namespace clns-9`。）
顺带：`/storage/emulated/0/...` 也不在该命名空间的 `permitted_paths` 里 ⇒ **任何把驱动/ICD 放 /sdcard 的方案都是死路**。

**(b) 正解 = `pojavEnv` 里的 `DLOPEN=<lib名>` 预加载键**（**只有 pojavEnv 被解析**，写 `boatEnv` 无效）：
ZL2 会把它变成 `"$nativeLibraryDir/$lib"`，并在 dlopen 渲染器库**之前**用
`ZLBridge.dlopen(path, RTLD_GLOBAL|RTLD_LAZY)` 预加载 ⇒ MGL 的 `DT_NEEDED` 命中已按 soname 加载的垫片。

**(c) 垫片必须带 `DT_SONAME=<同名>`**（`-Wl,-soname,libvkpanvk_shim.so`）。
否则即便 DLOPEN 成功，`DT_NEEDED` 仍会报 `not found` —— 这正是某一版（65080 B / sha `91c4053e`）失败的原因。
另经设备原生探针验证：**不要删 `DT_NEEDED` 只靠 `RTLD_GLOBAL` 全局组**，会 `cannot locate symbol`。

### 剩下的最后一步（方案已明确）
消掉"双栈"：让 `load_vulkan()` 也走垫片/我们的 ICD。可行顺序：
1. 把转发层**编进 MGL 本体** + `patchelf --remove-needed libvulkan.so`
   （MGL 是我们自己构建的，不需要重签 launcher）；
2. 或在能重签/自建 launcher 的前提下，把其原生代码里 `dlopen("libvulkan.so")` 改指向垫片；
3. 判别实验：写一个只加载我们 ICD 的 WSI 探针
   （instance → `vkCreateAndroidSurfaceKHR` → swapchain → `vkGetSwapchainImagesKHR`），
   以区分「外来句柄」与「PanVK WSI 在 Android 16 上的真 bug」。

### 本轮现场
设备已由协作者恢复为可用的 `versionCode 26 / 2.6-restore-good` 并熄屏；
红线三件套（网易云 / Stellar / DSH）全程存活。

---

## 18. 进展到"只剩最后一个 Vulkan 调用"：`vkCreateDevice` 的 -3 追踪全记录

### 18.1 已经确定的事实（按时间顺序，全部有原文证据）

| # | 事实 | 证据 |
|---|---|---|
| 1 | 我们的 PanVK 是标准 ICD，可加载可用 | 探针：`deviceName: Mali-G720 MC12` / `apiVersion 1.4.363` / 181 扩展 |
| 2 | 转发层（125 入口）已被 **静态链接进 MGL** | `readelf --dyn-syms`：UND vk* = **0** |
| 3 | 转发层**必须**带 `DT_SONAME`；插件 lib 目录**不在** `clns-9` 的搜索路径 | §17(a)(c) |
| 4 | 用 `pojavEnv: DLOPEN=<垫片>` 预加载是**正确且必要**的 | §17(b) |
| 5 | **MGL 完整跑在我们的驱动上** | MGL 日志：`Enabled optional device extension: VK_EXT_vertex_attribute_divisor`（早先走 blob 时它是 `missing` ✗）；logcat：`vendor.mesa.panvk.debug` 属性探测 |
| 6 | 转发层被 MGL 真正调用 | `/sdcard/MG/vkshim.log`：`vkCreateDevice: ext=11` + 11 个扩展名 + `features: 0x7b885f72b4` |
| 7 | 11 个扩展在 panvk 里**全部可用** | MGL 日志逐个列出（含 `VK_EXT_host_query_reset (r.1)`、`VK_EXT_subgroup_size_control (r.2)`）|

### 18.2 崩溃/错误的四次迁移（每次都前进一格）
```
libvulkan_freedreno.so wsi_GetSwapchainImagesKHR+0x20   ← 驱动 WSI（句柄域不一致）
        ↓ 修：补 vk_icdGetPhysicalDeviceProcAddr 分派
libMobileGL.so+0x961380（调用 NULL 函数指针）            ← MGL 内部
        ↓ 修：三源回退（gp_inst/gp_dev）+ 123 项 thunk 表（loader 语义）
Required extension found: VK_KHR_swapchain ✓             ← 扩展枚举正确了
vkCreateDevice → VK_ERROR_INITIALIZATION_FAILED (-3)     ← 当前：建设备被拒
```

### 18.3 当前这一环的根因假设（已定位到代码逻辑）

`vkCreateDevice` 的入参已拿到（11 扩展 + feature 位）。扩展全都可用 ⇒ 嫌疑集中在 **feature 位**：
MGL 日志自己写着 `robustBufferAccess=false geometryShader=false …`，
却仍按"支持"去建设备 ⇒ panvk 拒绝 ⇒ -3。

MGL 的"支持"从哪来？**`vkGetPhysicalDeviceFeatures` 这类物理设备级查询**。
而我们的 `gipa_pd()` 之前把 **instance GIPA 排在第一位**：
```
f = g_gipa(g_inst, name)      // ★ 错：对物理设备级函数会返回错的函数指针
if(!f) f = g_pdpa(pd, name)   //    正解在这里，却排在后面
```
这正是早先 `apiVersion=540.1018.2112`、`viewport limit=1852401253` 这些**垃圾值**的来源。

**修正（已实现并装进设备）**：
```c
static PFN_vkVoidFunction gipa_pd(VkPhysicalDevice pd, const char* n){
  if(g_pdpa) f = g_pdpa(pd, n);          /* ★ 物理设备级优先 */
  if(!f && g_gipa) f = g_gipa(g_inst, n);
  return f;
}
```
版本：`mgl-panvk-v39.apk` / `3.9-pdpa-first`，sha256 前缀 `1e4c231f`。

### 18.4 验证方法（一条命令）
```
# shim 的落盘日志（不受 logcat 环形缓冲影响）
cat /sdcard/MG/vkshim.log
# MGL 的日志：是否还出现 vkCreateDevice FATAL
tail -5 /sdcard/MG/latest.log
# 最终判据
grep -a "OpenGL Renderer" \
  "/storage/emulated/0/Android/data/com.movtery.zalithlauncher.v2/files/.minecraft/versions/26.3 Fabric/ZalithLauncher/latest_game.log"
#   期望：[OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363)]
```

### 18.5 若仍失败，下一个可做的动作
1. 用 shim 打印**完整的 `VkPhysicalDeviceFeatures` 逐位值**，与 panvk 的支持表对比，找出被虚假置位的位；
2. 或在 shim 里**对 `vkGetPhysicalDeviceFeatures` 加"仅走 pdpa"的强约束**并复测；
3. 或写一个独立小探针，用我们的 ICD 分别以"只带 feature 子集"的方式反复 `vkCreateDevice`，二分出被拒的 feature。

---

## 19. 🎯 根因链闭合（Mesa 源码级证据）

### 关键源码
```c
/* src/panfrost/vulkan/panvk_vX_device.c:376 —— 创建队列时 */
switch (create_info->queueFamilyIndex) {
case PANVK_QUEUE_FAMILY_GPU:  return panvk_per_arch(create_gpu_queue)(...);   /* ✓ */
case PANVK_QUEUE_FAMILY_BIND: return panvk_create_bind_queue(...);            /* ✓ */
default:                      return panvk_error(dev, VK_ERROR_INITIALIZATION_FAILED);  /* ← -3 */
}
```
（另一处：`panvk_physical_device.c:1408` 里 `VkResult result = VK_ERROR_INITIALIZATION_FAILED;`
是 arch 分派的**初值** —— 若 arch 分派没覆盖它，也会原样返回 -3。）

### 与真机日志交叉验证
MGL 早先的日志里明确写着：
```
[WARN] No graphics queue found on physical device. Picking a device that doesn't do graphics?
[FATAL] vkCreateDevice  →  VK_ERROR_INITIALIZATION_FAILED (-3)
```
⇒ **`-3` 的真身 = MGL 请求的队列族不是 GPU/BIND** ✓
⇒ 而"看不到图形队列"的根因，是 **`vkGetPhysicalDeviceQueueFamilyProperties`** 这类**物理设备级**查询
之前走了 instance GIPA，被返回了**错的函数指针**（早先 `apiVersion=540.1018.2112`、
`viewport limit=1852401253`、`timestampPeriod≈2.7e26` 这些垃圾值就是同一现象）。

### 因此修法链条是自洽的
| 修法 | 作用 |
|---|---|
| `gipa_pd()` **物理设备级优先**（v39）| 让 `vkGetPhysicalDeviceQueueFamilyProperties` / `…Features` / `…Properties` 拿到**正确**函数 ⇒ MGL 能看到图形队列与真实 feature |
| **123 项 thunk 表**（v36）| `vkGet*ProcAddr` 只返回自家 thunk ⇒ 句柄域统一、日志可观测 |
| **feature 屏蔽**（v41）| 建设备前 AND 掉驱动不支持的位 ⇒ 避免投机性提交被拒 |
| 日志双写 `/sdcard/MG/vkshim.log`（v38）| 不受 logcat 环形缓冲影响，能拿到 `vkCreateDevice` 入参 |

⇒ **v41 / 4.1-featmask（sha256 `acf64880`）已装设备**，从源码看三处都已对症；
剩下只差一次真机运行来读判据行。

### 若仍失败，按此定位
`/sdcard/MG/vkshim.log` 里 `feature mask applied, masked-out bits=0x…` 与 `feat[i] … xor=0x…`
会直接给出发散位；若 `xor` 全 0 且仍然 -3，则问题在**队列族**侧：
打印 `vkGetPhysicalDeviceQueueFamilyProperties` 的返回值（族数、每族的 queueFlags），
确认 `VK_QUEUE_GRAPHICS_BIT` 是否出现在返回值里。

---

## 20. 🎉 突破：`vkCreateDevice` 成功，错误推进到交换链（WSI / gralloc）

### 20.1 A/B 二分：现场日志直接指认转发层的 bug

在 shim 里打印"经 `gipa_pd` 解析的指针"与"直接问 instance GIPA 的指针"，一次运行即定论：

```
QFam-diag: fn=0x0  viaInstanceGIPA=0x79bad17d28  pdpa=0x79bad793b0  gipa=0x79ba9c4d0c  g_inst=0x7b6cf91680
           ↑ NULL ✗   ↑ 有效 ✓    ⇒ 结论：转发层取指针取空了，驱动没问题
QueueFamilyProperties: count=0      ← 因为 fn=NULL，转发器跳过调用，count 保持 0
```

**根因**：生成器把"首参含 `VkPhysicalDevice`"的函数误判成设备级（`VkPhysicalDevice` 里含子串 `VkDevice`），
于是它们走 `gp_inst(physicalDevice, …)` —— **把物理设备当成 instance 传给 ICD 的 GIPA**，查表必然落空。

修正（4 处）：
```c
/* 之前 */ PFN_vkX fn=(PFN_vkX)gp_inst(physicalDevice,"vkX");
/* 之后 */ PFN_vkX fn=(PFN_vkX)gipa_pd(physicalDevice,"vkX");   /* 先 pdpa，再退回【真实 instance】 */
```

### 20.2 修正后的实测结果（质的飞跃）

```
SurfaceCaps: min=2 max=4 cur=2376x1080 usage=0x17 alpha=0x9        ← surface 完全有效
QueueFamilyProperties: count=1
  family[0] flags=0x7 queues=2                                     ← GRAPHICS|COMPUTE|TRANSFER ✓
vkCreateDevice: feature mask applied, masked-out bits=0x00000000
  feat[0] want=0x1 sup=0x1 xor=0x0 …                               ← 请求位 == 支持位，零分歧
vkCreateDevice → 成功（MGL 日志中不再出现 vkCreateDevice FATAL）
```

### 20.3 当前唯一剩余问题：WSI 的 gralloc 转换

MGL 传的交换链参数**全部合法**（实测）：
```
CreateSwapchain: surf=0x7b8b3f9690 usage=0x13 fmt=37 cs=0 pm=1 alpha=0x1 layers=1 old=0x0
                 usage=0x13 ⊆ caps 0x17 ✓  fmt=37 ✓  pm=1(MAILBOX) ✓  图像数 3 ∈ [2,4] ✓
```
但仍返回 `VK_ERROR_INVALID_EXTERNAL_HANDLE (-1000072003)`。源码定位到**一行**：

```c
/* Mesa: src/vulkan/runtime/vk_android.c —— AHB → DRM format modifier 助手 */
struct u_gralloc *u_gralloc = vk_android_get_ugralloc();
if (u_gralloc_get_buffer_basic_info(u_gralloc, in_hnd, &info) != 0)
   return VK_ERROR_INVALID_EXTERNAL_HANDLE;          /* ← 就是这里 */
```

**与项目开头的老结论闭环**：本机 `/dev/dri/*` 全部 `EACCES`（DRM 路线不可用），
而 Mesa `u_gralloc` 的默认后端正是走 `/dev/dri` 的 "dri" 后端 ⇒ 必然失败 ⇒ 该返回值与实测数字完全一致。

**修法方向**：让 panvk 使用 Mesa 的 **Android gralloc 后端**（`u_gralloc_android`：经 `libgrallocmapper`/gralloc AIDL，不依赖 `/dev/dri`），
即检查我们这份 panvk 构建是否编入了该后端；若未编入则打开对应 meson 选项重编驱动，重打插件后再测。

### 20.4 本轮新增的落盘诊断（都在 `/sdcard/MG/vkshim.log`，不受 logcat 环形缓冲影响）
| 版本 | 新增诊断 |
|---|---|
| v38 | `FLOG` 双写 logcat + `/sdcard/MG/vkshim.log` |
| v40 | `feat[i] want/sup/xor`（6 字 = 192 个 feature 位逐位对比） |
| v41 | 建 device 前**屏蔽驱动不支持的 feature 位**并打印 `masked-out bits` |
| v42 | `QueueFamilyProperties: count / family[i] flags,queues` |
| v43 | `QFam-diag: fn / viaInstanceGIPA / pdpa / gipa / g_inst`（A/B 二分用） |
| v46 | `SurfaceCaps: …` 与 `CreateSwapchain: surf/usage/fmt/pm/alpha/layers/old` |

### 20.5 错误点迁移史（每一步都是净进步）
```
① libvulkan_freedreno.so wsi_GetSwapchainImagesKHR+0x20   驱动 WSI 段错
② libMobileGL.so+0x961380                                  MGL 内 NULL 函数调用
③ vkCreateDevice = -3                                      队列族 count=0（转发层取指针失败）
④ vkCreateDevice 成功、队列族 flags=0x7、feature xor 全 0   ✓✓
⑤ 交换链：VK_ERROR_INVALID_EXTERNAL_HANDLE                 u_gralloc(/dev/dri) 转换失败 ← 当前
```

---

## 21. WSI 唯一剩余问题：Mesa `u_gralloc` 后端（路线与判定）

### 21.1 实测证据（v46 运行）
MGL 传的交换链参数全部合法，仍返回 `VK_ERROR_INVALID_EXTERNAL_HANDLE`：
```
SurfaceCaps: min=2 max=4 cur=2376x1080 usage=0x17 alpha=0x9
CreateSwapchain: surf=0x7b8b3f9690 usage=0x13 fmt=37 cs=0 pm=1 alpha=0x1 layers=1 old=0x0
```
源码定位：`src/vulkan/runtime/vk_android.c`
```c
if (u_gralloc_get_buffer_basic_info(u_gralloc, in_hnd, &info) != 0)
   return VK_ERROR_INVALID_EXTERNAL_HANDLE;
```

### 21.2 为什么我们的 panvk 会失败（源码级判定）
`u_gralloc.c` 的后端尝试顺序：
```
CROS → [GRALLOC4 若编入] → LIBDRM → QCOM → FALLBACK
```
- 我们**没有**编入 GRALLOC4（构建时 `dependency('android.hardware.graphics.mapper')` 未找到）：
  `strings libvulkan_panfrost.so` 里 `android.hardware.graphics.mapper` 计数为 **0** ✓
- `LIBDRM` 后端**必然失败**：`u_gralloc_libdrm_create()` 要求 `hw_get_module("gralloc")`
  返回的模块名严格等于 **"gbm"**，本机 `gralloc.default.so` 不是 gbm 模块 ⇒ `goto fail` ✓
- `QCOM` 后端是高通专用 ⇒ 失败 ✓
- ⇒ 实际落到 **FALLBACK**，它靠 `hw_get_module(GRALLOC_HARDWARE_MODULE_ID)`
  拿到 `/vendor/lib64/hw/gralloc.default.so` —— 但 Android 16 上它只是**空壳**，
  `get_buffer_basic_info` 返回非 0 ⇒ 报 `VK_ERROR_INVALID_EXTERNAL_HANDLE` ✓
  **⇒ 路线 A（换运行时后端）被排除；必须把 GRALLOC4(imapper4) 后端编进去。**

### 21.3 路线 B 的准确技术要求（已摸清）
`u_gralloc_imapper4_api.cpp` 依赖：
```
aidl/android/hardware/graphics/common/{BufferUsage,ChromaSiting,Dataspace,ExtendableType,
     PlaneLayoutComponent,PlaneLayoutComponentType}.h     ← AOSP hardware/interfaces（源码可拉）
gralloctypes/Gralloc4.h                                   ← AOSP system/core/libgralloctypes
system/window.h                                           ← NDK 自带 ✓
android::hardware::graphics::mapper::V4_0::IMapper        ← 经 Gralloc4.h 传递的 HIDL 4.0 头
```
Meson 探测方式（**可用手写 pkg-config 满足**）：
```meson
dep_android_mapper4 = dependency('android.hardware.graphics.mapper', version:'>= 4.0', required:false)
```
设备侧已具备运行时库（实测存在）：
```
/vendor/lib64/hw/gralloc.default.so
android.hardware.graphics.mapper@2.0/2.1/3.0/4.0.so
libgralloctypes.so · libgralloctypes_mtk.so · libgralloc_extra.so · libgralloc_metadata.so
ro.hardware = mt6989
```
⇒ 步骤：① 拉 AOSP 头（libgralloctypes + mapper 4.0 HIDL + graphics/common aidl）
② 写 `android.hardware.graphics.mapper.pc` 指向头目录与（链接用的）设备库/stub
③ 重 configure Mesa 构建目录 ⇒ `USE_IMAPPER4_METADATA_API` 自动打开
④ 重编 `libvulkan_panfrost.so` → 重打插件 APK → 装机实测

### 21.4 风险与备选
- HIDL 4.0 头通常是 `hidl-gen` **生成**的，AOSP 源码树里未必直接存在；
  若拉不到，备选是改用 `u_gralloc_imapper5_api.cpp`（AIDL 路线，头文件都是源码形式），
  它由 `dep_android_ui = dependency('ui', …)` 触发。
- 两条路都不通时，最后的兜底是：在 **shim 层**绕过 Mesa 的 WSI —— 自行实现
  `vkCreateSwapchainKHR`/`vkGetSwapchainImagesKHR`，用 `/dev/mali0` + gralloc 自己建交换链
  （工作量大，但完全可控）。

---

## 22. 🏆🏆 目标达成：Minecraft 运行在「MobileGL DirectVulkan + 自编 Mesa PanVK」之上

### 判据行（游戏日志原文）
```
[10:46:13] [Render thread/INFO]: OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)
```
- `Mali-G720 MC12` ⇒ **我们的驱动**（厂商 blob 会显示 `Mali-G720-Immortalis MC12`）
- `Vulkan 1.4.363` ⇒ **我们的 API 版本**（厂商 blob 是 `1.3.247`）
- `Driver 26.2.99` ⇒ **我们的 Mesa 版本**

### 达成的版本与关键开关
- 插件 APK：`mgl-panvk-v47`（versionCode 47，sha256 前缀 `bb689838`）
  = `panel v46` 的载荷 + `pojavEnv` 增加 **`MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`**
- 该开关作用（Mesa `wsi_common.c`）：把**任意 surface（含 android）**换成 Mesa 的 headless 交换链，
  其 `queue_present` 是**空操作返回 `VK_SUCCESS`** ⇒ 绕开了"Android WSI 转 DRM 描述"这一环。
- 完整 `pojavEnv`：
  `LIBGL_ES=3:POJAV_RENDERER=opengles3:MOBILEGL_BACKEND_TYPE=DirectVulkan:MOBILEGL_ESPRYT_USE_ANGLE=0:MOBILEGL_MAGMA_R11G11B10F_FALLBACK=0:MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`

### 同一次运行的其它证据（`/sdcard/MG/vkshim.log`）
```
SurfaceCaps: min=2 max=4 cur=2376x1080 usage=0x17 alpha=0x9    ← surface 有效
QueueFamilyProperties: count=1 / family[0] flags=0x7 queues=2   ← 图形队列正常（我们的分派修正生效）
CreateSwapchain: surf=0x79c5fff910 usage=0x13 fmt=37 cs=0 pm=1 …   ← 第一次（MAILBOX）
CreateSwapchain: surf=0x79c5fff910 usage=0x13 fmt=37 cs=0 pm=0 …   ← 重试（FIFO）
```

### ⚠️ 尚存的下一关（如实记录）
MGL 随后在**纹理上传**阶段报 `VK_ERROR_DEVICE_LOST (-4)`：
```
[10:46:15] FATAL: Vulkan error VK_ERROR_DEVICE_LOST (-4) at VkTextureManager.cpp
[10:46:15] FATAL: vkQueueSubmit(texture upload batch)
[10:46:15] ERROR: WaitForSubmitIndex: vkWaitForFences returned -4
```
⇒ 判据行已拿到（渲染器链路成立）✓，但**这一版还不能稳定游玩**。
下一步候选：
1. 查 kbase 侧的 fault（`dmesg`/logcat 的 mali/kbase 记录）；
2. 06 号研究给出的 R4 杠杆：让上层申请 buffer 时带 **ARM gralloc 的 `no_afbc_usage` 位**
   （`ro.vendor.arm.gralloc.*` 存在）⇒ 很可能拿到 **LINEAR** ⇒ 无需 hack；
3. 05 号方案 A：`vk_android.c` 的「AHB 自描述回退」（约 90 行、重编 30–60 秒）；
4. 01 号突破：`u_gralloc_imapper5_api.cpp` **已能在不建 AOSP 的前提下编出**（复用 VNDK 树，
   产出 `libtest_imapper5.so`，导出 `u_gralloc_imapper_api_create`）⇒ 拿**真实 modifier** 的正路。

### 这条链的完整演进（每一步都有原文证据）
```
驱动加载不了 → 探针证明 PanVK 可用(Mali-G720 MC12/1.4.363/181 扩展)
→ 4 个编译坑 → MGL 跑通 DirectVulkan(59-60fps, 但走厂商 blob)
→ 铁证：Android loader 忽略 VK_ICD_FILENAMES
→ 自建 125 入口转发垫片 + 静态链入 MGL(UND vk*=0)
→ loader 语义修正(123 项 thunk 表, vkGet*ProcAddr 返回自家 thunk)
→ 三处分派修正 + 4 处"物理设备当 instance"修正 ⇒ 队列族 flags=0x7、feature xor 全 0
→ vkCreateDevice 成功
→ WSI 关卡：Mesa u_gralloc 转 DRM 描述失败(/dev/dma_heap/system 0444 ⇒ kbase dmabuf 关闭)
→ 用 MESA_VK_WSI_HEADLESS_SWAPCHAIN=1 绕开 ⇒ ★ 判据行出现：Mali-G720 MC12, Vulkan 1.4.363
→ 下一关：纹理上传阶段 VK_ERROR_DEVICE_LOST
```

---

## 23. v48/v49：headless 绕路之后 —— 诊断加深与 dma_heap 根因链复核

> 承接 §22：判据行已达成（v47），但 MGL 在**纹理上传**阶段报 `VK_ERROR_DEVICE_LOST (-4)`。
> 本节记录 v48/v49 两个**诊断版本**的运行结论，并给出本轮对 `/dev/dma_heap/*` 根因链的
> **源码级复核**——复核结果**推翻了 §21.2/`research/06` 沿用的一处说法**（见 23.4，这一条很重要）。

### 23.1 v48（`4.8-diag-deep`）：把 `-4` 的首次出现钉在「纹理上传的 `vkQueueSubmit`」

| 项 | 值 |
|---|---|
| APK | `mgl-panvk-v48.apk` |
| versionCode / versionName | `48` / `4.8-diag-deep` |
| 大小 / sha256 | 10,183,215 B / `e511f980b8a63d53169fb3d69e92a5a49151498397adfa3e75359a5f377fbab4` |
| mtime | 10:50 |
| 与 v47 的唯一自变量差异 | 追加诊断环境变量：`MESA_DEBUG=1`、`PANVK_DEBUG=1`、`LIBGL_DEBUG=1`、`EGL_LOG_LEVEL=debug`、`MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log` |

**运行结论**：诊断面铺满（Mesa / PanVK / MobileGL / libGL / EGL 五路）之后，
`VK_ERROR_DEVICE_LOST (-4)` 的**首次出现**被钉在 MGL 的**纹理上传批次**这一次提交上
（`VkTextureManager.cpp`）：

```
FATAL: Vulkan error VK_ERROR_DEVICE_LOST (-4) at VkTextureManager.cpp
FATAL: vkQueueSubmit(texture upload batch)
ERROR: WaitForSubmitIndex: vkWaitForFences returned -4
```

与 v47 相比：**错误码不变（-4）、位置不变**；v48 的价值在于把日志通道与基线固定下来
（`/sdcard/MG/mgl.log`），供后续探针取证使用。注意：**这一段链不创建 AHB、不经过 `u_gralloc`、也不经过任何 WSI 代码**
（`research/11` §0.13）⇒ 它与"交换链建不起来"是**两个独立病灶**。

### 23.2 v49（`4.9-nodmaheap`）：关掉 dma-heap 的反向对照 —— **无效**

| 项 | 值 |
|---|---|
| APK | `mgl-panvk-v49.apk` |
| versionCode / versionName | `49` / `4.9-nodmaheap` |
| 大小 / sha256 | 10,183,215 B / `f1389427f687a0cd7b945b7ad733a446d2f7236fa01c7fd6025b97abfdbecd53` |
| mtime | 10:55 |
| 与 v48 的唯一自变量差异 | 追加 `PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent` |

**实验目的**：`kbase_kmod.c` 从环境变量 `PANVK_KBASE_DMA_HEAP`（缺省 `/dev/dma_heap/system`）取堆节点路径；
把它指到不存在的路径 ⇒ `open()` 失败 ⇒ `dma_heap_fd = -1` ⇒ `kbase_kmod_supports_dmabuf()` 返回 false
⇒ panvk 退回到 **kbase 原生分配**（不走 dma-buf/dma-heap）。这是针对 §21.2 那条"0444 根因"的**反向对照**。

**运行结论**：**错误码不变** —— `-4` 依旧出现在纹理上传的 `vkQueueSubmit`，症状与 v48 完全一致。
⇒ dma-heap / dma-buf 这条子路径**不是该 `-4` 的直接成因**；把它的开关扳到另一端，
GPU 掉线**照旧发生**。这一条把 §22 遗留的"`-4` 是不是 dmabuf 打不开造成的"排除掉了。

### 23.3 源码事实：`O_RDONLY` 打开的 dma-heap fd 被用于 `DMA_HEAP_IOCTL_ALLOC`

本轮在**构建树**（`/root/zenithblue/work/mesa`，即 `build/android-v4` 的来源树，`research/11` §1）
上逐行核对，得到两条**可复核**的源码事实：

```
kbase_kmod.c:1285   kbase_dev->dma_heap_fd = open(dma_heap, O_RDONLY | O_CLOEXEC);
kbase_kmod.c:1569   if (ioctl(kbase_dev->dma_heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc)) {
kbase_kmod.c:1570      mesa_loge("kbase: DMA_HEAP_IOCTL_ALLOC failed: %s", strerror(errno));
                    }
```

即：**用只读方式打开的 fd，去做一个（内核侧要求可写的）分配 ioctl**。
本机对应的设备事实（`research/06` §一实测；本条为同源事实）：

| 节点 | 权限 | 含义 |
|---|---|---|
| `/dev/dma_heap/*` | **全部 0444**（`research/06` 记录 19 个节点；本任务记录为 20 个，节点数差异未复核） | `O_RDONLY` 可开、`O_RDWR` 不可开 |
| `/dev/mali0` | **0666**（`crw-rw-rw-` system:graphics） | kbase 本体可用，与任务前提一致 |

⇒ 按本轮（v48/v49 期间）的源码分析：`O_RDONLY` 能打开成功，但随后的 `DMA_HEAP_IOCTL_ALLOC`
**必然失败**（fd 无写权限）⇒ dma-heap 这条分配路径在本机**不可用**。

> **口径（不美化）**：上面这句"必然失败"是**源码推断**，本轮**未在设备侧取到独立证据**：
> `research/12` 的探针日志里**没有** `DMA_HEAP_IOCTL_ALLOC` 的尝试记录、也**没有**它的失败行；
> 日志里出现的 `kbase_kmod_bo_alloc_dmabuf: succeeded for …` 实为
> `kbase_kmod_import_dmabuf()`（`kbase_kmod.c:1439–1556`，`mesa_logi` 在 :1536）打印的
> **导入**成功行（来自 AHB 外部导入），**不能**用来证明 dma-heap 分配成功。
> 另需注意 `research/06` 当时给出的注释是**相反**的推断（"dma-heap 的 ALLOC ioctl 不要求写权限"，
> 并把"`O_RDONLY` 下 ALLOC 是否成功"列为**待验证假设 (i)**）。两条推断**至少有一条是错的**，
> 真正的判定需要一条 ~30 行的设备侧探针（`research/06` R5 已给出写法）。**此处标为「未验证」。**

### 23.4 ★ 更正：`O_RDWR` 那句话来自**另一棵树**，对出厂件不成立

仓库此前（§21.2、`README` N5、`CHANGELOG` M9、`research/06`、`summaries/06`）统一写着：

> `/dev/dma_heap/system` 0444 ⇒ `kbase_kmod.c` 用 **`O_RDWR`** 打开失败 ⇒
> `kbase_kmod_supports_dmabuf()=false` ⇒ panvk 落 `sw_device=true`

本轮核对发现：**存在两份 `kbase_kmod.c`，`O_` 标志不同**，此前混引了它们：

| 文件 | 大小 | md5 | `dma_heap` 打开方式 | 角色 |
|---|---|---|---|---|
| `/root/mesa/src/panfrost/lib/kmod/kbase_kmod.c` | 68,849 | `0f4d40caf42728e8670f16fb60beeb3a` | **`O_RDWR`**（:1269） | **旧树**（`research/06` 读的就是它） |
| `/root/zenithblue/work/mesa/…/kbase_kmod.c` | 72,941 | `e2e92db65be6b8f8fd87b9c7c8927c39` | **`O_RDONLY`**（:1285） | **构建树**（`build/android-v4` 的来源） |

构建树这份与 `patches/kbase-common/files/…` 中的副本**逐字节相同**（md5 均为 `e2e92db6…`），
且其 `mtime`（2026-10-04 22:58:22）**早于**产物 `libvulkan_panfrost.so` 的 `mtime`（2026-10-05 01:51）
⇒ 出厂件（md5 `4417b369…`）**编自 `O_RDONLY` 那一份**。

**推论（源码级，实机未复核）**：`O_RDONLY` 对 0444 节点**能打开成功** ⇒ `dma_heap_fd >= 0` ⇒
`kbase_kmod_supports_dmabuf()`（:157–166，函数体就是 `return dma_heap_fd >= 0;`）返回 **true**。
因此 §21.2/N5/M9 那条"`supports_dmabuf()=false` ⇒ `sw_device=true`"**对出厂件不成立**——
失败被推迟到真正做分配时的 `DMA_HEAP_IOCTL_ALLOC`（见 23.3）。

这也正好解释了 23.2：**v49 关掉 dma-heap 之后错误码不变**，因为无论开关在哪一端，
`-4` 都发生在与 dma-heap 无关的**绘制/提交**一侧（详见 §25）。

> **待办（新）**：用 `research/06` R5 的探针在真机上判定 ①`O_RDONLY` 下 `DMA_HEAP_IOCTL_ALLOC` 是否成功、
> ②`kbase_kmod_supports_dmabuf()` 实际返回值。这两条**尚未验证**，但在 §25 之后**优先级已下降**
> （WSI/dma-heap 不再是首要瓶颈）。

---

## 24. v50：WSI 补丁（05 方案 A + MR !43659 式 LINEAR 推断）

> 实施细节与完整 diff 见 `research/11`（350 行）。本节记录**改动范围、产物哈希与构建目录判定**，
> 以及**未验证项**（v50 未上机）。源码树：`/root/zenithblue/work/mesa`（**未做任何 git 回退/清理**；
> 该树有 45 个未提交改动 ⇒ 回滚只能靠 `cp` 备份，**禁止 `git checkout --`**）。

### 24.1 三个文件，全部**严格加性**

| # | 文件 | 改动 | 性质 |
|---|---|---|---|
| ①a | `src/util/u_gralloc/u_gralloc_fallback.c` | `fallback_gralloc_get_yuv_info()` 里 `!gr_mod || !gr_mod->lock_ycbcr` 的返回值 **`-EINVAL` → `-EAGAIN`** | 一行修复（`research/09` §1.1）：调用方用 `-EAGAIN` 表示"其实不是 YUV，请继续走 fourcc 分支"，`-EINVAL` 会被原样上抛成 `VK_ERROR_INVALID_EXTERNAL_HANDLE` |
| ①b | 同文件 | 新增 `panvk_infer_linear_modifier()` —— **MR !43659 式 LINEAR 推断** | 判定条件：`modifier == DRM_FORMAT_MOD_INVALID` + `num_planes==1` + `offsets[0]==0` + `strides[0]>0` + `fstat(data[0]).st_size % strides[0] == 0` + `implied_h ∈ (0, 65536]` ⇒ `DRM_FORMAT_MOD_LINEAR` |
| ② | `src/vulkan/runtime/vk_android.c` | **AHB 自描述回退**（约 150 行 + 110 行注释）：新增 `vk_android_ahb_format_to_drm()` / `vk_android_ahb_probe_row_pitch()` / `vk_android_ahb_layout_from_desc()` | **严格加性**：仅在 `u_gralloc_get_buffer_basic_info()` **已经失败**之后才可达；`result == VK_SUCCESS` 时与改动前逐指令等价 |
| ②b | `src/android_stub/nativewindow_stub.cpp` | 补 `AHardwareBuffer_lockPlanes` 桩（返回 `-EINVAL`） | 链接命令带 `-Wl,--no-undefined`，不补桩会链接失败；`readelf -sW --dyn-syms` 确认它仍是 **UND** ⇒ 设备上解析到**系统真实库**（桩不会被用到） |

**行为级开关**（不重编、不换包，只改 `pojavEnv`）：`PANVK_GRALLOC_NO_FALLBACK=1`（关 ②）、
`PANVK_GRALLOC_NO_INFER_LINEAR=1`（关 ①b）、`PANVK_GRALLOC_ANY_USAGE=1`（放宽 ② 的 CPU-usage 门，默认关）。

**一个容易误判的点（`research/11` §2.2）**：①a 对**本机 WSI 自己的 AHB 不生效**——
`panvk_wsi.c:370` 用的是 `format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM`（=1），
而 `is_hal_format_yuv(1) == false`（`droid_yuv_formats[]` 只含 `0x22/0x23/YV12`）⇒ 根本不走 ①a 那条分支。
真正拦死交换链的是 V19 那句"元数据不可用就 `return stable_ret`"的硬失败 ⇒ **让交换链通过的是 ①b**。

### 24.2 产物与哈希（服务器实测，本节数字均已复核）

| 文件 | size (B) | md5 | sha256 |
|---|---|---|---|
| 新 `libvulkan_panfrost.so`（build/android-v4 与 dist 两处一致） | 20,005,320 | `e08e07645c16d8ebaa11ca70a09884fd` | `a0b2451ee15a17bf25b91e195e0b59f4ad93732d7fa771afb6a2ac7be3853f2d` |
| 旧件（`.bak-1791169250`，与出厂件逐字节相同） | 20,003,136 | `4417b369591fc2b3df27e22019ccf3a2` | — |
| `mgl-panvk-v50.apk` | 10,187,311 | — | `677d81eb29c7c57938573dfd9de20d39ff7f0557d466d3ed7ade9510033fc4b9` |

- `SONAME` / `NEEDED`（7 个）与旧件**逐条一致**，未变坏；`RUNPATH=$ORIGIN/../../android_stub` 不变。
- APK 载荷校验：`unzip -p` 取出的 `lib/arm64-v8a/libvulkan_freedreno.so` 的 sha256 与新 `.so` **逐位相同**。
- 两次**独立增量编译**均 `exit=0`（第一次只有 ①a+①b，第二次 ②+桩），无 warning/error。
- 新增字符串确认三个改动都进了二进制（`[PANVK-LINEAR-INFER] …`、
  `u_gralloc cannot describe AHB …; using self-described LINEAR layout` 等）。

### 24.3 与 v49 的**唯一自变量差异** = 删掉 `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`

v50 = v49 的载荷（`libMobileGL.so` + `classes.dex`）+ **新 `.so`**；`pojavEnv` **只**去掉了
`MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`（`grep -c HEADLESS` 实测 **0**），
并保留了 v49 已验证的 `PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent`、`MESA_DEBUG=1`、`PANVK_DEBUG=1`，
另加两个 `=0` 的显式开关。完整 `pojavEnv`（`aapt2 dump badging` 原文）：

```
LIBGL_ES=3:POJAV_RENDERER=opengles3:MOBILEGL_BACKEND_TYPE=DirectVulkan:
MOBILEGL_ESPRYT_USE_ANGLE=0:MOBILEGL_MAGMA_R11G11B10F_FALLBACK=0:
PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent:MESA_DEBUG=1:PANVK_DEBUG=1:
PANVK_GRALLOC_NO_FALLBACK=0:PANVK_GRALLOC_NO_INFER_LINEAR=0:
MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log
```

目的：让本次真机实验**只有一个自变量**（走真 Android 交换链 vs headless 交换链）。
包名/签名/`versionCode` 与 v46–v49 同构（`com.dsh.plugin.driver.g720`，同 keystore）⇒ 可直接覆盖安装。

### 24.4 ★ 构建目录实测判定 —— **闭合 README/CHANGELOG 的 U4**

`research/05` 与 `research/06` 曾对"出厂 `.so` 出自哪个 build 目录/哪套 platforms"给出**矛盾结论**。
本轮以 md5 + `strings` 实测判定：

```
$ find /root/zenithblue/build -name libvulkan_panfrost.so
/root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so     ← 唯一产物
$ ls /root/zenithblue/build/
android-bionic  android-v2  android-v3  android-v4  host-tools                  ← 4 个 build 目录，只有 v4 有产物
```

| 检查 | 实测结果 |
|---|---|
| `strings <dist .so> \| grep -c wsi_x11` | **0** |
| `grep -c 'wsi_common_x11\|wsi_x11' build/android-v4/build.ninja` | **0** |
| meson `intro-buildoptions.json` → `platforms` | `['android']` |
| 同上 → `vulkan-drivers` / `gallium-drivers` | `['panfrost']` / `[]` |
| 同上 → `android-stub` | `True` |

⇒ **`research/05` 正确、`research/06` 的"出厂件含 x11 WSI"不成立**；
`research/06` 的误判来源已定位：编译命令里有 `-I/root/zenithblue/work/android-deps-x11/include`，
那只是**依赖 include 目录的名字**，不代表编了 x11 WSI。

⇒ **结论（闭合 U4）**：要改、要编的目录就是 **`/root/zenithblue/build/android-v4`**，
必须在其中**增量**编译。（顺带记录一个坑：直接 `ninja` 会以
`aarch64-linux-android35-clang: not found` 失败，需先把
`/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin` 加进 `PATH`。）

### 24.5 未验证项（如实）

1. **v50 从未在真机上运行**（任务禁止碰手机）⇒ 交换链补丁只有**静态/链接层**验证，
   **没有运行时证据**。首跑必须看 `/sdcard/MG/mgl.log` 里是否出现
   `AHB layout fallback: … -> DRM_FORMAT_MOD_LINEAR` 或 `[PANVK-LINEAR-INFER] …`。
2. **`VK_ERROR_DEVICE_LOST (-4)` 不能声称已消除**（`research/11` §0.13）：
   它出现在 **headless 交换链**配置下、于 MGL 纹理上传的 `vkQueueSubmit` 处，
   **那条路径不创建 AHB、不经过 `u_gralloc`、也不经过本补丁**。**不要把 v50 的成败直接等同于本补丁的成败。**
3. LINEAR 推断只在"usage 声明了 CPU 可读/写"时成立（AFBC/UBWC 不可 CPU 映射 ⇒ 声明可映射即证明线性），
   否则 fail-closed 拒猜；`desc.stride` 的单位假设为**像素**（与 `panvk_wsi.c:280-290` 既有用法一致）。
4. `panvk_v19_query_mapper()` 仍优先于推断（有真实 modifier 时用真实的）⇒ 同一份 `.so`
   在不同设备上可能走不同分支，排查必须靠日志区分。

---

## 25. ★★ 真机探针 8 模式：原假设不能复现，真正掉线的是「绘制」

> 素材：`research/12`（327 行）。设备：OPPO PHZ110 / MT6989 / Immortalis-G720 MC12 / Android 16 (SDK 36) /
> 无 root（执行走特权 shell `uid=2000`，探针落在 `/data/local/tmp/`）。
> **重要前提**：探针加载的驱动是**补丁前**的出厂件 `md5 = 4417b369…`（size 20,003,136）
> ——即 v47 那一代的 `.so`，**不是** §24 的新件 ⇒ 本节结论**独立于 v50 补丁**。

| 项 | 值 |
|---|---|
| 探针源码/二进制 | `panvk_wsi_probe.c`（68,530 B）/ `panvk_wsi_probe`（277,800 B，**md5 `f735e1f4d03c40f248e76e02565f51e6`**） |
| 修补前的原件 | 274,064 B（`research/12` §1 记录了探针自身 A/B/C 三个"一跑就退"的 bug，已修并重编） |
| 驱动 .so | md5 `4417b369591fc2b3df27e22019ccf3a2`（与设备侧校验一致后才执行） |
| logcat 判据 tag | `MESA`（另有探针自定义 tag `P10PROBE`） |
| ⚠️ 操作告诫 | **`--mode=all` 不可用**：`tri` 的 CSF 掉线会**污染同进程后续所有步骤**（`tri` 之后连 `render` 的 `vkQueueSubmit` 都变成 `-4`）⇒ **必须一模式一进程** |

### 25.1 逐步 VkResult 总表

`-1000072003` = `VK_ERROR_INVALID_EXTERNAL_HANDLE`；`-4` = `VK_ERROR_DEVICE_LOST`。

| 模式 | 结果 | 关键 VkResult 链 |
|---|---|---|
| **render** | ✅ **PASS** `failures=0` | createImage 0 → AllocateMemory 0 → BindImageMemory 0 → **Submit 0 → WaitForFences 0** → 三点像素精确 |
| **tri** | ❌ **FAIL** `failures=1` | 整条管线全 0 → Submit 0 → **WaitForFences −4 DEVICE_LOST** |
| **ahb**（fmt=0x1） | ✅ **PASS** `failures=0` | AHB_allocate 0 → createImage 0 → **vkGetAHBProps 0**（allocSize=16384 externalFormat=37）→ **AllocateMemory 0** → Bind 0 → Submit 0 → Fences 0 → 三点像素精确 |
| **ahbimpdef**（fmt=0x22） | ❌ **FAIL** `failures=1`（**可复现 ×2**） | AHB_allocate 0 → createImage 0 → **vkGetAHBProps −1000072003**（allocSize=0 format=UNDEFINED） |
| **mapper** | ✅ PASS（无 Vulkan） | SPHAL 取到 handle → `AIMapper_loadIMapper rc=0 version=5` → `importBuffer rc=0` → 5 类 metadata 全部返回正值 |
| **headless** | ✅ **PASS** `failures=0` | HeadlessSurface 0 → support 0(supported=1) → caps 0 → formats 4 → presentModes 2 → **CreateSwapchainKHR 0** → images 4 → Acquire 0 → Present 0 |
| **win**（fmt=0x1） | ✅ **PASS** `failures=0` | AndroidSurface 0 → caps 0 → **CreateSwapchainKHR 0** → images 2 → Acquire 0 → Submit 0 → Present 0 → acquireNextImage 0 → **窗口像素精确** |
| **winimpdef**（reader fmt=0x22） | ✅ PASS（swapchain 段） | ANativeWindow format=34(0x22) → **CreateSwapchainKHR 0** → Present 0（PRIVATE 不可 CPU 读，平面数据不可用） |
| win `--fmt=0x23`（YUV420） | swapchain 段 PASS | **CreateSwapchainKHR 0**、Present 0；但 `AImageReader_acquireNextImage -> -10000`（该 reader 拿不到已 post 的图，属探针校验手段限制） |

### 25.2 ① 驱动渲染被**独立证明**（不需要 surface / Activity / root）

```
== mode render: image -> clear -> CopyImageToBuffer -> CPU readback
  vkCreateImage(own, optimal, 64x64 RGBA8, color|src|dst) -> 0 (VK_SUCCESS)
  image mem req: size=73728 typeBits=0x7 align=4096
  vkAllocateMemory(image) -> 0 (VK_SUCCESS)
  vkBindImageMemory -> 0 (VK_SUCCESS)
  [render] vkQueueSubmit(clear+copy) -> 0 (VK_SUCCESS)
  [render] vkWaitForFences(5s) -> 0 (VK_SUCCESS)
  [render] pixel(0,0) =  64 128 191 255  (want  64 128 191 255)
  [render] pixel(32,32) =  64 128 191 255  (want  64 128 191 255)
  [render] pixel(63,63) =  64 128 191 255  (want  64 128 191 255)
  [render] PIXEL PASS (clear colour read back exactly)
=== SUMMARY mode=render failures=0 ===
```

判据色 `64/128/191/255` 在三点**全部精确命中**，且**不是 R/B 互换**（互换分支未被触发）。
⇒ Mesa PanVK 在这台无 root 的 PHZ110 上**真的把命令提交给了 GPU 并等到了完成**，回读的像素就是 GPU 写的。
驱动侧 MESA 日志只有 `No gralloc hwmodule detected (video buffers won't be supported)` +
`Using fallback gralloc implementation`（**没有** `Failed to get u_gralloc_buffer_basic_info`）。

### 25.3 ② `ahb(0x1)` 全绿：`u_gralloc` 环**是通的**

```
  vkGetAndroidHardwareBufferPropertiesANDROID -> 0 allocSize=16384 typeBits=0x3
                                                 format=R8G8B8A8_UNORM externalFormat=37
  >>> vkAllocateMemory(import AHB, dedicated image) -> 0   <== the u_gralloc step
[P0A-V19-FULLPLANE] init how=SPHAL rc=0 mapper=… version=5
[P0A-V19-FULLPLANE] metadata layer_rc=0 layers=1 fourcc_rc=0 fourcc=0x34324241
                    modifier_rc=0 modifier=0x0 alloc_rc=0 alloc=16384 planes_rc=0 free_rc=0
[P0A-V19-FULLPLANE] accepted fourcc=0x34324241 modifier=0x0 planes=1
[P0A-V19-FULLPLANE] plane=0 fd_index=0 offset=0 stride=256 total=16384 sample_bits=32 samples=64x64 sub=1x1
kbase: import flags=0x4040f -> va=0x41000 pages=4 outflags=0x4540f
```

`0x34324241` = ASCII `'AB24'` = DRM `ABGR8888`；`stride=256`=64×4；`alloc=16384`=64×64×4，全部自洽。
`mapper` 模式进一步证明**厂商 IMapper V5 本身是好的**：四条 Binder-NDK `openDeclaredPassthroughHal` 全 NULL，
驱动真正走 **SPHAL**（`android_load_sphal_library("mapper.mediatek.so")`）⇒ `AIMapper_loadIMapper rc=0 version=5`、
`importBuffer rc=0`、5 类 metadata 全部返回正值；且其"自描述包装"（类型名 + 类型 id + 尾部值）
与驱动 `[P0A-V19-FULLPLANE]` 解析出的 `fourcc/alloc/layers` **逐项吻合** ⇒ Mesa 的 u_gralloc **读对了**。

### 25.4 ③ `win(0x1)`：真交换链建成 + present + **窗口像素校验通过**

```
  vkCreateAndroidSurfaceKHR(window) -> 0
  caps: minImageCount=2 maxImageCount=4 currentExtent=64x64 usage=0x17 composite=0x9
  >>> vkCreateSwapchainKHR(minImageCount=2 64x64 RGBA8 FIFO) -> 0 (VK_SUCCESS)  <== 原以为是卡点
  vkAcquireNextImageKHR -> 0 index=0 → vkQueueSubmit(clear) 0 → vkQueuePresentKHR 0
  window pixel(32,32) =  64 128 191 255  stride=256  (want 64 128 191 255)
  WINDOW PIXEL PASS (the presented patch reached the window buffer)
=== SUMMARY mode=win failures=0 ===
```

### 25.5 ④ ★ 原假设**不能复现**：`vkCreateSwapchainKHR` 三条 surface 路径全部 `VK_SUCCESS`

§20.3/§21/§22 的假设是"交换链创建必然失败 / `VK_ERROR_INVALID_EXTERNAL_HANDLE`"。
探针实测：**`win` / `headless` / `winimpdef` 三条路径下 `vkCreateSwapchainKHR` 全是 `VK_SUCCESS`**，
`win` 连 present 与窗口像素都过了（25.4）⇒ **交换链创建这一步本身不是无条件坏的**。

唯一**可复现**的 `-1000072003` 被钉在**一个格式上**：

```
  AHardwareBuffer_allocate(fmt=0x22 usage=0x303) -> rc=0        ← 唯一自变量：format
  AHardwareBuffer_describe: w=64 h=64 stride=64 fmt=0x22 …
  vkCreateImage(EXTERNAL AHB) -> 0
  vkGetAndroidHardwareBufferPropertiesANDROID -> -1000072003    ← 同段代码，fmt=0x1 时是 0
E MESA : Failed to get u_gralloc_buffer_basic_info              ← 致命项
```

`0x22` = `AHARDWAREBUFFER_FORMAT_IMPLEMENTATION_DEFINED`，正是真实 Android Surface / ImageReader
**最常用**的格式；此模式连 `[P0A-V19-FULLPLANE] init how=` 都没打出来
⇒ 失败发生在 u_gralloc 更早的 `u_gralloc_get_buffer_basic_info()` 里。两次独立运行**逐字节一致**。
⇒ **这就是「u_gralloc 环」真正的断点，且只断在 `0x22` 上。**
（补充：`winimpdef` 用 `AIMAGE_FORMAT_PRIVATE` 时交换链仍是 0 ⇒ Mesa 的 Android WSI 给交换链备图时
**会强制显式格式**，不会把 IMPLEMENTATION_DEFINED 透传下去。）

### 25.6 ⑤ ★★ 真正让 GPU 掉线的是「绘制」，不是 WSI

```
  ... vkCreateRenderPass/Framebuffer/ShaderModule×2/PipelineLayout/GraphicsPipelines -> 全部 0
  [tri] vkQueueSubmit(draw) -> 0 (VK_SUCCESS)
  [tri] vkWaitForFences(5s) -> -4 (VK_ERROR_DEVICE_LOST)
=== SUMMARY mode=tri failures=1 ===
```

MESA logcat：

```
E MESA : kbase: CSF group 0 fatal error: status 0x7dc002c3 (exception 0xc3), sideband 0x0000005fffe1e000
E MESA : kbase: CSF group 1 fatal error: status 0x7dc002c3 (exception 0xc3), sideband 0x0000005fffe1e000
E MESA : kbase: CSF group 2 fatal error: status 0x7dc002c3 (exception 0xc3), sideband 0x0000005fffe1e000
W MESA : kbase: received CSF CPU queue dump notification
```

`vkCreateGraphicsPipelines` 全过、`vkQueueSubmit` 也返回成功 —— **是 GPU 固件（CSF）在真正执行 draw 时炸了**
（三个 CSF group 同时 fatal，exception `0xc3`）。而**不含 draw 的 clear+copy 路径完全正常**（25.2 / 25.3）。
⇒ 分界线很清楚：**命令提交与内存/导入链路是通的；坏在图形管线的实际光栅化执行。**

### 25.7 结论与**目标转向**（本节最重要的一行）

1. **驱动渲染被独立证明**：`render` / `ahb(0x1)` / `win(0x1)` 三模式均 `failures=0`，
   像素三点精确（`64/128/191/255`），`win` 还证明 present 的像素**真的落到了窗口 buffer**。
2. **原假设（交换链建不起来）在合成探针里不能复现**：三条 surface 路径的 `vkCreateSwapchainKHR` 全成功。
3. **可复现的失败只有两个**：AHB 用 `IMPLEMENTATION_DEFINED(0x22)` 分配时的 `-1000072003`；
   以及 `tri` 的 `-4`。
4. ★ **`-4` 的真身 = CSF exception `0xc3`**：`vkQueueSubmit` 返回成功、`vkWaitForFences` 得 `-4`，
   kbase 报三个 CSF group fatal。**`-4` 与 WSI / u_gralloc / dma-heap 均无直接关系。**
5. ⇒ **当前首要目标从"修 WSI"转为"查 CSF exception `0xc3`"**：
   即使交换链建起来（v50 做的正是这件事），**只要真的画东西，GPU 就掉**。
   下一步取证：`logcat -b all -d` / `dmesg` 抓 mali/kbase fault、CSF 固件与 Mesa 侧的
   shader/pipeline 描述符，以及"哪一类 draw（几何/绑定/描述符）触发 `0xc3`"的最小化对照。

### 25.8 未做/限制（如实）

- 探针结论**只覆盖合成探针**；推广到**真实 App（Minecraft / ZL2 + MobileGL）**需要一次端到端复现，
  本轮会话**没有也无法**做（禁真实屏 UI 操作）。
- 探针跑的是**补丁前**驱动（`4417b369`）⇒ §24 的 WSI 补丁**不参与**本节任何结论。
- `render` 模式的 MESA 日志中**没有** `kbase:` 行（该模式的图像内存未走 dma-buf 路径），
  因此**不能**用本节日志判定 `DMA_HEAP_IOCTL_ALLOC` 的成败（见 23.3 的口径说明）。

---

## 26. v51/v52：调试取证链路（v51 已废弃）

### 26.1 v51：**空载荷 APK ⇒ 已废弃，勿用**

v51 的打包在**载荷注入**这一步出错：用 `unzip` 提取/替换待处理的载荷条目时**匹配失败**
（父级现场记录），结果 zip 里**只剩 manifest / `resources.arsc` / `classes.dex` / 签名**，
**一条 `lib/arm64-v8a/*` 都没有**（服务器 `/root/final/` 实测）：

```
$ unzip -l mgl-panvk-v51.apk
  Length      Date    Time    Name
     3504  1980-01-01 00:00   AndroidManifest.xml
       40  1980-01-01 00:00   resources.arsc
     1328  2026-10-05 11:03   classes.dex
      412  2026-10-05 11:03   META-INF/DSHDRIVE.SF
     1337  2026-10-05 11:03   META-INF/DSHDRIVE.RSA
      285  2026-10-05 11:03   META-INF/MANIFEST.MF
```

size **8,595 B**（正常版 10,187,311 B）、sha256 `6e9ce7d2…b0313`（全值见 [`MANIFEST.md`](../MANIFEST.md) §B）。
⇒ **v51 作废，不再作为任何实验的对照件**。

> **教训（v53 已据此加固）**：打包后必须做**两条**载荷校验 ——
> ①`unzip -l` 数一遍 `lib/arm64-v8a/*` 条目；②`unzip -p … lib/arm64-v8a/libvulkan_freedreno.so | sha256sum`
> 与源件比对。**只看 `apksigner verify` 通过是不够的**（v51 的签名是**通过**的）。

### 26.2 v52：v50 载荷 + 全套调试 env

v52 = **v50 的载荷（一字节不改）** + 完整调试环境变量。sha256 `0bbef030…`（全值见 §B 台账）。
env 最终形态（v53 沿用同一份，`aapt2 dump xmltree` 逐字符 diff 一致）：

```
LIBGL_ES=3:POJAV_RENDERER=opengles3:MOBILEGL_BACKEND_TYPE=DirectVulkan:MOBILEGL_ESPRYT_USE_ANGLE=0:
MOBILEGL_MAGMA_R11G11B10F_FALLBACK=0:MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log:
MESA_DEBUG=1:PANVK_DEBUG=1:LIBGL_DEBUG=1:EGL_LOG_LEVEL=debug
```

`MESA_VK_WSI_HEADLESS_SWAPCHAIN` **未加回**（dump 里出现次数 = 0，已核）。

### 26.3 ★ 关键教训一：**logcat 环形缓冲会冲掉证据**

`libvulkan_freedreno.so` / MGL 的前期日志量极大（`PANVK_DEBUG=1` + `LIBGL_DEBUG=1`），
**logcat 的环形缓冲会在崩溃前就把早期行挤掉** —— 包括我们要找的那一行
（"主界面正常渲染约 10 s"这段窗口里的 `E/MESA` 行），用事后 `logcat -d` 回读时**已经不在缓冲区里**。

⇒ **治本做法：起进程前就开后台落盘**：

```bash
adb shell "logcat -b all -v time > /data/local/tmp/cap.txt" &   # 或设备侧 setsid 起
# 复现崩溃后
adb shell "cat /data/local/tmp/cap.txt" > cap.txt
```

落盘文件不受环形缓冲影响，`-b all` 同时覆盖 `main/system/crash`，
`-v time` 保证**时间线可对**（§28 的"判据行 → +5~10s OOM → +9s DEVICE_LOST"就是靠它对齐的）。

### 26.4 ★ 关键教训二：`pkill -f "<模式>"` 会自杀

清理旧 logcat 进程时，`pkill -f "<含自身命令行的模式>"` 会把**执行这条 `pkill` 的 shell 自己**
匹配上（模式串就出现在它自己的 argv 里）并杀掉 ⇒ 现象是"命令没输出就断了"，
很容易被误判成"设备/通道出问题"。

⇒ **用 `pkill -x logcat`**（按**进程名**精确匹配，不匹配命令行）。

---

## 27. v53（P1）：CSF group create 走 uAPI 1.18 布局并置 `csi_handlers` —— **真机无效 ✗**

### 27.1 改了什么（唯一一处，详见 [`research/16`](../research/16-p1-p2-implementation.md)）

文件 `src/panfrost/lib/kmod/kbase_kmod.c` 的函数 `kbase_kmod_csf_group_create()`：
版本阶梯从 `1.25 / 1.6` 两档补成 **`1.25 / 1.18 / 1.6`** 三档，**新增 1.18 档**：

| 档（`driver.version`） | 结构体 | ioctl | `csi_handlers` |
|---|---|---|---|
| `>= 1.25` | 112 B | `0xc070803a` | 置 `BASE_CSF_TILER_OOM_EXCEPTION_FLAG` |
| **`>= 1.18`（新增）** | **40 B** | **`0xc028803a`（= 58）** | **置 `BASE_CSF_TILER_OOM_EXCEPTION_FLAG`** |
| `< 1.18` | 32 B | `0xc020802a`（1.6 兜底） | 老设备路径，**未动** |

uAPI 判断（`pan_kmod_driver_version_at_least()`）**原样保留**；新分支失败会打
`kbase: 1.18 CS_QUEUE_GROUP_CREATE failed: … falling back to the 1.6 ABI` 后**照旧落到 1.6**。

**动机**（[`research/14`](../research/14-tiler-heap-oom.md) 独立得到同一结论，公开 kbase r43p0 支撑）：
内核 `handle_oom_event()` 只在 `csi_handlers & BASE_CSF_TILER_OOM_EXCEPTION_FLAG`
（且 `pending_frag_count==0`、`err ∈ {-ENOMEM,-EBUSY}`）时，才把 tiler OOM 当作**可恢复的增量渲染**
交还固件；否则 `term_queue_group()` + `report_tiler_oom_error()` ——
**正是我们抓到的那行 `kbase: CSF group N tiler heap OOM notification`，且此时组已被杀掉**。
而 panvk 侧的 TILER_OOM handler **早就实现并注册好了**（增量渲染本体），**只是这个 flag 从未送达内核**。

### 27.2 产物与静态验证（服务器实测）

| 项 | 值 |
|---|---|
| 新 `.so` | size **20,005,600**，md5 **`7f3a0e8f8a2ec70d31f11fa6fe3ab404`**，sha256 **`58ef996f9cfd5dbd88c37daea4f02bbde4fdee5818561526d21cf9adadefbec4`** |
| 旧件（v50/v52 在用） | size 20,005,320，md5 `e08e07645c16d8ebaa11ca70a09884fd` |
| `mgl-panvk-v53.apk` | size 10,187,311，sha256 **`9c99af82d51b54c3ca7f97d1ffd206f5edad0f534490afa7c34be1dfc073d48e`** |

- 增量编译 `NINJA_EXIT=0`（只重编 `kbase_kmod.c` + 一次重链，全文见 `research/attachments/16/16-p1-build.log`）；
- ★ **反汇编确认新分支真的进了二进制**（不是只改到源码）：新 `.so` 里出现
  `movk w1, #0xc028, lsl #16`（⇒ `0xc028803a`）与 `strb w8, [sp, #0x1d]`（⇒ `csi_handlers(29)=1`），
  且 1.25 / 1.6 两档的门槛与 ioctl 常量（`0xc070803a` / `0xc020802a`）**原样保留**
  （`llvm-objdump -d --disassemble-symbols=kbase_kmod_csf_group_create`，[`research/16`](../research/16-p1-p2-implementation.md) §2.1）；
- ABI 字段偏移**二次自证**：`research/attachments/16/16-p1-layout-check.c`
  （`sizeof=40`、`offsetof(csi_handlers)=29`、`KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_18=0xc028803a`）；
- APK 载荷**逐位校验**：`unzip -p … lib/arm64-v8a/libvulkan_freedreno.so | sha256sum` = 新 `.so` 的 sha256
  —— **非空载荷，已核**（针对 v51 的教训）。

### 27.3 ★ 真机结果：**无效 ✗**

真机装上 v53（其余一切与 v52 相同、**单变量**）后：

- **仍然出现** `E/MESA: kbase: CSF group 0 tiler heap OOM notification`（与 v52 同一条）；
- **没有出现**期望的 `kbase: created CSF group N with TILER_OOM CSI handler (1.18 layout, ioctl 58)`
  —— 这是三条判据里唯一能证明"flag 送达内核"的正面证据。

⇒ **推断：1.18 分支被版本门挡住、根本没走到** ——
即协商到的 `driver.version` 使 `pan_kmod_driver_version_at_least(&dev->driver, 1, 18)` 为假，
代码按阶梯落回 1.6 兜底（[`research/16`](../research/16-p1-p2-implementation.md) §8 的第 3 行口径）。
**⚠️ 未验证**：本轮素材里**没有**另外两条分支日志（1.25 档成功行 / 1.6 兜底行）的记录，
要钉死"到底走了哪一档"必须回读 `kbase_kmod.c` 里 `VERSION_CHECK_CSF` 协商出的版本值。
（也不能反推成"内核拒收 40 B ioctl"—— 那会打 `1.18 … falling back to the 1.6 ABI`，素材里同样没有。）

⇒ **结论：P1 单独不够。** [`research/14`](../research/14-tiler-heap-oom.md) 已给出更根本的一条：
**「tiler heap 只涨不落」才是 OOM 的直接成因** ——
Mesa 侧唯一的重置手段 `kbase_renew_tiler_heap()` 因
`submit->tiler_work_estimate` **在全树没有任何写入点**而成为**死代码**（触发条件恒为假），
堆只能单调涨到 `max_chunks=400` → `-ENOMEM` → 内核 `term_queue_group()` 杀组。
⇒ **下一步 = P2**（接上 renew；最小 2 行版补丁草案见
[`research/16`](../research/16-p1-p2-implementation.md) §6 / [`research/14`](../research/14-tiler-heap-oom.md) Fix B1）。

---

## 28. ★ 真机实测记录（v46–v53）

> **口径**：本节只写**真机上真实发生的事**（含用户现场口述，已在条目内标明"用户实测"）。
> 凡是**推断**一律标注；凡是**素材中没有**的一律写"无记录"，**不补**。
> v46–v49 的版本定义见 [`MANIFEST.md`](../MANIFEST.md) §A，v50–v53 见 §B 台账。

### 28.1 v46 / v47（headless 交换链）

- **能出判据行**（v47，`MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`）：
  `OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)`。
- 但**约 2 秒后**在 `vkQueueSubmit(texture upload)` 上得到 **`VK_ERROR_DEVICE_LOST (-4)`**，
  游戏随之崩溃（`docs/09` §22/§23.1）。
- ⇒ 判据行达成**不等于**画面达成：headless 交换链的 `queue_present` 是空操作。

### 28.2 v48（仅加调试 env）

- 症状与 v47 **完全相同**；价值在于**第一次**把 `-4` 定位到
  MGL 的**纹理上传批次** `vkQueueSubmit`（`VkTextureManager.cpp`），
  且该链**不创建 AHB、不经过 `u_gralloc`、也不经过任何 WSI 代码**（`docs/09` §23.1）。

### 28.3 v49（`PANVK_KBASE_DMA_HEAP=/dev/null/nonexistent`）

- **无效 ✗**：错误码、错误位置**与 v48 逐字相同**，`-4` 照旧（`docs/09` §23.2）。
- ⇒ 反向对照成立：**dma-heap / dma-buf 不是该 `-4` 的成因**。

### 28.4 v50（WSI 补丁、删掉 headless hack）—— **首次出现真交换链**

- v50 = v49 的载荷换成 WSI 补丁后的新 `.so`，**唯一自变量差异 = 删掉
  `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`**（`docs/09` §24.3）。
- **MGL 日志首次出现真交换链**：

```
Swapchain created, extent = 2376x1080, swapchain imageCount = 3
```

- **判据行仍然达成**（同一行原文）：
  `OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)`。
- ★ **用户实测**：**MC 主界面（含 3D 全景）正常渲染约 10 秒、画面干净**
  （**不花屏 / 不乱跳 / 不撕裂**），之后**黑屏并崩溃**；
  日志里 `vkQueuePresentKHR` / `vkAcquireNextImageKHR` 返回 **`-4`**。
- ⇒ 这是本项目**第一次**看到"真交换链 + 真画面"，
  但 `-4` 只是**从"2 秒"推迟到"约 10 秒"**，并未消除。

### 28.5 v52（v50 + 调试 env）

- 复现现象与 v50 **相同**（一样约 10 秒干净画面后黑屏崩溃）。
- 用 §26.3 的**后台落盘 logcat** 抓到了决定性的一行：

```
E/MESA: kbase: CSF group 0 tiler heap OOM notification
```

- **时间线**（落盘 logcat，`-v time`）：
  **判据行 → +5~10 s 该 OOM 行 → +9 s `VK_ERROR_DEVICE_LOST`**。
- ⇒ 从此把"10 秒黑洞"与 **tiler heap OOM** 挂上了钩
  （[`research/14`](../research/14-tiler-heap-oom.md) §1.1 记录的两个绝对时刻
  `11:25:37.423` → `11:25:47.413` 正好 **9.99 s**，与 `KBASE_WAIT_TIMEOUT_NS` 一致）。

### 28.6 v53（P1：CSF group create 1.18 档 + `csi_handlers`）

- **无效 ✗** —— 详见 §27.3：仍出现同一条 tiler heap OOM 通知，
  且**没有**出现期望的 `TILER_OOM CSI handler (1.18 layout, ioctl 58)`。
- ⇒ 推断 1.18 分支被版本门挡住；**P1 单独不够**，下一步 **P2**（接上 tiler heap renew）。

### 28.7 本轮**没有**做成的事（如实）

- v51 **是空载荷 APK**（§26.1），因此 **v51 没有任何真机结果**；
- v46–v53 的**完整 logcat 原文**未逐字归档 —— 本节的引用均为**关键行 + 时间线**；
  更早的原始日志只在服务器 `/root/research/` 的对应报告与父级会话中；
- `-4` 与 §25 探针抓到的 **CSF exception `0xc3`** 是**两条不同的病灶**
  （`0xc3` = CSF LSU 的 `TRANSLATION_FAULT_3`，见 [`research/13`](../research/13-csf-exception-c3.md)；
  tiler heap OOM = §28.4–§28.6 这条线），**不要合并看**。
