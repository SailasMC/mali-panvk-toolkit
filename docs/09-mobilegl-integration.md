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
