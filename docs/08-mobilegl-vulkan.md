# MobileGL（MGL）与它的 Vulkan 后端：怎么接到我们的 PanVK 上

> 目标栈：**Minecraft 26.3 → MobileGL（DirectVulkan 后端）→ Vulkan loader → 我们的 PanVK → kbase**
>
> 本篇是调研结论 + 可执行的落地计划。所有结论都标了来源（源码路径 / 官方 README）。

---

## 1. MobileGL 是什么（官方 README 原文要点）

- 定位：**桌面 OpenGL 的开源实现**，多后端、带完整状态管理层
- 模块划分：
  - `MG_State` —— 图形 API 状态跟踪与管理
  - `MG_Impl` —— 图形 API 的前端实现（对 `MG_State` / `MG_Backend` 编程）
  - **`MG_Backend`** —— 每个后端一个翻译层，把前端语义映射到具体后端 API
- **两个后端**：
  - `Direct (OpenGL ES)` —— 跑在宿主 GLES 上（社区里 FCL/ZL2 用的 "MG 渲染器/MobileGlues" 就是这一类）
  - **`Direct (Vulkan)`** —— 内部代号 **Magma**，跑在 **Vulkan** 上 ← **本篇主角**
- 短期目标：`OpenGL 4.2 (Core Profile)`
- 状态：**没有发布任何预编译二进制**，必须自行构建（CMake、C++23、需初始化子模块）
- 许可：**LGPL-3.0**

> 注意与 **MobileGlues**（`MobileGL-Dev/MobileGlues-release`）区分：那是打包好的
> "GL 跑在宿主 GLES 上" 的发行版，**不经过 Vulkan**，跟我们想用的 PanVK 无关。

## 2. 后端是「运行期环境变量」，不用重编

CMake 有两个后端实现（`CMakeLists.txt` 里同时编译 `MG_Backend/DirectGLES/*`
与 `MG_Backend/DirectVulkan/*`），运行期用环境变量切换：

| 变量 | 取值 | 默认 |
|---|---|---|
| `MOBILEGL_BACKEND_TYPE` | `DirectGLES` / **`DirectVulkan`** | `DirectGLES` |
| `MOBILEGL_MAGMA_FRAMESINFLIGHT` | 1–64 | 3 |
| `MOBILEGL_MAGMA_R11G11B10F_FALLBACK` | 0/1 | 0 |
| `MOBILEGL_MAGMA_DISABLE_SUBGROUP` | 0/1 | 0 |
| `MOBILEGL_DISABLE_TIMERQUERY` | 0/1 | 0 |
| `MOBILEGL_COHERENT_AS_FLUSH` | 0/1 | 0 |
| `MOBILEGL_ESPRYT_USE_ANGLE` | 0/1 | 0 |

**⇒ 结论：只要编译出一个插件 APK，在启动器里把「Rendering backend」切成
`DirectVulkan` 即可** —— 这是启动器 UI 里的一个下拉项（见下节）。

## 3. 启动器的**渲染器插件契约**（来自 ZalithLauncher2 源码）

`game/plugin/renderer/RendererPluginManager.kt`：

```kotlin
val metaData = info.metaData ?: return
if (metaData.getBoolean("fclPlugin", false) ||
    metaData.getBoolean("zalithRendererPlugin", false)) {
    val rendererString  = metaData.getString("renderer")   // ★ 要 dlopen 的库名
    val des             = metaData.getString("des")        // 描述
    val pojavEnvString  = metaData.getString("pojavEnv")   // ★ 环境变量
    minMCVer = metaData.getVersionString("minMCVer")       // 支持的 MC 版本区间
    maxMCVer = metaData.getVersionString("maxMCVer")
}
```

加载方式（`game/launch/GameLauncher.kt`）：

```kotlin
val rendererLib = getRendererLibrary() ?: return
if (!ZLBridge.dlopen(rendererLib) && !ZLBridge.dlopen(findInLdLibPath(rendererLib))) {
    Logger.error(TAG, "Failed to load renderer $rendererLib")
}
```

**契约总结（一个渲染器插件 APK 需要）**：

| meta-data | 含义 |
|---|---|
| `fclPlugin=true`（或 `zalithRendererPlugin=true`）| 标记为渲染器插件 |
| `renderer` | **原生库文件名**，启动器会 `dlopen` 它（例：`libMobileGL.so`）|
| `des` | 显示名 |
| `pojavEnv` | 空格分隔的 `KEY=VALUE`，注入游戏进程 |
| `minMCVer` / `maxMCVer` | MC 版本区间 |
| 还有一个 **V2 格式**：`fclPlugin_V2`（`@string/config`，支持带 UI 的多项配置）|

（另外还有 `fclPlugin` 的**驱动插件**变体：`fclPlugin=true` + `driver=<名字>`，
那个用 `DRIVER_PATH` 注入 Vulkan 驱动 —— 见 [03](03-fcl-adreno-lock.md)。）

## 4. MobileGL 自带的 Android 插件工程，已经把这一切做好了

仓库里有 `android-plugin/`（Gradle 工程，产物就是 FCL/ZL2 的渲染器插件 APK）：

`android-plugin/app/build.gradle.kts`：

```kotlin
renderer(
    rendererId      = "opengles3",
    rendererGLPath  = nativePath("libMobileGL.so"),
    rendererEGLPath = nativePath("libMobileGL.so"),
    ...
    customizable(key = "MOBILEGL_BACKEND_TYPE",
                 items = RendererConfig.EnvItems("DirectGLES", listOf("DirectVulkan"))),  // ★
    toggleable("MOBILEGL_MAGMA_DISABLE_SUBGROUP", ...),
    toggleable("MOBILEGL_MAGMA_R11G11B10F_FALLBACK", ...),
    customizable("MOBILEGL_MAGMA_FRAMESINFLIGHT", "3", ...),
    ...
)
ndkVersion = "27.3.13750724"
boatEnv { put("MOBILEGL_BACKEND_TYPE", "DirectGLES") }   // 默认
pojavEnv { put("MOBILEGL_BACKEND_TYPE", "DirectGLES") }
abiFilters += arm64-v8a
productFlavors { create("plugin"); create("trace") }     // plugin = 我们要的
```

`android-plugin/app/src/main/AndroidManifest.xml` 里对应地生成了
`fclPlugin` / `des` / `renderer` / `pojavEnv` / `boatEnv` 以及
`mobilegl_backend_type_title` 等 UI 标题项。

**⇒ 所以「把 MGL 的 Vulkan 后端接上我们的 panvk」= 构建这个 `plugin` flavor 的
APK + 在启动器里把后端切成 `DirectVulkan`。**

## 5. Vulkan 从哪来 —— 我们接进去的入口

MobileGL 的 Magma 后端使用 **标准 Vulkan loader**（README 明确列出
`VK_ICD_FILENAMES` 作为配置项）。因此有两条接法：

| 路线 | 做法 | 优点 | 风险 |
|---|---|---|---|
| **A. 走启动器的驱动注入** | 启动器把厂商 HAL 换成我们的 `libvulkan_freedreno.so`（PanVK），MGL 的 loader 自然就用它 | 与现有链路一致 | FCL 上我们已发现两处缺陷（见 [07](07-our-build-and-results.md)）|
| **B. 走 ICD** | 在渲染器插件的 `pojavEnv` 里设 `VK_ICD_FILENAMES=<插件目录>/panvk.json`，让 loader 直接加载我们的驱动 | **绕过启动器的驱动注入**，与厂商锁完全无关 ✓ | 需要一个 ICD JSON + 路径占位符支持 |

**路线 B 值得优先试** —— 它把「启动器要不要支持非 Adreno」这个问题**整个绕开了**：
只要是标准 Vulkan loader，`VK_ICD_FILENAMES` 指向谁就用谁。

## 6. 落地步骤（可执行清单）

```
1. 构建 MobileGL android-plugin（plugin flavor, arm64-v8a）
     cd MobileGL/android-plugin
     echo "sdk.dir=/opt/android-sdk" > local.properties
     ./gradlew :app:assemblePluginRelease -Pmobilegl.abis=arm64-v8a
   ← 需要：NDK 27.3+、CMake、JVM、子模块（DiligentCore / SPIRV-Cross / glslang / VMA / Vulkan-Headers…）

2. 安装 APK → 在启动器「渲染器」里选中它 → 把 Rendering backend 设为 DirectVulkan

3. 让 Vulkan loader 找到我们的 PanVK（二选一）
   A) 启动器驱动插件指向我们的 libvulkan_freedreno.so（现有链路）
   B) 渲染器插件 pojavEnv 里加 VK_ICD_FILENAMES=<panvk.json>

4. 用 MC 26.3 实测；F3 界面应显示 MobileGL + Direct (Vulkan)
   （README 原话：After startup, the Minecraft F3 screen should report
     MobileGL and the Direct (Vulkan) backend if the injection worked.）
```

## 7. 与 MC 26.3 的关系

- 26.3 是 MC 的新版本号体系下的快照版；本项目在 26.2-Fabric 上已完成大半验证
  （我们的 PanVK 可被加载并初始化 —— 见 [07](07-our-build-and-results.md)）
- **走 MobileGL 路线时，MC 不需要用它的原生 Vulkan 后端**：MC 照旧走 OpenGL，
  由 MobileGL 在下面翻译成 Vulkan。这正好**避开了**我们在 FCL 上遇到的
  「原生 Vulkan 后端触发 `NoSuchMethodError`」那个坑 ✓
- 因此推荐组合：**MC（任意版本）+ MobileGL(DirectVulkan) + PanVK**，
  而不是 MC 原生 Vulkan 后端

## 8. 参考

- [MobileGL-Dev/MobileGL](https://github.com/MobileGL-Dev/MobileGL)（LGPL-3.0，无预编译产物）
- [MobileGL-Dev/MobileGlues-release](https://github.com/MobileGL-Dev/MobileGlues-release)（GLES 路线，非本方案）
- [FCL 自定义渲染器文档（DeepWiki）](https://deepwiki.com/FCL-Team/FoldCraftLauncher/5.3-creating-custom-renderers)
- 社区讨论：[FCL / ZL2 使用 MobileGlues 渲染器](https://klpbbs.com/thread-170028-1-1.html)
