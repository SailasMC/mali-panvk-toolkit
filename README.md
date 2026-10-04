# mali-panvk-toolkit

**在免 root 的 Android 设备上，让 Mali GPU 用上开源驱动（Mesa PanVK / kbase 后端），并把它接进 FCL / Zalith Launcher。**

本仓库是这条路走通（以及走不通）的全过程沉淀：探测工具、诊断工具、打包工具、关键发现、构建配方。
所有工具都在 **OPPO PHZ110 / 天玑 9300 / Immortalis-G720 MC12 / Android 16 (SDK 36) / 无 root** 上实测过。

---

## 一句话结论

> **免 root 的普通 App 可以完整驱动 Mali kbase 内核接口**（uAPI 1.21 / CSF），
> 从而加载并运行 Mesa PanVK。**但 FCL / Zalith Launcher 都用 `checkAdrenoGraphics()`
> 硬编码只认高通 Adreno**，所以驱动即使能用也不会被加载 —— 本仓库给出了拆掉这把锁的
> 可发布方案（原生库插件覆盖 `libpojavexec.so`，不改不重签启动器）。

---

## 目录

| 路径 | 内容 |
|---|---|
| [`tools/kbase_probe.py`](tools/kbase_probe.py) | 免 root 探测设备是否具备 kbase 通路（节点可开、握手、GPU 属性） |
| [`tools/kbase_selftest.c`](tools/kbase_selftest.c) | kbase 全链路自检：握手 → 属性 → 内存分配 → mmap 读写校验 → 释放 |
| [`tools/vkicd_probe.c`](tools/vkicd_probe.c) | 任意驱动 `.so` 冒烟测试：ICD/HAL 形态判定、实例/设备/扩展枚举 |
| [`tools/zink_check.c`](tools/zink_check.c) | 把驱动的特性/扩展和 **Zink 的需求**逐条对照 |
| [`tools/wsiprobe.c`](tools/wsiprobe.c) | 窗口系统扩展探测（`VK_KHR_android_surface` 等，Zink 出图必需） |
| [`tools/make_driver_apk.sh`](tools/make_driver_apk.sh) | 把驱动 `.so` 打包成 FCL / ZL2 可识别的驱动插件 APK |
| [`tools/vkshim_icd.c`](tools/vkshim_icd.c) | `libvulkan.so` 转发垫片 —— **记录一条已证明走不通的路**（见下文） |
| [`docs/03-fcl-adreno-lock.md`](docs/03-fcl-adreno-lock.md) | ⭐ FCL / ZL2 的 Adreno 厂商锁分析与拆锁方案 |
| [`docs/04-build-g720-panvk.md`](docs/04-build-g720-panvk.md) | 构建 G720/v12 PanVK（zenithblue 基线）与完整依赖踩坑清单 |
| [`fcl-patch/`](fcl-patch/) | 拆锁插件的一键构建脚本 |

---

## 快速上手

### 1. 我的设备能用开源 Mali 驱动吗？

```bash
python3 tools/kbase_probe.py          # 只看有没有通路（秒级）
```

想彻底验证，编译自检工具（需 NDK）：

```bash
aarch64-linux-android26-clang -O2 -o kbase_selftest tools/kbase_selftest.c
./kbase_selftest                       # 全部 PASS 才说明能真正驱动 GPU
```

判据：`/dev/mali0` **普通应用可打开**（mode `0666`）+ 握手 ioctl 返回成功。
`/dev/dri/*` 需要 root，**不是**免 root 路线。

### 2. 拿到一个驱动 `.so`，先验它能不能用

```bash
aarch64-linux-android26-clang -O2 -o vkicd_probe tools/vkicd_probe.c -ldl
./vkicd_probe /path/to/libvulkan_xxx.so
```

输出会告诉你：能不能 `dlopen`、导出的是 `vk_icd*`（ICD 形态）还是 `HMI`（HAL 形态）、
GPU 名字、API 版本、扩展清单。

### 3. 打包成启动器认识的驱动插件

```bash
bash tools/make_driver_apk.sh ./libvulkan_freedreno.so "My PanVK" mydrv
# → 产出签好名的 APK，装到设备上即可被 FCL / ZL2 的驱动选择器看到
```

---

## 关键发现（踩坑记录）

### ⚠️ 坑 1：启动器的 Adreno 厂商锁

FCL 与 Zalith Launcher（同源）在原生层都有：

```c
bool checkAdrenoGraphics() {
    // ...
    bool is_adreno = (vendor && renderer
                      && strcmp(vendor, "Qualcomm") == 0
                      && strstr(renderer, "Adreno") != NULL);
    return is_adreno;
}
```

自定义驱动注入路径 `loadTurnipVulkan()` **第一步就是这个检查**，Mali 上必然为假：

```c
void* loadTurnipVulkan(...) {
    if (!checkAdrenoGraphics()) return NULL;   // ← Mali 永远从这里返回
    ...
}
```

结果：走回系统 `libvulkan.so`（厂商 blob），插件里的驱动**根本不会被加载**。
详细分析、日志证据与三种拆锁方案见 [`docs/03-fcl-adreno-lock.md`](docs/03-fcl-adreno-lock.md)。

### ⚠️ 坑 2：`LD_LIBRARY_PATH` 在进程启动后无效

启动器运行时 `setenv("LD_LIBRARY_PATH", ...)` 想让自己插件里的 `libvulkan.so`
盖过系统的 —— **无效**。Android 的 linker **只在进程启动时**读取该变量。
这条路（本仓库 `tools/vkshim_icd.c` 就是为它写的）已实测证明不通。

### ⚠️ 坑 3：驱动插件必须有 launcher Activity

FCL 用 `queryIntentActivities(Intent(ACTION_MAIN))` 扫描插件。
APK 里只有 `<meta-data name="fclPlugin">` 而没有带 `LAUNCHER` 的 Activity，
**启动器根本看不到它**。

### ⚠️ 坑 4：`environment` meta-data 是 JVM 参数，不是环境变量

原生库插件的 `environment` 里写 `PANVK_SHIM=1` 会变成 JVM 参数 `PANVK_SHIM=1`
→ `ClassNotFoundException`。要写成 `-Dname=value`。

### ⚠️ 坑 5：驱动形态要匹配加载方式

- 启动器按 **HAL** 加载：`linker_ns_dlopen` + 系统加载器 → 需要 `HMI` 导出
- 按 **ICD** 加载：需要 `vk_icd*` 导出

两者不通用。`tools/vkicd_probe.c` 可以一眼看出是哪种。

---

## 实测环境（可作为对照基线）

| 项 | 值 |
|---|---|
| 设备 | OPPO PHZ110，Android 16 (SDK 36) |
| SoC | 联发科天玑 9300（MT6989） |
| GPU | Immortalis-G720 MC12，`PRODUCT_ID 0xc870`，1.3 GHz |
| 内核 | 6.1.157-android14 |
| kbase | `/dev/mali0`（0666，免 root 可开），CSF，**uAPI 1.21** |
| 内存模型 | `RAW_JS_PRESENT=0`、`RAW_COHERENCY_MODE=0`（非一致） |
| L2 | 512 KB / 4 slices |

---

## 相关项目与致谢

- [zenithblue-oss/panvk-kbase-android](https://github.com/zenithblue-oss/panvk-kbase-android) —— PanVK kbase 后端基线（本项目构建配方的来源）
- [wonderkast02/panvk-g720-kbase-csf](https://github.com/wonderkast02/panvk-g720-kbase-csf) —— G720 / v12 CSF 参考实现
- [Mesa3D](https://gitlab.freedesktop.org/mesa/mesa) —— PanVK 本体
- [Fold Craft Launcher](https://github.com/FCL-Team/FoldCraftLauncher) / [Zalith Launcher 2](https://github.com/ZalithLauncher/ZalithLauncher2) —— 本项目的目标启动器

## 许可

工具与文档：**MIT**（见 [LICENSE](LICENSE)）。
第三方来源与各自许可见 [NOTICE.md](NOTICE.md)。

> 注意：不附带任何 FCL / ZL2 的二进制产物，也不附带 PanVK 驱动本体。
> 本仓库只提供**工具、脚本、文档**，由使用者自行获取上游产物。
