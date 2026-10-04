# 构建 G720/v12 的 PanVK（kbase 后端 + Android WSI）

> 目标产物：`libvulkan_panfrost.so` —— 一个面向 **mali_kbase**、支持 **Android 窗口系统**、
> 覆盖 **G720（panArch 12 / 5th Gen CSF）** 的 PanVK 驱动。
>
> 本文档记录在 **Debian 13 / x86_64 / 2 核 VPS** 上从零到产出的完整流程，
> 以及**所有踩过的依赖坑**（这些坑花掉了大半天，列在这里让你 30 分钟走完）。

---

## 1. 为什么用 zenithblue 的仓库做基线

Mesa 主线虽然已实现 CSF 提交，但「在 Android 上、走 **kbase**（而不是 panthor）、
并带窗口系统支持」这一整套补丁，目前最完整的是
[zenithblue-oss/panvk-kbase-android](https://github.com/zenithblue-oss/panvk-kbase-android)：

- 它把 Mesa pin 在特定 commit，并带一个**带校验 id 的补丁序列**（`sources.lock` 里有 `patchSeriesId`）
- 补丁分族：`android` / `kbase-common` / `app-loader` / `wsi` / `csf` / `csf-v11` / `jm-v9` …
- 有现成的 **profile 文件**，其中就包括 `profiles/g720-v12-csf.json`
- `sources.lock` 里把 [wonderkast02/panvk-g720-kbase-csf](https://github.com/wonderkast02/panvk-g720-kbase-csf)
  列为 **G720 参考实现**（CSF 生命周期、AHB、sync、tessellation）

```
$ cat profiles/g720-v12-csf.json
{ "profile": "g720-v12-csf", "gpu": "Mali-G720",
  "panArch": 12, "frontend": "CSF", "kernelInterface": "mali_kbase",
  "reference": "https://github.com/wonderkast02/panvk-g720-kbase-csf" }
```

> ⚠️ 注意它的 `status` 字段是 **`planned`** —— 作者尚未在 G720 真机上验证过这条 profile。
> 也就是说：**你正在当这个 profile 的第一个测试者**。

## 2. 一次性环境准备

```bash
apt-get install -y --no-install-recommends \
  git ninja-build meson pkg-config ccache python3 python3-pip python3-mako python3-yaml \
  bison flex gettext g++ make \
  clang llvm-dev llvm-19-dev libclang-19-dev libclang-cpp19-dev \
  libdrm-dev libudev-dev libexpat1-dev libzstd-dev zlib1g-dev \
  libx11-dev libxext-dev libxrandr-dev libxfixes-dev libx11-xcb-dev \
  libxcb1-dev libxcb-randr0-dev libxcb-shm0-dev libxcb-glx0-dev libxcb-present-dev \
  libxcb-sync-dev libxcb-dri3-dev libxcb-xfixes0-dev libxshmfence-dev \
  glslang-tools spirv-tools
```

Android NDK（r27c 及以上均可，profile 用的是 API 35）：

```bash
# 从 https://developer.android.com/ndk/downloads 取 linux-x86_64 版
# 解压到 /opt/android-ndk-r27c
```

## 3. 构建三步

```bash
git clone https://github.com/zenithblue-oss/panvk-kbase-android.git
cd panvk-kbase-android

./scripts/bootstrap-host-tools.sh          # 1) 主机侧代码生成工具（mesa_clc 等）
./scripts/apply-patches.sh --profile g720-v12-csf   # 2) 打补丁序列
./scripts/build-android.sh --profile g720-v12-csf \
    --api 35 --ndk /opt/android-ndk-r27c   # 3) 编 Android 驱动
```

产物：`dist/android-g720-v12-csf/libvulkan_panfrost.so`

> **若中途失败**：`apply-patches.sh` 会因工作区脏而报 `PATCH-DRIFT`。
> 先复位：
> ```bash
> git -C work/mesa reset --hard $(python3 -c "import json;print(json.load(open('sources.lock'))['mesaCommit'])")
> git -C work/mesa clean -fdq -- src/ meson.build meson.options
> ```
> （实测中，前几次失败的构建会在 Mesa 树里留下 **45 个**改动文件，正是 `PATCH-DRIFT` 的原因。）

---

## 4. ★ 依赖踩坑清单（照单抓药可省半天）

按我们实际遇到的顺序：

| # | 报错 | 原因 | 解法 |
|---|---|---|---|
| 1 | `Dependency "libdrm" not found` | 没装 `libdrm-dev` | `apt install libdrm-dev` |
| 2 | `Neither a subproject directory nor a llvm.wrap file` + `llvm-config` 报 `missing: /usr/lib/llvm-19/lib/libLLVM*.a` | Debian 的 LLVM 包缺可选静态库；实际是 dev 包不全 | `apt install llvm-dev llvm-19-dev libllvm19 libclang-19-dev libclang-cpp19-dev`；必要时用 `ar rcs` 造空库补齐 llvm-config 的检查 |
| 3 | `Dependency "LLVMSPIRVLib" not found` | 缺 SPIR-V 翻译器 | `apt install libllvmspirvlib-19-dev` |
| 4 | `need host libxcb, libx11 and libxshmfence headers under /usr/include` | 检查的是 `X11/Xlib-xcb.h`，它在 **`libx11-xcb-dev`** 里（不在 `libx11-dev`）| `apt install libx11-xcb-dev` |
| 5 | `'xf86drm.h' file not found`（NDK 编译**目标**代码时）| Debian 把 `xf86drm.h` 放在 `/usr/include/` 而不是 `/usr/include/libdrm/`，而 **NDK 交叉编译器用自己的 sysroot，看不到宿主 `/usr/include`** | 建软链：`ln -sf ../xf86drm.h /usr/include/libdrm/xf86drm.h`（`xf86drmMode.h` 同理）|
| 6 | `'X11/Xlib.h' file not found`（同上）| `build-android.sh` 只在 `work/android-deps/lib/pkgconfig` **存在**时才把 X11 的 pkg-config 写进交叉文件；该目录不存在 → 走了宿主 pc 文件，返回的路径对 NDK 无效 | 在交叉文件模板 `meson/android-aarch64.ini` 的 `[properties]` 里加：<br>`c_args = ['-I<repo>/work/android-deps-x11/include']` |
| 7 | `'spirv-tools/libspirv.h' file not found`（同上）| 同 5/6：宿主头文件对 NDK 不可见 | 把需要的子目录软链进上面那个 include 目录：<br>`ln -s /usr/include/spirv-tools <repo>/work/android-deps-x11/include/spirv-tools` |

> ⚠️ **不要图省事直接给 NDK 加 `-I/usr/include`** —— 那会把 glibc 头文件
> 插到 bionic 头文件前面，污染整个交叉编译。
> 正确做法是把**缺的那个子目录**软链进自己的 deps include 目录。

### 改了交叉文件后必须重新 configure

`meson setup --reconfigure` **不会**重新读取交叉文件！实测：改了 `c_args` 后
`--reconfigure` 依旧报同样的错。用**新的构建目录**最省事：

```bash
export BDIR=$PWD/build/android-v2        # build-android.sh 支持 BDIR 覆盖
export DDIR=$PWD/dist/android-g720-v12-csf
./scripts/build-android.sh --profile g720-v12-csf --api 35 --ndk /opt/android-ndk-r27c
```

---

## 5. 验证产物

```bash
# 形态与能力（不需要设备）
aarch64-linux-android26-clang -O2 -o vkicd_probe ../tools/vkicd_probe.c -ldl
./vkicd_probe dist/android-g720-v12-csf/libvulkan_panfrost.so
```

在真机上验证（免 root）：

```bash
# 推到设备后
adb push libvulkan_panfrost.so /data/local/tmp/
adb shell ./data/local/tmp/vkicd_probe /data/local/tmp/libvulkan_panfrost.so
```

期望看到：`deviceName: Mali-G720 ...`、`apiVersion 1.4.x`、以及
`VK_KHR_swapchain` / `VK_KHR_android_surface` / `VK_ANDROID_external_memory_android_hardware_buffer`。

## 6. 两种驱动形态，别搞混

| 形态 | 导出符号 | 用途 |
|---|---|---|
| **ICD** | `vk_icdGetInstanceProcAddr` 等 | 给 `VK_ICD_FILENAMES` / Vulkan loader 直接加载 |
| **HAL** | `HMI` | 给启动器的「驱动注入」路径：它按 **HAL** 换掉厂商的 `libvulkan.<vendor>.so` |

**FCL / Zalith Launcher 的自定义驱动走的是 HAL 形态**（`linker_ns_dlopen` + 系统加载器）。
`tools/vkicd_probe.c` 会把两者都列出来，照着选。

---

## 附录：整包二进制补丁的注意点

如果你选择「给启动器整包打补丁并重签名」这条路：

1. 只改 `lib/arm64-v8a/libpojavexec.so` 的 8 个字节；
2. 重打包时**去掉旧签名**（`META-INF/*.SF`、`*.RSA`、`*.MF`），并让
   **`resources.arsc` 保持未压缩且 4 字节对齐** —— Android 11+ 安装时会强制检查；
   `zipalign` 会做这件事，但很多手机上跑不了（`/storage` 是 `noexec`，且没有 aarch64 版 zipalign），
   可以自己在 zip 写入时用 **extra field 填充** 来实现对齐；
3. 重签名换了证书 → **必须卸载原版**才能安装 → 应用内设置会丢失
   （游戏存档通常在共享目录，不受影响）。
