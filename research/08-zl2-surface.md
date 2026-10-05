# 08 · ZalithLauncher2（FCL 兼容）如何把 Surface 交给渲染器

- 源码：`/root/zl2src/ZalithLauncher2-main`（只读；本机快照 887 文件 / 9.8 MB）
- 对照源码：`/root/fcl`（FoldCraftLauncher，`git bb1308a3f37a8ce98c9e40614bfc8985f63b937f`，2026-10-04）
- MobileGL 源码：`/root/MobileGL`（DirectVulkan 后端）
- 阅读日期：2026-10-05

> **快照完整性提醒**：ZL2 快照只含 `ZalithLauncher/src/main/java` + `src/main/jni`，**没有** `AndroidManifest.xml`、`build.gradle`、`jniLibs/`、`assets/`、git 元数据。
> 因此：① 启动器自身的 manifest 无法核对；② `libOSMesa_*.so` 等预编译 native 是否存在只能靠代码里的文件名推断；
> ③ 结论中标注 `[不在树内]` 的符号（Pojav 定制版 GLFW/LWJGL、SDL3 的 C 侧）属于外部二进制。

---

## 摘要（12 行）

1. Surface 的唯一入口是 `ZLBridge.setupBridgeWindow(Object)` → JNI `egl_bridge.c:80` → `ANativeWindow_fromSurface`（`egl_bridge.c:84`），存进全局 `pojav_environ->pojavWindow`（`environ.h:39`）。
2. 谁调用它取决于 MC 用哪套窗口层：老版本走 GLFW → `GameHandler.kt:94` / `VMActivity.kt:748,800`；MC 26.3+ 走 SDL3 → `SdlBridge.kt:101` → `SDLSurface.setNativeSurface` → SDL 的 JNI 自己 `ANativeWindow_fromSurface`。
3. 渲染器侧拿 Surface 的方式由 `POJAV_RENDERER` 字符串唯一决定（`egl_bridge.c:134-197`）：`opengles*` → GL 桥（**EGL `eglCreateWindowSurface`**，`gl_bridge.c:132`）；`custom_gallium`/`vulkan_zink`/`gallium_freedreno`/`gallium_panfrost` → OSMesa 桥（**没有 EGL，没有 VkSurface**，`ANativeWindow_lock` 拿 CPU 缓冲，`osm_bridge.c:126,84`）。
4. Vulkan 直连路径：`GLFW_NO_API` → `config_renderer=RENDERER_VULKAN` → `pojavCreateContext` **直接把 `pojavWindow` 当窗口返回**（`egl_bridge.c:294`），由 MC/LWJGL 自己 `vkCreateAndroidSurfaceKHR`。
5. 可配置项只有 3 类：启动器设置（`AllSettings.kt`：`renderer/vulkanDriver/graphicsApi/useSurfaceView/zinkPreferSystemDriver/vsyncInZink/dumpShaders/resolution*`）、插件 manifest（`renderer`/`pojavEnv`）、以及代码里写死的一批 env key。
6. `pojavEnv` 的传递：`RendererPluginManager.kt:111-127` 解析 → `RendererPlugin.env` → `GameLauncher.kt:409` 合进 envMap → `Launcher.kt:470-477` `Os.setenv` → **同进程**环境变量，原生侧 `getenv` 直接可见。
7. 插件契约**没有**「离屏/无窗口」开关；离屏是「落到哪个桥」的副产品。
8. `boatEnv` 在 **ZL2 完全不存在**（全树 0 命中）；FCL 的 v1 契约**要求**它有值（`RendererPlugin.kt:96`）却**从不消费**（全树只有解析处）。
9. FCL vs ZL2 在该点上的差异：① 桥集合少了 `gallium_panfrost`；② FCL 有 `custom_gallium` 之外的 Adreno/turnip 专用加载器与 `VKSHIM_ENABLE`，ZL2 没有；③ FCL 用 TextureView only，ZL2 多了 `useSurfaceView`；④ FCL `pojavCreateContext` 有 NULL 兜底（`egl_bridge.c:388`），ZL2 没有。
10. **能**（切桥）：把 `POJAV_RENDERER` 换成 `custom_gallium`（或 `vulkan_zink`/`gallium_panfrost`）→ 渲染器侧**永不创建 Android 交换链**，画面走 CPU 缓冲 + `ANativeWindow_unlockAndPost`。纯 manifest 改动，不动 panvk。
11. **不能**（保桥）：保留 `opengles*`（MobileGL DirectVulkan）却只让 panvk 不建交换链——launcher 侧没有任何开关能强制 EGL 走 pbuffer/headless；pbuffer 只在「窗口已死」时兜底（`gl_bridge.c:166-169`）。`useSurfaceView`、虚拟屏、`GraphicsApi` 都不改变「是否建交换链」。
12. 且**切桥≠换成 panvk**：ZL2 在 Mali 上拿 Vulkan 只能 `dlopen("libvulkan.so")`（`egl_bridge.c:129`；turnip 分支被 `checkAdrenoGraphics()` 的 Qualcomm 判定挡死，`driver_helper.c:52,62`），Android loader 又忽略 `VK_ICD_FILENAMES`（doc09 §11 铁证）→ 想真用 panvk 只能做「loader 转发垫片」，与本文第 5 节无关。

---

## 1. MC/JVM 侧如何拿到 ANativeWindow / Surface

### 1.1 Java/Kotlin 侧：Surface 从哪来（两条窗口层）

ZL2 有两条并行的窗口/输入栈，共享**同一个 `Surface` 对象**：

| 路径 | 谁创建 GL 上下文 | Surface 从哪取 | 关键位置 |
|---|---|---|---|
| **GLFW（老 MC / 内置 renderer）** | Pojav 定制版 `libglfw.so` `[不在树内]` | `SurfaceView.holder.surface` 或 `TextureView` 的 `Surface(SurfaceTexture)` | `VMActivity.kt:739-765`（TextureView）、`VMActivity.kt:795-817`（SurfaceView） |
| **SDL3（MC 26.3+ / RenderPearl）** | SDL3 自己（`SDL_GL_CreateContext`）`[不在树内]` | 同一个 `Surface` 经 `SDLSurface.setNativeSurface` 交给 SDL | `SdlBridge.kt:101-115`、`SDLSurface.java:115-122`、`SDLActivity.java:352-374` |

**SurfaceView / TextureView 的选择**由设置决定（默认 TextureView）：

```
AllSettings.kt:94            val useSurfaceView = boolSetting("useSurfaceView", false)
VMActivity.kt:880-903        if (useSurfaceView) SurfaceView(...).also{ applySizeToSurface = holder.setFixedSize }
                             else TextureView(...).also{ applySizeToSurface = surfaceTexture.setDefaultBufferSize }
VMActivity.kt:739-741        onSurfaceTextureAvailable → val nativeSurface = Surface(surfaceTexture)
VMActivity.kt:795-800        surfaceCreated → val surface = holder.surface
```

**交给原生**（老路径）：

```
VMActivity.kt:744-748        CallbackBridge.setDirectGamepadEnableHandler{...}; ZLBridge.setupBridgeWindow(nativeSurface)
VMActivity.kt:800            ZLBridge.setupBridgeWindow(surface)
GameHandler.kt:94            override suspend fun execute(surface, ...) { ZLBridge.setupBridgeWindow(surface) ... }
ZLBridge.java:70             @Keep public static native void setupBridgeWindow(Object surface);
ZLBridge.java:71             @Keep public static native void releaseBridgeWindow();
```

**交给 SDL**（新路径）：

```
SdlBridge.kt:101-115         prepareSurface(activity, surface, layout, source)
                               → SDL.initialize(); SDL.setContext(activity);
                                 SDLActivity.externalInitialize(SDLSurface(activity), layout, surface)   // 首窗
                               → SDLSurface.setNativeSurface(surface)                                  // 后续换窗
SDLSurface.java:115-122      setNativeSurface: 校验 isValid() → mNativeSurface = surface → surfaceCreated(null)
SDLSurface.java:131-135      surfaceCreated(holder) → SDLActivity.onNativeSurfaceCreated()   // native，SDL3 内部才是 ANativeWindow_fromSurface
SDLActivity.java:352-374     externalInitialize(...): mSurface = surface; SDLSurface.setNativeSurface(nativeSurface)
SdlBridge.kt:62-69           setupJNI() → SDL.setupJNI()
```

SDL 侧的 ANativeWindow 转换发生在 **libSDL3.so 的 C 代码里**（`Android_JNI_GetNativeSurface` + `ANativeWindow_fromSurface`），**不在本仓库**。

**SDL 与 GLFW 的窗口对齐**（ZL2 独有，`sdl_hook.c`）：

```
sdl_hook.c:583-602           create_sdl_hooks(): hook SDL_InitSubSystem / SDL_CreateWindow /
                             SDL_CreateWindowWithProperties / SDL_DestroyWindow / SDL_GetWindowFromEvent /
                             SDL_GetWindowFromID / SDL_LoadObject / SDL_UnloadObject / SDL_LoadFunction /
                             SDL_EGL_GetProcAddress        ← 注意：没有 SDL_GL_CreateContext / SDL_GL_SwapWindow
sdl_hook.c:439-471           custom_SDL_CreateWindow / WithProperties → 首窗复用（SDL Android 后端只支持单窗口）
sdl_hook.c:199-203           shouldReusePrimaryWindow(): env POJAV_SDL_REUSE_WINDOW（默认 true）
sdl_hook.c:368-396           sdlInitSubSystemPrepare(): notifyLauncher(NOTIF_TYPE_SDL, ACTION_INIT_LAUNCHER_INTEGRATION)
sdl_hook.c:414-421           forceEglProfileEs(): SDL_GL_SetAttribute(PROFILE_MASK, PROFILE_ES)（仅移动 ES 渲染器）
sdl_hook.c:264-302           injectEglProxy(): 在 eglChooseConfig/eglCreateContext/eglSwapBuffers 的**解析出口**套代理
sdl_hook.c:311-322/559-568   SDL_LoadObject("libvulkan*") 直接返回 VULKAN_PTR 句柄（SDL 与 LWJGL 必须同一 loader 实例）
sdl_dlopen_hook.c:31-51      customDlsym → sdlDlsymProxy()（LWJGL 经 dlsym 直接调 SDL 函数，GOT 补丁拦不到）
```

`CallbackBridge.java:91-133` 的 `notifyLauncher` 是 SDL 初始化的 Java 端落点：`System.loadLibrary("SDL3")` → `setupJNI()` → `SDLSurface.surfaceChanged()` → `nativeResize()`。

### 1.2 原生侧：ANativeWindow 的落库与分发

```
egl_bridge.c:80-90           Java_..._ZLBridge_setupBridgeWindow(env, clazz, jobject surface)
  :83                        bool windowRecreated = pojav_environ->pojavWindow != NULL;
  :84                        pojav_environ->pojavWindow = ANativeWindow_fromSurface(env, surface);   ★唯一入口
  :87-88                     窗口重建时重放交换间隔（非 RENDERER_VULKAN）
  :89                        if (br_setup_window) br_setup_window();
egl_bridge.c:92-95           Java_..._releaseBridgeWindow → ANativeWindow_release(pojav_environ->pojavWindow)
environ/environ.h:38-41      struct pojav_environ_s { struct ANativeWindow* pojavWindow;
                             basic_render_window_t* mainWindowBundle; int config_renderer; ... }
environ/environ.c:11-26      __attribute__((constructor)) env_init(): POJAV_ENVIRON 存的是结构体指针（跨 .so 共享）
egl_bridge.c:213-235         pojavInit()（渲染线程）:
  :224                       ANativeWindow_acquire(pojav_environ->pojavWindow)
  :225-227                   getWidth/getHeight → ANativeWindow_setBuffersGeometry(..., R8G8B8X8_UNORM)
  :229                       pojavInitOpenGL()
egl_bridge.c:237-260         pojavSetWindowHint(hint,value): hint==GLFW_CLIENT_API(0x22001) 时
  :241                       GLFW_NO_API(0) → config_renderer = RENDERER_VULKAN
  :245-252                   GLFW_OPENGL_API  → 按 POJAV_RENDERER 恢复 GL4ES / VK_ZINK
```

### 1.3 渲染器怎么「拿到」这个 ANativeWindow（三种桥）

`pojavInitOpenGL()` 按 `POJAV_RENDERER` 字符串选桥（**这是全篇的枢纽**）：

```
egl_bridge.c:134-197   pojavInitOpenGL()
  :137-146  !strncmp("opengles", ...)                  → RENDERER_GL4ES   + set_gl_bridge_tbl()
                                                        （opengles3_desktopgl_zink_kopper 额外 load_vulkan +
                                                          GALLIUM_DRIVER=zink + MESA_ANDROID_NO_KMS_SWRAST=1）
  :148-153  "custom_gallium"                           → RENDERER_VK_ZINK + load_vulkan + set_osm_bridge_tbl()
  :155-161  "vulkan_zink"                              → 同上 + GALLIUM_DRIVER=zink
  :163-170  "gallium_freedreno"                        → + MESA_LOADER_DRIVER_OVERRIDE=kgsl + GALLIUM_DRIVER=freedreno
  :172-178  "gallium_panfrost"                         → + GALLIUM_DRIVER=panfrost + MESA_DISK_CACHE_SINGLE_FILE=1
  :180-192  "gallium_virgl"                            → RENDERER_VIRGL + GALLIUM_DRIVER=virpipe + virglInit()
  :194      if (br_init()) br_setup_window();          ★未匹配任何分支时 br_init==NULL → NULL 调用崩溃
```

**（A）GL 桥 —— 用 EGL 把 ANativeWindow 变成 EGLSurface**

```
bridge_tbl.h:35-43    set_gl_bridge_tbl()
gl_bridge.c:59-121    gl_init_context(): eglChooseConfig(EGL_WINDOW_BIT|EGL_PBUFFER_BIT) :68,
                                        eglBindAPI(ES/GL) :97-104, eglCreateContext :111
gl_bridge.c:123-170   gl_swap_surface():
  :132                  bundle->surface = eglCreateWindowSurface_p(g_EglDisplay, bundle->config,
                                                                    bundle->nativeSurface, NULL);   ★EGL 交换链在此
  :166-169              无新窗口时回退 1×1 pbuffer（唯一 pbuffer 分支，且必须"窗口已死"）
gl_bridge.c:172-208   gl_make_current(): :188 mainWindowBundle->newNativeSurface = pojav_environ->pojavWindow
gl_bridge.c:210-233   gl_swap_buffers(): :220 eglSwapBuffers_p(...)；EGL_BAD_SURFACE 时重建
egl_bridge.c:262-276  pojavSwapBuffers() → br_swap_buffers()
egl_bridge.c:278-290  pojavMakeCurrent(window) → br_make_current((basic_render_window_t*)window)
egl_bridge.c:292-300  pojavCreateContext(contextSrc) → br_init_context(...)
```

**（B）OSMesa 桥 —— 没有 EGL、没有 VkSurface，直接渲染进 ANativeWindow 的 CPU 缓冲**

```
bridge_tbl.h:25-33    set_osm_bridge_tbl()
osm_bridge.c:25-38    osm_init_context(): OSMesaCreateContext_p(GL_RGBA, share)   ← 无 EGLDisplay/EGLSurface
osm_bridge.c:40-45    osm_set_no_render_buffer(): 4 字节假缓冲（"没地方画"时用）
osm_bridge.c:47-71    osm_swap_surfaces(): :59 ANativeWindow_acquire; :60 ANativeWindow_setBuffersGeometry(RGBX_8888)
osm_bridge.c:82-88    osm_apply_current_ll(): OSMesaMakeCurrent_p(ctx, buffer->bits, GL_UNSIGNED_BYTE, w, h)  ★写内存
osm_bridge.c:90-117   osm_make_current(): :102 newNativeSurface = pojav_environ->pojavWindow
osm_bridge.c:119-135  osm_swap_buffers(): :126 ANativeWindow_lock(win,&buffer,NULL)
                                          :130 glFinish_p()        ← 把 GL 结果逼进 buffer
                                          :133 ANativeWindow_unlockAndPost(win)
osm_bridge.c:145-149  osm_swap_interval() → setNativeWindowSwapInterval(nativeSurface, i)
osm_bridge.c:26-29（loader） is_renderer_vulkan(): VK_ZINK / VIRGL 才 dlopen libOSMesa
```

**（C）VirGL 桥 —— 渲染在独立进程（vtest server）里，经 unix socket**

```
egl_bridge.c:180-192  loadSymbolsVirGL() + virglInit()
virgl_bridge.c:47-66  loadSymbolsVirGL(): dlopen("$POJAV_NATIVEDIR/libvirgl_test_server.so") → vtest_main
virgl_bridge.c:68-... virglInit(): eglGetDisplay/eglInitialize，随后 vtest_main(3,{...}) 跑 vtest 服务
```

**（D）Vulkan 直连 —— 把 ANativeWindow 原样交给 MC**

```
egl_bridge.c:292-294  pojavCreateContext(): if (config_renderer == RENDERER_VULKAN) return (void*)pojavWindow;
egl_bridge.c:341-355  Java_org_lwjgl_vulkan_VK_updateFps / Java_org_lwjgl_vulkan_VK_getVulkanDriverHandle
  :352-354            → maybe_load_vulkan()（MC/LWJGL 侧问启动器要 Vulkan loader 句柄）
egl_bridge.c:302-308  maybe_load_vulkan(): VULKAN_PTR 缓存
lwjgl_dlopen_hook.c:22-43  ndlopen 覆盖：filename 以 "libvulkan.so" 开头 → 返回 maybe_load_vulkan()
```

即：**MC 26.x 走自己的 Vulkan 后端时，是 MC 自己用「启动器给的 ANativeWindow」调
`vkCreateAndroidSurfaceKHR` + `vkCreateSwapchainKHR`**；ZL2 只是把指针递过去。

### 1.4 当前「MobileGL + panvk」这条路，交换链具体在哪一步被创建

```
插件 manifest（/root/pack_mgl_plugin.sh 实际生成）
  renderer  = opengles3:libMobileGL.so:libMobileGL.so
  pojavEnv  = MOBILEGL_BACKEND_TYPE=DirectVulkan:VK_ICD_FILENAMES=/storage/emulated/0/mali-icd/panvk_icd.json
                    ↓ RendererPluginManager.kt:102-127 → rendererId="opengles3", eglName="libMobileGL.so"
GameLauncher.kt:412  POJAVEXEC_EGL  = libMobileGL.so
GameLauncher.kt:420  SDL_EGL_LIBRARY= <插件lib目录>/libMobileGL.so
GameLauncher.kt:423  POJAV_RENDERER = opengles3
GameLauncher.kt:208-224  ZLBridge.dlopen(<插件目录>/libMobileGL.so)
                    ↓
egl_bridge.c:137-145  "opengles*" → RENDERER_GL4ES + set_gl_bridge_tbl()
egl_loader.c:34-45    dlsym_EGL(): eglName = LIBGL_GLES ?: POJAVEXEC_EGL ?: "libEGL.so" → dlopen("libMobileGL.so")
gl_bridge.c:132       eglCreateWindowSurface(dpy, cfg, pojavWindow)        ← A 路径入口
                    ↓（MobileGL 内部）
MobileGL/.../BackendObject_DirectVulkan.cpp:407  CreateEGLWindowSurface(surface, handle)
MobileGL/.../VulkanRenderer.cpp:14634            vkCreateAndroidSurfaceKHR(m_instance, &sci, ...)
MobileGL/.../VulkanRenderer.cpp:14507            VulkanRenderer::CreateSwapchain()
MobileGL/.../SwapchainObject.cpp:279             vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_swapchain)
```

→ panvk（作为该 loader 的 ICD）**必然**看到 `VK_KHR_android_surface` + 交换链。

---

## 2. 影响 surface 类型 / EGL / Vulkan 配置的可配置项

### 2.1 启动器设置项（`AllSettings.kt`，用户可见）

| 设置 key | 行 | 默认 | 对 surface / EGL / Vulkan 的作用 |
|---|---|---|---|
| `renderer` | `AllSettings.kt:54` | `""` | 选中的**渲染器唯一标识**（内置 UUID 或插件包名）→ 决定 `POJAV_RENDERER` |
| `vulkanDriver` | `:59` | `default turnip` | 驱动插件（`DriverPluginManager`）→ `DRIVER_PATH`（`GameLauncher.kt:185`），**只在 `checkAdrenoGraphics()` 为真时有效** |
| `graphicsApi` | `:64` | `DEFAULT_OPENGL` | 写游戏 `options.txt` 的 `preferredGraphicsBackend`（`GameHandler.kt:118-127`）；`VULKAN` 会让 MC 走 `GLFW_NO_API` |
| `useSurfaceView` | `:94` | **false** | true=SurfaceView（独立合成层），false=TextureView（帧内合成）。**只改承载方式，不改 WSI** |
| `resolutionRatio` / `resolutionRule` / `customResolutionWidth/Height` | `:69,74,79,84` | 100 / PERCENTAGE | 经 `holder.setFixedSize` 或 `SurfaceTexture.setDefaultBufferSize` 改 Surface 缓冲尺寸 |
| `zinkPreferSystemDriver` | `:104` | false | → env `POJAV_ZINK_PREFER_SYSTEM_DRIVER=1`（`Launcher.kt:511`），跳过 turnip 私有加载 |
| `vsyncInZink` | `:109` | false | → env `POJAV_VSYNC_IN_ZINK=1`（`Launcher.kt:512`），影响 `setNativeWindowSwapInterval` |
| `dumpShaders` | `:114` | false | → env `LIBGL_VGPU_DUMP=1`（`Launcher.kt:510`） |
| `sustainedPerformance` | `:99` | false | 仅性能档，与 surface 无关 |

内置渲染器（`Renderers.kt:50-57`）与它们的 `POJAV_RENDERER`/库/桥：

| 渲染器 | `getRendererId()` = POJAV_RENDERER | GL 库 | EGL 库 | 桥 |
|---|---|---|---|---|
| Krypton Wrapper (NGGL4ES) | `opengles3` | `libng_gl4es.so` | – | GL |
| GL4ES | `opengles2` | `libgl4es_114.so` | – | GL |
| Kopper Zink | `opengles3_desktopgl_zink_kopper` | `libglxshim.so` | `libEGL_mesa.so` | GL |
| VirGLRenderer | `gallium_virgl` | `libOSMesa_2121.so` | – | VirGL |
| Freedreno (Adreno) | `gallium_freedreno` | `libOSMesa_8.so` | – | OSMesa |
| **Panfrost (Mali)** | **`gallium_panfrost`** | **`libOSMesa_2300d.so`** | – | **OSMesa** |

（各文件：`game/renderer/renderers/*.kt`）

### 2.2 原生侧读取的全部 env key（`getenv` 全表）

| env key | 读取处 | 语义 |
|---|---|---|
| `POJAV_RENDERER` | `egl_bridge.c:135,246`；`gl_bridge.c:97`；`sdl_hook.c:188` | **选桥 + 选 SDL EGL 兼容策略** |
| `POJAVEXEC_EGL` | `egl_loader.c:40`；`sdl_hook.c:179` | 用哪个库当 EGL（`dlsym_EGL` 的首选） |
| `LIBGL_GLES` | `egl_loader.c:34` | 若非空则**优先**当 EGL 名；`libGLESv2_angle.so`→改用 `libEGL_angle.so` |
| `LIB_MESA_NAME` | `osmesa_loader.c:45` | OSMesa 库文件名（相对 `POJAV_NATIVEDIR`，或绝对路径） |
| `POJAV_NATIVEDIR` | `osmesa_loader.c:46`；`virgl_bridge.c:53` | 启动器 native 库目录 |
| `POJAV_ENVIRON` | `environ.c:12` | 跨 .so 共享的 `pojav_environ` 指针 |
| `VULKAN_PTR` | `egl_bridge.c:306`；`sdl_hook.c:313,325,561,575` | **Vulkan loader 句柄（唯一注入点）** |
| `POJAV_ZINK_PREFER_SYSTEM_DRIVER` | `egl_bridge.c:111` | 跳过 turnip 私有加载 |
| `POJAV_VSYNC_IN_ZINK` | `egl_bridge.c:87,231`；`swap_interval_no_egl.c:227` | vsync 开关 |
| `DRIVER_PATH` | `egl_bridge.c:115` | 驱动插件的库目录（turnip 分支用） |
| `TMPDIR` | `egl_bridge.c:116` | turnip 的 linker 命名空间缓存目录 |
| `LIBGL_ES` | `gl_bridge.c:108` | EGL_CONTEXT_CLIENT_VERSION |
| `OSMESA_NO_FLUSH_FRONTBUFFER` | `egl_bridge.c:187` | VirGL 专用 |
| `POJAV_SDL_REUSE_WINDOW` | `sdl_hook.c:200` | SDL 主窗口复用开关 |
| `POJAV_EMUI_ITERATOR_MITIGATE` | `input_bridge_v3.c:342` | 输入（非渲染） |
| `POJAV_FFMPEG_PATH` | `java_exec_hooks.c:65` | 非渲染 |
| `DALVIK_JAVAVM` / `DALVIK_APPLICATION` | `flite_bridge.c:88-89` | Narrator，非渲染 |

### 2.3 `pojavEnv` → envMap → `setenv` 的完整传递链

```
① 插件清单  <meta-data android:name="pojavEnv" android:value="K=V:K=V"/>
     ↓
② RendererPluginManager.kt:104   val pojavEnvString = metaData.getString("pojavEnv") ?: return   ← 缺了插件直接不认
   RendererPluginManager.kt:111-127  pojavEnvString.split(":") 逐项 split("=")
       "POJAV_RENDERER"            → rendererId 变量（覆盖 renderer[0]）        :117
       "DLOPEN"                    → dlopenList（逗号分隔）                      :118-122
       "LIB_MESA_NAME"/"MESA_LIBRARY" → envList[key] = "$nativeLibraryDir/$value" :123   ★自动补插件 lib 目录
       其它                         → envList[key] = value（字面量，**不补路径**） :124
     ↓
③ RendererPlugin.kt:47   getRendererEnv() = lazy{ env }        （v2：RendererV2Plugin.kt:54 → RendererEnv.getEnv()）
     ↓
④ GameLauncher.kt:409    envMap += renderer.getRendererEnv().value
   GameLauncher.kt:408-436  setRendererEnv 的其它注入（见 2.4）
     ↓
⑤ Launcher.kt:463-478    envMap.forEach { LoggerBridge.appendInfo(...); Os.setenv(key, value, true) }
     ↓
⑥ 原生   getenv("...")   ← 同进程，立即可见；在 JVM 起来之前就设好了
```

⚠ `GameLauncher.kt:425  if (RendererPluginManager.selectedRendererPlugin != null) return`：
**插件渲染器生效时，启动器不再写入 `MESA_LOADER_DRIVER_OVERRIDE` / `LIB_MESA_NAME` / `MESA_GL_*`**
（`GameLauncher.kt:427-436`），这些必须由插件 `pojavEnv` 自己给全。

### 2.4 启动器自己注入的渲染相关 env（不可由用户直接改）

```
GameLauncher.kt:399   SDL_OPENGL_LIBRARY = rendererId        ← 注意是"id"不是库路径（老 MC 桌面 GL 才用）
GameLauncher.kt:401-407  rendererId.startsWith("opengles2") → LIBGL_ES=2 / LIBGL_MIPMAP / LIBGL_NOERROR / ...
GameLauncher.kt:412   POJAVEXEC_EGL = renderer.getRendererEGL()
GameLauncher.kt:420   SDL_EGL_LIBRARY = "<插件或启动器 nativeLib>/<eglName>"   ← SDL 强制用它加载 EGL
GameLauncher.kt:423   POJAV_RENDERER = rendererId
GameLauncher.kt:428   MESA_LOADER_DRIVER_OVERRIDE = "zink"   （仅内置非 GL4ES 渲染器）
GameLauncher.kt:435   LIB_MESA_NAME = getRendererLibrary()   （仅内置非 GL4ES 渲染器）
GameLauncher.kt:229   JVM 参数 -Dorg.lwjgl.opengl.libname=<renderer.getRendererLibrary()>
Launcher.kt:497-508   POJAV_NATIVEDIR / JAVA_HOME / HOME / TMPDIR / LD_LIBRARY_PATH / PATH /
                      AWTSTUB_WIDTH / AWTSTUB_HEIGHT / MOD_ANDROID_RUNTIME / DALVIK_JAVAVM / DALVIK_APPLICATION /
                      ALSOFT_DRIVERS=opensl
Launcher.kt:426-440   LD_LIBRARY_PATH = lwjglNatives : /system/lib64 : /vendor/lib64 : /vendor/lib64/hw :
                      /system_ext/lib64 : <选中渲染器插件的 path> : RUNTIME_MOD : DIR_NATIVE_LIB
                      ← 插件目录排在 /system/lib64 **之后**（对"顶替 libvulkan.so"这类技巧有直接影响）
GameLauncher.kt:185   DRIVER_PATH = 驱动插件 path
GameLauncher.kt:208-224  dlopenEngine(): 先 dlopen 插件 DLOPEN 列表，再 dlopen 渲染器 GL 库
```

### 2.5 v2（JSON）渲染器插件带来的额外可配置项

```
RendererV2PluginManager.kt:56   val configRes = metaData.getStringRes("fclPlugin_V2")   ← 指向 @string 资源
RendererV2PluginManager.kt:63   GLOBAL_JSON.decodeFromString<RendererConfig>(configString)
RendererConfig.kt:18-35         displayName / rendererId / rendererGLPath / rendererEGLPath /
                                dlopenLibPaths / env[] / minMCVer / maxMCVer
RendererConfig.kt:37-106        Env 四种：NormalEnv(固定) / SelectableEnv(下拉+可选开关 check) /
                                CustomizableEnv(用户输入) / ToggleableEnv(开关)
RendererConfig.kt:131-134       "**|" 前缀 = 插件 nativeLibraryDir 的占位符
RendererConfig.kt:139-155       resolveNativePaths(): 只对 GLPath/EGLPath/dlopenLibPaths/Normal/Toggleable 展开
                                （注释明说：Selectable/Customizable 的值"暴露给用户，不支持拼接路径"）
RendererEnv.kt:125-153          getEnv(): 按 MMKV 里保存的用户选择产出最终 map
```
v2 的 `env[]` **同样没有**任何 surface 类型 / 无窗口 语义的专用字段——依旧只能塞上面那批 env key。

---

## 3. 渲染器插件契约：是否有「离屏/无窗口」模式

### 3.1 ZL2 v1 契约的全部 meta-data key（`RendererPluginManager.kt:80-160`）

| key | 必需 | 类型 | 语义 / 行号 |
|---|---|---|---|
| `fclPlugin` | 二者其一 | boolean | `true` 即被识别为 FCL 系渲染器插件（`:88`） |
| `zalithRendererPlugin` | 二者其一 | boolean | ZL2 自己的标记（`:89`） |
| `renderer` | **必需** | string | `"id:GL库:EGL库"`，`split(":")`；`renderer[0]`→`POJAV_RENDERER`（可被 pojavEnv 覆盖），`[1]`→GL 库（`getRendererLibrary()`），`[2]`→EGL 名（`getRendererEGL()`）（`:102,106-108,136-137,162-164`） |
| `des` | **必需** | string | 显示名（`:103`） |
| `pojavEnv` | **必需** | string | `"K=V:K=V"`，逐项 setenv（`:104,111-127`）；缺了直接 `return`（插件被忽略） |
| `minMCVer` / `maxMCVer` | 可选 | version string | 兼容区间（`:134-135`），`Bundle.getVersionString` |
| `boatEnv` | **不支持** | – | 全树 0 命中（FCL 有，见 §4） |
| `fclPlugin_V2` | 另立 | int(string res) | 走 v2 JSON 架构（`RendererV2PluginManager.kt:56`），同包同时声明时 **v2 优先**（`RendererPluginManager.kt:96-100`） |

`isConfigurable` 白名单（硬编码三个包名，决定是否给插件显示"设置"按钮）：
`com.bzlzhh.plugin.ngg` / `com.bzlzhh.plugin.ngg.angleless` / `com.fcl.plugin.mobileglues`（`:141-145`）。

### 3.2 结论：**契约里没有"离屏/无窗口"开关**

- 契约只能改：用哪个 GL/EGL 库、哪些 env、兼容版本、显示名。**没有任何字段描述 surface 类型**。
- 「离屏」在 ZL2 里是**桥的属性**，而桥由 `POJAV_RENDERER` 字符串**硬编码匹配**决定（`egl_bridge.c:134-197`）。
  因此插件要"离屏"，唯一的做法是把自己的 `renderer[0]` 或 `pojavEnv:POJAV_RENDERER` 写成
  `custom_gallium` / `vulkan_zink` / `gallium_freedreno` / `gallium_panfrost` / `gallium_virgl` 之一。
- 而且**没有任何模式是真正"无窗口"的**：
  - OSMesa 桥：渲染器不碰 EGL/Vulkan WSI，但启动器仍然 `ANativeWindow_lock/unlockAndPost`（`osm_bridge.c:126,133`），
    即 **surface 仍在用**，只是"谁去合成"从驱动变成了启动器。
  - VirGL 桥：渲染在 vtest 服务进程里，最终仍要回帖到同一 ANativeWindow。
  - 真正纯软件、完全不碰 Surface 的只有 **AWT/Cacio 安装器路径**（`awt_bridge.c:83` +
    `CTCScreen.getCurrentScreenRGB()`，把 JVM 的画面当 `int[]` 拷出来用 Canvas 画），
    但那只服务 `HandlerType.JVM`（`HandlerType.kt:21-23`），**不是游戏**。
- **红线（实测代码级）**：`renderer[0]` 若**不是**那六个字符串之一，`br_init` 保持 NULL，
  `egl_bridge.c:194 if (br_init())` 就是一次 NULL 函数指针调用 → **启动即崩**。
  （FCL 在 `pojavCreateContext` 加了 `br_init_context == NULL` 兜底（FCL `egl_bridge.c:388`），
  ZL2 的 `egl_bridge.c:299` 没有；两边的 `br_init()` 都没兜底。）

---

## 4. FCL（PojavLauncher 系）与 ZL2 的差异

**有源码**：`/root/fcl`（完整仓库，含 manifest/jniLibs/CMake）。逐条对照：

| 维度 | FCL | ZL2 | 依据 |
|---|---|---|---|
| Surface 入口 | `org.lwjgl.glfw.CallbackBridge.setupBridgeWindow`（**类名不同**） | `com.movtery.zalithlauncher.bridge.ZLBridge.setupBridgeWindow` | FCL `egl_bridge.c:92` / ZL2 `egl_bridge.c:80` |
| `ANativeWindow_fromSurface` | FCL `egl_bridge.c:97` | ZL2 `egl_bridge.c:84` | 同 |
| 承载控件 | **仅 TextureView** | SurfaceView **或** TextureView（`useSurfaceView`） | FCL `JVMActivity.java:47,49,126-131` / ZL2 `VMActivity.kt:880-903` |
| 插件 v1 必需 key | `renderer` + `des` + **`boatEnv`** + `pojavEnv` | `renderer` + `des` + `pojavEnv`（**无 boatEnv**） | FCL `RendererPlugin.kt:94-97` / ZL2 `RendererPluginManager.kt:102-104` |
| `boatEnv` 是否生效 | **否**——全仓库只在解析处出现，`Renderer` 存下来后无人读取 | **不存在** | FCL `Renderer.kt:9`，`grep -rn boatEnv` 结果 |
| `minMCVer`/`maxMCVer` 缺失 | 允许（`?: ""`） | 允许（`getVersionString` 返 null） | FCL `RendererPlugin.kt:102-103` |
| v2 架构 | `fclPlugin_V2` → `@string` JSON，`SelectableEnvV2/CustomizableEnvV2/ToggleableEnvV2/NormalEnvV2`，开关状态键后缀 `@enabled` | `fclPlugin_V2` → `@string` JSON，`NormalEnv/SelectableEnv/CustomizableEnv/ToggleableEnv`，开关状态存 MMKV（`RendererEnv.kt:53` 用 `:check`） | FCL `RendererPlugin.kt:121-247` / ZL2 `RendererV2PluginManager.kt`+`RendererConfig.kt` |
| `pojavEnv` 特殊键 | `LIB_MESA_NAME`/`MESA_LIBRARY` 补插件目录；**`DLOPEN` 在本循环被跳过**（`return`），改由 `setupGraphicAndSoundEngine` 处理 | `POJAV_RENDERER` 覆盖 id；`LIB_MESA_NAME`/`MESA_LIBRARY` 补目录；**`DLOPEN` 立即展开成 dlopen 列表** | FCL `FCLauncher.java:331,334-337,457-477` / ZL2 `RendererPluginManager.kt:117-124` |
| 插件 id 缺省 | 内置渲染器**不设** `POJAV_RENDERER`（v1 插件为空串时才设） | 总是 `envMap["POJAV_RENDERER"] = rendererId` | FCL `FCLauncher.java:319` / ZL2 `GameLauncher.kt:423` |
| 支持的桥集合 | `opengles*` / `gallium_virgl` / `vulkan_zink` / `gallium_freedreno` / `custom_gallium` | 同上 **+ `gallium_panfrost`** | FCL `egl_bridge.c:244-292` / ZL2 `egl_bridge.c:134-194` |
| `custom_gallium` 是否设 `GALLIUM_DRIVER` | 否（要靠 env） | 否（要靠 env） | 两边同 |
| `pojavCreateContext` 兜底 | `if (br_init_context == NULL) return NULL;` | 无 | FCL `egl_bridge.c:387-391` / ZL2 `egl_bridge.c:292-300` |
| Vulkan loader 注入点 | `VULKAN_DRIVER_SYSTEM` / `VKSHIM_ENABLE` → `dlopen("libvkshim.so")`，否则 `dlopen("libvulkan.so")` | **只有** `POJAV_ZINK_PREFER_SYSTEM_DRIVER` 跳过 turnip，然后 `dlopen("libvulkan.so")` | FCL `egl_bridge.c:215-241` / ZL2 `egl_bridge.c:110-132` |
| turnip 私有加载 | `loadTurnipVulkan()` 无参、另有 `vkshim` 垫片（`driver_helper/vkshim/vkshim.c:1-27`，729 行） | 有 `loadTurnipVulkan(NULL, DRIVER_PATH, TMPDIR)`，但 `checkAdrenoGraphics()` 要求 `vendor=="Qualcomm"`，Mali 上恒 false | FCL `vkshim.c:263-289` / ZL2 `driver_helper.c:16-63` |
| SDL hook 范围 | 无 `SDL_EGL_GetProcAddress` hook，无 EGL 函数代理 | **多了** `SDL_EGL_GetProcAddress` hook + `eglChooseConfig/eglCreateContext/eglSwapBuffers` 代理（ES 兼容重试） | FCL `sdl_hook.c:408-446` / ZL2 `sdl_hook.c:264-302,595` |
| `libvulkan.so` 处理 | `vkshim` 用**硬编码** `/system/lib64/libvulkan.so` 避免自递归 → 说明"给垫片起别名"这条路 FCL 已走过 | 无 | FCL `vkshim.c:263-277` |

**公开资料补充（标注）**：FCL 与 ZL2 都源自 PojavLauncher 的 `egl_bridge.c`/`ctxbridges` 设计
（源文件头注释写着 `Created by maks on ...`，ZL2 `sdl_hook.c:1` 明写参考 Amethyst-Android）。
二者在「Surface→渲染器」这一点上**没有架构差异**：都是「Java 拿 Surface → `ANativeWindow_fromSurface`
→ 按 `POJAV_RENDERER` 选 EGL 窗口表面 / OSMesa 内存缓冲」。
本仓库内 FCL 是 1.3.x 之后的重构版（`com.mio.plugin` 新插件系统 + `com.tungsten.fclauncher` 启动链并存），
**结论以本仓库源码为准**，未依赖外部资料。

---

## 5. 结论：能否用 launcher 配置让 panvk 不创建 Android 交换链 / 换一条 surface 路

### 5.1 当前局面（先定位问题）

| 层 | 现状 |
|---|---|
| 启动器 | `POJAV_RENDERER=opengles3` → GL 桥（`egl_bridge.c:137-145`） |
| EGL | `POJAVEXEC_EGL` / `SDL_EGL_LIBRARY` 都指向插件的 `libMobileGL.so`（`GameLauncher.kt:412,420`） |
| GL 桥 | `eglCreateWindowSurface(pojavWindow)`（`gl_bridge.c:132`） |
| MobileGL | `CreateEGLWindowSurface` → `vkCreateAndroidSurfaceKHR` → `CreateSwapchain` → `vkCreateSwapchainKHR`（见 §1.4 行号） |
| panvk | 作为该 loader 的 ICD，**必然**遇到 Android WSI + 交换链 |

### 5.2 ✅ 能：把渲染器切到 OSMesa 桥 → **渲染器侧永不创建 Android 交换链**

只要插件 manifest 里把渲染器 id 换成下列之一，就落到 `set_osm_bridge_tbl()`，
`eglCreateWindowSurface` / `VkSurfaceKHR` / `VkSwapchainKHR` **全部不出现**：

```xml
<!-- 方案 A：Gallium 直连（Mali 上现成可用，ZL2 内置） -->
<meta-data android:name="renderer" android:value="gallium_panfrost:libOSMesa_2300d.so:libOSMesa_2300d.so"/>
<meta-data android:name="pojavEnv" android:value="DLOPEN=libOSMesa_2300d.so"/>

<!-- 方案 B：zink → panvk（才是"panvk 不建交换链"的正解） -->
<meta-data android:name="renderer" android:value="custom_gallium:libOSMesa_zink.so:libOSMesa_zink.so"/>
<meta-data android:name="pojavEnv" android:value="
    GALLIUM_DRIVER=zink:LIB_MESA_NAME=libOSMesa_zink.so:MESA_LOADER_DRIVER_OVERRIDE=zink"/>
```

依据与前提（逐条都可核）：
- `egl_bridge.c:148-153`：`custom_gallium` 只做 `load_vulkan() + set_osm_bridge_tbl()`，
  **自己不设 `GALLIUM_DRIVER`** → 必须由 `pojavEnv` 给（`vulkan_zink` 分支 `:155-161` 才是自带 zink 的那个）。
- `LIB_MESA_NAME` 的值会被自动补上**插件自己的 `nativeLibraryDir`**（`RendererPluginManager.kt:123`），
  所以只写文件名即可；`osmesa_loader.c:42-59` 用它 `dlopen` 出 `OSMesaMakeCurrent` 等符号。
- 呈现由启动器完成：`ANativeWindow_lock` → `OSMesaMakeCurrent(ctx, bits, ...)` → `glFinish` → `unlockAndPost`
  （`osm_bridge.c:126,84,130,133`）。**这条路上没有 EGL，也没有 `vkCreateSwapchainKHR`。**
- 尺寸/几何由 `ANativeWindow_setBuffersGeometry(..., WINDOW_FORMAT_RGBX_8888)` 设定（`osm_bridge.c:60`）。

**代价 / 必须同时满足的条件（诚实标注）**：

1. **需要一个带 zink（或 panfrost）的 `libOSMesa_*.so`。**
   内置的 `libOSMesa_2300d.so`(panfrost) / `libOSMesa_8.so`(freedreno) / `libOSMesa_2121.so`(virgl)
   是**预编译产物**，不在源码树内，无法从本快照确认它们是否包含 zink。
   若要用 panvk，需要一份 `-Dosmesa=true -Dgallium-drivers=zink` 的 Mesa 库 —— 这是**编 Mesa**，
   不是编 panvk（符合"不重编驱动"的字面要求，但不是零编译）。
2. **panvk 仍然要能被找到。** 这条路的 Vulkan 句柄还是 `load_vulkan()` 的 `dlopen("libvulkan.so")`（`egl_bridge.c:129`），
   Mali 上 turnip 分支被 `checkAdrenoGraphics()`（`driver_helper.c:52,62`）挡死 →
   实际用的仍是**系统 loader**，而 doc09 §11 已铁证 Android loader 忽略 `VK_ICD_FILENAMES`。
   ⇒ 切桥只解决"交换链"，**不解决"用上 panvk"**；后者仍需 loader 转发垫片（doc09 §12）。
3. **性能/行为变化**：每帧多一次 CPU 合成与 `lock/unlockAndPost`；`POJAV_VSYNC_IN_ZINK`
   会通过 `osm_swap_interval` → `setNativeWindowSwapInterval`（`osm_bridge.c:145-148`）改变节流行为。
4. **版本区间**：ZL2 给 `gallium_panfrost` 标了 `maxMCVersion = "1.21.4"`（`PanfrostRenderer.kt:30`），
   它对应的是 GLFW 窗口层的老版本 MC。MC 26.3 走 SDL3，走 OSMesa 桥是否能工作属于未知，
   **必须实测**（OSMesa 桥依赖 GLFW 侧 `pojavCreateContext`/`pojavMakeCurrent`，而 SDL3 路径用的是 SDL 自己的 EGL）。

### 5.3 ❌ 不能：保留 `opengles*`（MobileGL DirectVulkan）而"只让 panvk 不建交换链"

逐条否掉所有可能的开关：

| 想用的杠杆 | 为什么不成立 |
|---|---|
| 让 EGL 走 pbuffer / 让 MobileGL 走 windowless 分支 | `gl_bridge.c` 的 config 里**有** `EGL_PBUFFER_BIT`（`:68`），但代码只在"新窗口为空"时用 1×1 pbuffer（`:166-169`）；且 `pojavInit` 无条件 `ANativeWindow_acquire(pojavWindow)`（`egl_bridge.c:224`），**窗口不可能为 NULL**。没有任何设置/env 能强制这条分支。 |
| MobileGL 侧的 windowless | 判决在构造时一次定下：`VulkanRenderer.cpp:2983-2984` `m_presentsToAppWindow = (window != 0)` —— **window 为 0（即应用创建了 EGL pbuffer）才走 offscreen**。`CreateSurface()` 的 headless/AImageReader 分支（`:14537-14597`）与自注释「没有见过移动 ICD 暴露 `VK_EXT_headless_surface`」（`:14548,13160,14568`）都在其后；即便走到，也只是换成 AImageReader 的 `vkCreateAndroidSurfaceKHR` + 交换链。**决定权在启动器的 gl_bridge，不在配置。** |
| `useSurfaceView`（`AllSettings.kt:94`） | SurfaceView/TextureView 都是真 BufferQueue producer，panvk 照样建 Android surface + 交换链。只影响图层合成方式。 |
| 虚拟显示（`android_vdisplay`） | 虚拟屏给 app 的仍是普通 `Surface`（BufferQueue），且没有 HWC → 合成回退。**不消除交换链**，只换显示/刷新语义。 |
| `GraphicsApi.VULKAN`（`VersionConfig.kt:312-328` + `GameHandler.kt:118-127`） | 让 MC 走 `GLFW_NO_API` → `pojavSetWindowHint`（`egl_bridge.c:237-244`）→ `pojavCreateContext` 返回裸 `pojavWindow`（`:294`）→ MC 自己 `vkCreateAndroidSurfaceKHR`。**是加一条交换链，不是去一条。** |
| 换 SDL 的 EGL 库 | `SDL_EGL_LIBRARY` 可由插件 `renderer[2]` 控制（`GameLauncher.kt:411-421`）——这是"让 surface 走另一条 EGL 路"的**唯一真实旋钮**。但替换的 EGL 实现仍必须为 SDL 的窗口请求做 `eglCreateWindowSurface`；想借此绕开交换链，等于要一个"愿意提供 offscreen 窗口 surface 的 EGL"，现实中不存在这样的现成库。 |
| `POJAV_SDL_REUSE_WINDOW` / EGL 代理 / ES 兼容重试 | 只影响窗口复用与上下文属性重试（`sdl_hook.c:199-203,264-302`），与 WSI 无关。 |
| ZL2 的 AWT/Cacio 软件路径 | `awt_bridge.c:83` + `CTCScreen.getCurrentScreenRGB()`：完全无 EGL/Vulkan，但只服务 `HandlerType.JVM`（`HandlerType.kt:21-23`），**游戏不走这条路**。 |

### 5.4 给本项目的可执行建议（按性价比排序）

1. **想验证"panvk 的 Android WSI 是不是卡点"** → 用 §5.2 方案 A（`gallium_panfrost`，零插件改动，
   直接在 ZL2 渲染器列表里选 **Panfrost (Mali)**）。若这条能出画面，说明"不建交换链"确实能绕开问题，
   再投入做 zink 版 `libOSMesa`。
2. **想既用 panvk 又不建交换链** → 需要两件事同时到位：
   ① 一份 `-Dgallium-drivers=zink` 的 `libOSMesa_*.so`（方案 B 的 `LIB_MESA_NAME`）；
   ② 让 zink 找到 panvk —— 仍是 doc09 §11/§12 的 loader 问题（`VK_ICD_FILENAMES` 无效 → 转发垫片）。
   注意：如果最终用 §12 的垫片冒充 `libvulkan.so`，**垫片不要起名 `libvulkan.so.1`**
   （doc09 §9② 的 JVM 卡死事故）；且 ZL2 的 `LD_LIBRARY_PATH` 里 `/system/lib64` 排在插件目录**之前**
   （`Launcher.kt:431` vs `:435`），"靠搜索顺序顶替 libvulkan.so" 在 ZL2 上**是否成立必须先实测**。
3. **不要**尝试在 ZL2 上用一个不在六字符串里的渲染器 id（`egl_bridge.c:194` NULL 调用会崩）。
   若确需新 id，只能：借用 `custom_gallium`（OSMesa 系）或让 id 以 `opengles` 开头（GL 系）。
4. **想确认 SDL3 路径下"谁创建 EGLSurface"** → 看 `SDL_EGL_LIBRARY` 指向的库
   （ZL2 日志里会打印 env，`Launcher.kt:471`），插件场景下即插件的 `renderer[2]`。

### 5.5 与 05/07 号报告的关系（本报告最决策相关的一点）

本仓库 `05-bypass-patch.md` 的目标是「改 Mesa `src/vulkan/runtime/vk_android.c`，让 PanVK 能创建交换链图像」，
`07-mobilegl-wsi.md` 的结论是「MGL(DirectVulkan) 架构上把默认帧缓冲绑死到交换链图像，没有非 WSI 呈现路径」。

本文的 §5.2 给出的是**完全不同的第三条路**，而且它绕开的正是 05 号要补的那个洞：

> 切到 OSMesa 桥之后，`vkCreateAndroidSurfaceKHR` / `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` /
> `vkGetSwapchainImagesKHR` / `vkCreateSwapchainKHR` **一次都不会被调用**（Mesa 侧对应 `vk_android.c` /
> `u_gralloc` 的代码路径根本不进入），画面由 Mesa 渲染进 `ANativeWindow_lock` 得到的**普通 CPU 内存**
> （`osm_bridge.c:126` → `OSMesaMakeCurrent(ctx, bits, ...)` `:84`），启动器再 `unlockAndPost`（`:133`）。

因此：**如果目标是"让 panvk 出画面"而不是"让 panvk 支持 Android 交换链"，那么 §5.2 的方案 B
（zink → panvk，OSMesa 前端）在原理上不需要动 Mesa 的 `vk_android.c`，也不需要 gralloc/IMapper**
（zink 在 displaytarget 路径下分配的是普通 `VkImage`，再 `vkCmdCopyImageToBuffer` 拷进内存缓冲）。
剩下要解决的是两件与 WSI 无关的事：
① 一份 zink-enabled 的 `libOSMesa_*.so`（编 Mesa，但不改 Mesa 源码）；
② 让 zink 找到 panvk —— 仍是 loader 垫片问题（doc09 §11/§12）。
反过来，若性能不可接受（每帧 CPU 合成），05 号补丁路线才是唯一能保住"驱动直出显示"的方案。
**两条路互斥，建议先用 §5.4 的第 1 步（选 Panfrost (Mali)）花最小代价判断"不建交换链能不能出画面"。**

---

## 附：本文所有 file:line 的一览（便于复核）

**ZL2 源码根 = `/root/zl2src/ZalithLauncher2-main`**

```
ZalithLauncher/src/main/jni/egl_bridge.c          80,84,92,94,104,110,111,115,116,129,134,137,145,148,152,
                                                  155,159,163,167,172,175,180,187,194,213,224-229,231,237,
                                                  241,245-252,262-276,278-290,292-294,299,302-308,341-355,357-371
ZalithLauncher/src/main/jni/ctxbridges/gl_bridge.c        23-40,59-121(68,97,108,111),123-170(132,166-169),
                                                          172-208(188),210-233(220),235-242,244-246,248-263
ZalithLauncher/src/main/jni/ctxbridges/osm_bridge.c        16-19,25-38,40-45,47-71(59,60,67),73-80,82-88(84),
                                                          90-117(102),119-135(126,130,133),137-143,145-149
ZalithLauncher/src/main/jni/ctxbridges/bridge_tbl.h        16-22,25-33,35-43
ZalithLauncher/src/main/jni/ctxbridges/common.h            11-15
ZalithLauncher/src/main/jni/ctxbridges/renderer_config.h   7-10,17-23
ZalithLauncher/src/main/jni/ctxbridges/egl_loader.c        31-50(34,40,45,48)
ZalithLauncher/src/main/jni/ctxbridges/osmesa_loader.c     26-29,42-59(45,46,54)
ZalithLauncher/src/main/jni/ctxbridges/virgl_bridge.c      47-66,68-...
ZalithLauncher/src/main/jni/ctxbridges/br_loader.c         14-48
ZalithLauncher/src/main/jni/ctxbridges/swap_interval_no_egl.c  227
ZalithLauncher/src/main/jni/sdl_hook.c            178-183,187-195,199-203,264-302,311-322,334-358,360-365,
                                                  368-396,414-421,439-471,583-602,605-643
ZalithLauncher/src/main/jni/sdl_dlopen_hook.c     31-51
ZalithLauncher/src/main/jni/lwjgl_dlopen_hook.c   22-43(29-32)
ZalithLauncher/src/main/jni/driver_helper/driver_helper.c  16-59(52),61-106(62,70,71)
ZalithLauncher/src/main/jni/environ/environ.h     38-41
ZalithLauncher/src/main/jni/environ/environ.c     11-26
ZalithLauncher/src/main/jni/awt_bridge.c          83-120
ZalithLauncher/src/main/jni/Android.mk            39-43,53-55,76-79
ZalithLauncher/src/main/java/.../bridge/ZLBridge.java                     70,71
ZalithLauncher/src/main/java/.../game/launch/handler/GameHandler.kt       94,118-127
ZalithLauncher/src/main/java/.../game/launch/handler/HandlerType.kt       21-23
ZalithLauncher/src/main/java/.../ui/activities/VMActivity.kt             356,631,739-765,795-817,880-903
ZalithLauncher/src/main/java/.../game/sdl/SdlBridge.kt                   62-69,101-115,148-160
ZalithLauncher/src/main/java/org/libsdl/app/SDLSurface.java              115-122,131-135,142-158
ZalithLauncher/src/main/java/org/libsdl/app/SDLActivity.java             352-374
ZalithLauncher/src/main/java/org/lwjgl/glfw/CallbackBridge.java          91-133(111-117)
ZalithLauncher/src/main/java/.../game/launch/Launcher.kt                 426-440(431,435),463-478(473),490-516
ZalithLauncher/src/main/java/.../game/launch/GameLauncher.kt             182-206(185),208-224(221),229,
                                                                         394-453(399,401-407,409,412,420,423,425,428,435)
ZalithLauncher/src/main/java/.../setting/AllSettings.kt                  54,59,64,69,74,79,84,94,99,104,109,114
ZalithLauncher/src/main/java/.../game/plugin/renderer/RendererPluginManager.kt  85-90,96-100,102-104,106-108,
                                                                                111-127,134-137,141-145,162-164
ZalithLauncher/src/main/java/.../game/plugin/renderer/RendererPlugin.kt  38-52
ZalithLauncher/src/main/java/.../game/plugin/renderer_v2/RendererV2PluginManager.kt  52-57,63-84,97-101
ZalithLauncher/src/main/java/.../game/plugin/renderer_v2/RendererV2Plugin.kt         48-57
ZalithLauncher/src/main/java/.../game/plugin/renderer_v2/data/RendererConfig.kt      18-35,37-106,113-128,131-155
ZalithLauncher/src/main/java/.../game/plugin/renderer_v2/data/RendererEnv.kt         27-153(53,125-153)
ZalithLauncher/src/main/java/.../game/renderer/Renderers.kt              50-57
ZalithLauncher/src/main/java/.../game/renderer/renderers/*.kt            各文件的 getRendererId/Library/EGL/Env
ZalithLauncher/src/main/java/.../game/version/installed/VersionConfig.kt 312-328
```

**FCL 源码根 = `/root/fcl`**

```
FCL/src/main/jni/egl_bridge.c                  92,97,205-241(228-235),244-292(251,260,263,271,275,278,283,
                                               286,289,292),324,332-350,381-392(388),394-405
FCL/src/main/jni/native_hooks/sdl_hook.c        408-446（hook 清单，无 SDL_EGL_GetProcAddress）
FCL/src/main/jni/driver_helper/vkshim/vkshim.c  1-27,263-289
FCL/src/main/jni/ctxbridges/osm_bridge.c        与 ZL2 同源
FCL/src/main/java/com/mio/plugin/RendererPlugin.kt  18-119(96,100-103),121-247,255-312
FCL/src/main/java/com/mio/plugin/PluginManager.kt   36,39
FCL/src/main/java/com/mio/data/Renderer.kt          9,10,14
FCL/src/main/java/com/tungsten/fclauncher/FCLauncher.java  262,281-312(284,296,303),315-390(319,325-330,
                                                           334-337,352-383),457-477(461,465),399
FCL/src/main/java/com/tungsten/fcl/activity/JVMActivity.java  47,49,126-131
```

**MobileGL 源码根 = `/root/MobileGL`**

```
MobileGL/MG_Backend/DirectVulkan/BackendObject_DirectVulkan.cpp   407,437
MobileGL/MG_Backend/DirectVulkan/Renderer/VulkanRenderer.cpp      14507,14537-14597(14548,14554-14597),
                                                                  14634,14751,14883,2983-2984,13090,13160,13177,13185
MobileGL/MG_Backend/DirectVulkan/Renderer/VulkanRenderer.h        522,526,534,540
MobileGL/MG_Backend/DirectVulkan/Renderer/SwapchainObject.cpp     279
tools/cts/platform/tcuMobileGLPlatform.cpp                        20-45(33-37,42)
```
