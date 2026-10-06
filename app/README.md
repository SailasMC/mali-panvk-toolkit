# GPU 驱动测试台（Android App）

一个**独立、免 root、不依赖 Shizuku** 的真机 GPU 驱动测试软件。

## 它做什么

| 功能 | 实现 |
|---|---|
| ① 扫描驱动 | 扫已安装插件包的 `fclPlugin` / `driver` meta-data（启动器同款约定）+ 扫本地 `.so` |
| ② 从 APK 提取驱动 | `java.util.zip` 打开 APK，取 `lib/**` 里最大的 `.so`，落到 App 外部目录并自动选中 |
| ③ 安装驱动插件 APK | 文件选择器 → 系统安装器 |
| ④ 驱动自测（原生） | `dlopen` 选中 ICD → 协商接口版本 → `vkCreateInstance` → 枚举设备并打印设备名/类型/API/驱动版本 |
| ⑤ 三角形绘制 + 像素校验（原生） | 建渲染目标 + 图形管线（着色器内嵌 SPIR-V），画三角形后回读像素，判定「纯红 / 有异常像素 / 无输出」 |
| ⑥ 跑分（原生） | 填充率基准：1 Mpixel 图像批量清屏，输出 Mpixel/s、提交吞吐、提交往返、失败次数与分数 |
| ⑦ 拉起启动器 | 自动发现 ZalithLauncher2 / FCL / Pojav 并拉起 |
| ⑧ 导出 / 分享日志 | 落盘到 App 外部目录并调起分享 |

## 关键实现要点（都是踩过的坑）

- **不依赖系统 Vulkan loader**：直接 `dlopen` 驱动 `.so` 并调 `vk_icdGetInstanceProcAddr` / `vk_icdNegotiateLoaderICDInterfaceVersion`。
- **驱动必须先拷进 App 私有目录**才能 `dlopen` —— `/sdcard` 不可执行。
- 已安装插件的 `.so` 通过 `ApplicationInfo.nativeLibraryDir` 定位（可读）。
- 着色器用**内嵌 SPIR-V 数组**，顶点着色器靠 `gl_VertexIndex` 生成全屏三角形，**不需要顶点缓冲**。

## 免 Gradle 构建

```bash
# 在装有 Android SDK/NDK 的机器上
bash build_app.sh
```
流水线：`javac --release 11` → `d8` → NDK `clang` 编 `libgputest.so` →
`aapt2 link` → 装 `classes.dex` 与 `lib/arm64-v8a/libgputest.so` → `zipalign` → `apksigner`。

> 注意：`javac` 必须指定 `--release`（新版 JDK 默认产出 class 版本过高，`d8` 读不了）。

## 产物

`GPUTest-0.2.apk`（29 KB）：已实测可在 OPPO PHZ110（天玑 9300 / Immortalis-G720 / Android 16 / 无 root）安装运行。
