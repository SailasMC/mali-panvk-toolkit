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
