# NOTICE — 第三方来源与许可

本仓库**只包含原创的工具、脚本和文档**，不包含下列项目的任何二进制产物。
使用者在按本仓库文档操作时，会自行获取这些上游项目，需遵守其各自许可。

## 上游项目

| 项目 | 用途 | 许可 |
|---|---|---|
| [Mesa3D](https://gitlab.freedesktop.org/mesa/mesa)（PanVK） | 开源 Mali Vulkan 驱动本体 | MIT；内核 uAPI 头文件部分为 GPL-2.0 WITH Linux-syscall-note |
| [zenithblue-oss/panvk-kbase-android](https://github.com/zenithblue-oss/panvk-kbase-android) | PanVK 的 mali_kbase 后端 + Android/W11 WSI 补丁序列；本仓库构建配方来源 | 见其仓库 |
| [wonderkast02/panvk-g720-kbase-csf](https://github.com/wonderkast02/panvk-g720-kbase-csf) | G720 / v12 CSF 参考实现 | 见其仓库 |
| [FCL-Team/FoldCraftLauncher](https://github.com/FCL-Team/FoldCraftLauncher) | 目标启动器 | **GPL-3.0** |
| [ZalithLauncher/ZalithLauncher2](https://github.com/ZalithLauncher/ZalithLauncher2) | 目标启动器 | **GPL-3.0** |
| [PojavLauncher](https://github.com/PojavLauncherTeam/PojavLauncher) | 上述两者的上游 | LGPL-3.0 / GPL-3.0 |
| [LWJGL](https://www.lwjgl.org/) | Java 层 GL/Vulkan 绑定 | BSD-3-Clause |
| Android NDK / SDK build-tools | 交叉编译与打包 | Android SDK 许可 |

## 关于 FCL / ZL2 的 GPL-3.0

FCL 与 Zalith Launcher 2 均为 **GPL-3.0**。因此：

- ✅ 本仓库发布的是**我们自己的插件代码与补丁说明**（原创，MIT）；
- ✅ 文档里描述「如何对你自己设备上已安装的库打字节补丁」，属于**操作说明**；
- ❌ 本仓库**不**分发修改过的启动器 APK，也不分发启动器的任何二进制；
  如果你打算分发修改版启动器，必须同时提供完整对应源码并遵守 GPL-3.0。

同理，PanVK 驱动本体（`.so`）也不随本仓库分发 —— 请自行按
[`04-build-g720-panvk.md`](docs/04-build-g720-panvk.md) 构建，或从上游发布页获取。

## 商标

Mali、Immortalis 是 Arm Limited 的商标；Adreno 是高通的商标；
Qualcomm、MediaTek、OPPO 等为其各自持有者的商标。
本项目与上述公司无任何关联，也未获其授权或认可。
