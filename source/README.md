# source/ —— 源码与脚本（我们的转发垫片 + 打包链）

> **范围**：本目录只收**我们自己写的**关键源码与脚本 —— 让 MobileGL 用上自编 PanVK 的
> **Vulkan 转发垫片**、它的**生成器**、以及**打包 / 校验**脚本与**插件清单模板**。
>
> **不收**：第三方或厂商的二进制（`libMobileGL.so`、`libvulkan_panfrost.so`、APK）——
> 那些只登记在 [`../MANIFEST.md`](../MANIFEST.md)。
>
> **来源注记**：每个文件都在下表中标注了它在服务器上的**原始路径**与**用途**，
> 内容为**原样收录**（未改动）。哈希（`sha256` 前 16 位）由本仓库实测，用于核对"仓库里的就是当时用的那份"。
>
> 设计动机、生成器踩过的两个坑、以及 125/123 这些数字的来龙去脉，见
> [`../docs/09-mobilegl-integration.md`](../docs/09-mobilegl-integration.md) §10–§12、§15、§17、§18–§20，
> 以及综述论文 [`../research/paper.md`](../research/paper.md) §3.2–§3.4。

---

## 1. `shim/` —— Vulkan 转发垫片（本项目最关键的一段代码）

**它是什么**：Android 的系统 loader **忽略** `VK_ICD_FILENAMES`（[`../docs/09` §11](../docs/09-mobilegl-integration.md)），
而我们的 PanVK 是**纯 ICD**（只导出 `vk_icd*`），MGL 却在链接期直接引用 **125 个** loader 风格 `vk*` 入口
（[`../docs/09` §10](../docs/09-mobilegl-integration.md)）。
所以需要一个**导出这 125 个名字、内部 `dlopen` 我们的 ICD 并逐一分发**的转发层。
最终它是**静态链进 MGL 本体**的（UND `vk*` = 0，[`../docs/09` §18.1](../docs/09-mobilegl-integration.md)）。

| 文件 | 原始路径 | 大小 (B) | sha256(前16) | 来源与用途 |
|---|---|---|---|---|
| [`vkshim_mgl.c`](shim/vkshim_mgl.c) | `/root/mgl_icd/vkshim_mgl.c` | 64,413 | `792baae1c364d780` | **最终**转发垫片源码（注释："自动生成（修好正则 + 设备缓存版）—— 唯一名 `libvkpanvk_shim.so`"）。内含：`shim_init()` 用 `dladdr()` 求自身目录 → `dlopen` ICD → `vk_icdNegotiateLoaderICDInterfaceVersion(&7)` → 取 `vk_icdGetInstanceProcAddr`；`gipa_pd()`（**物理设备级优先**）；`gp_inst`/`gp_dev` 多源回退；`FLOG` **双写** logcat + `/sdcard/MG/vkshim.log`；WSI 四个入口（surface / create swapchain / get images / present）挂钩。**这是让判据行成立的那份代码** |
| [`gen_shim.py`](shim/gen_shim.py) | `/root/shimgen/gen_shim.py` | 7,051 | `51f3039d2c6de728` | **生成器**：从 NDK sysroot 的 `vulkan/vulkan_core.h` + `vulkan/vulkan_android.h` 解析 `VKAPI_ATTR <ret> VKAPI_CALL <name>(<params>);`，按**首参类型**分类，生成 C 转发层到 `shim_gen.inc`。输入 = `vkund.txt`（125 个 UND 符号） |
| [`shim.c`](shim/shim.c) | `/root/shimgen/shim.c` | 17,188 | `a147da783fd12fff` | 手写外壳：6 个"特殊函数"（`vkCreateInstance`/`vkDestroyInstance`/`vkCreateDevice`/`vkDestroyDevice`/`vkGetInstanceProcAddr`/`vkGetDeviceProcAddr`）的分派实现、句柄缓存、以及 `#include "shim_gen.inc"` 引入 119 个泛型转发 |
| [`shim_gen.inc`](shim/shim_gen.inc) | `/root/shimgen/shim_gen.inc` | 132,213 | `b5b3584360568a1e` | **生成物**（由 `gen_shim.py` 产出，原样收录以便审计"分派表到底长什么样"）：119 个泛型转发函数 + 123 项 thunk 表的实现 |
| [`vkund.txt`](shim/vkund.txt) | `/root/shimgen/vkund.txt` | 2,718 | `e172c349fa1f9fe1` | **输入清单**：`libMobileGL.so` 里 UND 的 `vk*` 符号，每行一个。**实测 125 行** —— 与 [`../docs/09` §10](../docs/09-mobilegl-integration.md) 的"125 个"互证 |
| [`gen_report.txt`](shim/gen_report.txt) | `/root/shimgen/gen_report.txt` | 378 | `f7b858f339b67a80` | **生成报告**（原文）：`总符号: 125 · 泛型转发: 119 · 手写特殊: 6`；返回类型分布 `{VkResult: 52, void: 67}`；解析路径分布 `{device: 61, device_global: 42, instance: 3, physdev: 11, instance_create: 2}`；未知返回类型：无 |
| [`build_shim.sh`](shim/build_shim.sh) | `/root/shimgen/build_shim.sh` | 1,692 | `05b51143fa9252de` | 编译垫片。**关键**：必须带 `-Wl,-soname,libvkpanvk_shim.so`（`DT_SONAME`）—— 否则即使 `DLOPEN` 预加载成功，`DT_NEEDED` 仍报 `not found`（[`../docs/09` §17(c)](../docs/09-mobilegl-integration.md)） |

### 自查（一条命令，必须两条都过）

```bash
# ① 导出清单（必须排除 STT_FILE 伪符号，否则 vkshim.c 会被算进去凑成 126）
readelf -sW --dyn-syms libvkpanvk_shim.so \
  | awk '$4!="FILE" && $7!="UND" && $8~/^vk/{print $8}' | sort -u | wc -l    # 期望 125
# ② 与需求求差（期望空）
comm -23 <(sort -u vkund.txt) <(readelf -sW --dyn-syms libvkpanvk_shim.so \
  | awk '$4!="FILE" && $7!="UND" && $8~/^vk/{print $8}' | sort -u)
```

---

## 2. `pack/` —— 打包、清单模板与校验

| 文件 | 原始路径 | 大小 (B) | sha256(前16) | 来源与用途 |
|---|---|---|---|---|
| [`AndroidManifest.v46.xml`](pack/AndroidManifest.v46.xml) | `/root/v46/AndroidManifest.xml` | 1,439 | `22310107ed2eb6cd` | ★ **插件清单模板**（`versionCode 46` / `4.6-wsi-diag`）。含全套 meta-data：`fclPlugin` / `renderer = magma_panvk:libMobileGL.so:libMobileGL.so` / `des` / `pojavEnv`（含 `MESA_VK_WSI_HEADLESS_SWAPCHAIN=1`）/ `boatEnv` / `minMCVer` / `maxMCVer`，以及**必须有**的带 `LAUNCHER` 的 Activity（否则 FCL 扫描不到插件） |
| [`pack_mgl_plugin.sh`](pack/pack_mgl_plugin.sh) | `/root/pack_mgl_plugin.sh` | 2,421 | `7accccd6280af62b` | 渲染器插件打包主脚本：`aapt2 link` → `zip` 塞 `lib/` + `classes.dex` → `zipalign` → `apksigner` |
| [`pack_v50.sh`](pack/pack_v50.sh) | `/root/pack_v50.sh` | 3,003 | `b66d33b6fb58dfd7` | **v50（真 Android 交换链版）**的打包脚本。价值在于它把**完整的 `pojavEnv`/`boatEnv` 与包名/versionCode 写在脚本里**，是"清单即配置"的活样板（含 `PANVK_KBASE_DMA_HEAP`、`PANVK_GRALLOC_NO_FALLBACK`、`PANVK_GRALLOC_NO_INFER_LINEAR`） |
| [`verify_mgl.sh`](pack/verify_mgl.sh) | `/root/verify_mgl.sh` | 604 | `cff66dd940a44997` | 装机后的验证脚本（读日志取判据） |

> ⚠️ **签名 key 不在本仓库**。脚本里引用 `/root/dsh-driver.keystore`（别名 `dshdriver`，口令在脚本内为明文 `android`）。
> 原样收录是为了复现流程；**使用者需自备 key**，不要把这里的口令当作可复用的凭据。

---

## 3. 与 `tools/` 的关系（别混淆）

| | 位置 | 是什么 |
|---|---|---|
| **本目录 `source/shim/`** | 我们**最终采用**的转发垫片（125 入口、静态链入 MGL、`gipa_pd` 物理设备优先） | ✅ 让判据行成立的代码 |
| [`../tools/vkshim_icd.c`](../tools/vkshim_icd.c) | **早期**的 `libvulkan.so` 转发垫片 | ❌ **记录一条已证明走不通的路**（`LD_LIBRARY_PATH` 在进程启动后无效；顶替 `libvulkan.so.1` 会卡死 JVM） |
| [`../tools/vkicd_probe.c`](../tools/vkicd_probe.c) | 任意驱动 `.so` 的冒烟测试（ICD/HAL 形态判定、设备名/API/扩展） | 用来**先验**一份驱动能不能用 |
| [`../tools/make_driver_apk.sh`](../tools/make_driver_apk.sh) | 驱动插件 APK 打包（早期形态） | 与本目录 `pack/` 互补 |

---

## 4. 复现顺序（最短路径）

```bash
# ① 生成转发层并编译（须带 DT_SONAME）
python3 source/shim/gen_shim.py            # → shim_gen.inc + gen_report.txt
bash    source/shim/build_shim.sh          # → libvkpanvk_shim.so
# ② 自查：导出 vk* 数 == 125 且与 vkund.txt 求差为空（见 §1 的两条命令）
# ③ 打包渲染器插件（模板：source/pack/AndroidManifest.v46.xml）
bash source/pack/pack_mgl_plugin.sh ./libvulkan_freedreno.so "MobileGL Magma + PanVK" mgl
# ④ 装机后读判据
bash source/pack/verify_mgl.sh
```

完整步骤（含每一步的判据与未实测标注）见 [`../research/paper.md`](../research/paper.md) 附录 B
与 [`../README.md`](../README.md) §3。

---

## 5. 已知缺口（如实标注）

1. **`vkshim_mgl.c` 的生成时机与 `shimgen/` 的对应关系未逐行核对**：
   `vkshim_mgl.c` 头部注释与 `shimgen/shim.c` 同源（"自动生成（修好正则 + 设备缓存版）"），
   但**哪一次生成对应哪一版**没有记录。若需精确复现某一版垫片，以 [`../MANIFEST.md`](../MANIFEST.md)
   的版本台账 + `versionName` 为准。
2. **`shim.c` 与 `vkshim_mgl.c` 的关系**：前者是生成器的**外壳模板**，后者是**最终链接进 MGL 的成品源码**。
   两者**不完全相同**（后者含 v39 之后的 `gipa_pd` 修正与 WSI 挂钩）。**差异未做逐行 diff 记录。**
3. **`shim_gen.inc` 是生成物**：不要手改；改 `gen_shim.py` 与 `shim.c` 后重新生成。
4. **打包脚本里的绝对路径**（`/opt/android-sdk`、`/root/dsh-driver.keystore`、`/root/vNN/`）
   是按当时的服务器环境写的，**跨环境使用需自行调整**。
