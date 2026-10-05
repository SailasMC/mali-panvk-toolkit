# 20 号报告 — v55：把 wonderkast02 的 G720 先例驱动打进我们的插件壳（含"到底能不能装上去"的判定）

面向：OPPO PHZ110（MT6989 / Immortalis-G720 **MC12** / Android 16 / 无 root）上，用**第三方已跑通二进制**
做一次干净的 A/B 判定 —— 病灶在"我们这台变体的 **MC12 / uAPI 1.21** 配置面"，还是在"**zenithblue 装配**"。

- 报告日期：2026-10-05
- 纪律：**未改** `/root/mesa`、`/root/zenithblue`、`/root/MobileGL`；**未 `rm -rf`** 任何既有目录；
  **未在手机上安装任何东西**；**未做 adb/UI 操作**；**未触碰红线三件套**（`com.netease.cloudmusic` /
  `roro.stellar.manager` / `com.dsharnessmobile.shell`）。服务器侧设备检查只用了只读 `ls/cat/getprop`。
  新增写入仅：`/root/research/**`、以及任务明确要求的产物 `/root/final/mgl-panvk-v5{5,6}.apk`(+`.idsig`)。
- 标注：【已定论】= 有命令输出/源码行号支撑；【推断】= 由已定论事实演绎；**未验证**= 未取到证据。

---

## 0. 一页结论（先读这段）

1. **取回了，而且哈希完全对得上。** 发布物 `PanVK-G720-0.1.0-beta.2.zip`
   SHA-256 = `fc1d69647c071ca3fe30ae2fb450e95c91c08e90779eb32c865fa384dff5aaca`，
   与 19 号报告 §0.7 给的 `fc1d6964…` **逐字符一致**，也与发布方自己的 `SHA256SUMS.txt` 一致。
   包内只有两个文件：`libvulkan_panfrost.so`（21,497,392 B，
   SHA-256 `126b8b6124a8677298469f520cd6c825de88b498fe93a38bd358ef3b557882d3`）+ `meta.json`。
   **★ 注意：内嵌 .so 的 sha256 与 19 号报告里的 `126b8b61…` 也一致。** 【已定论】

2. **★ 最重要的发现：它不是"纯 ICD"，原样换上去一定起不来。**
   `vk_icdGetInstanceProcAddr` / `vk_icdNegotiateLoaderICDInterfaceVersion` /
   `vk_icdGetPhysicalDeviceProcAddr` **三个入口都存在于文件里，但都是 LOCAL**：
   它们**不在 `.dynsym`、不在 `.gnu.hash`** 里 ⇒ **`dlsym()` 三个全部返回 NULL**。
   该 .so 的 `.dynsym` 里**唯一被导出的动态符号是 `HMI`**（`HAL_MODULE_INFO_SYM`，248 B）。
   ⇒ 这份产物是**按"Android Vulkan HAL 模块"方式链接的**（`/vendor/lib64/hw/vulkan.*.so` 那种，
   由 `hw_get_module` 用 `HMI` 取模块），**不是 JSON 可发现的桌面式 ICD**。 【已定论，见 §3】

3. **再叠一个独立的阻断点**：它的 `DT_NEEDED` 里有 **`libhardware.so`**。
   设备上 `/system/lib64/libhardware.so` **存在**，但它**不在** `/system/etc/public.libraries.txt`
   （也不在 `public.libraries-mtk.txt` / `-oplus.txt`）⇒ **targetSdk≥24 的 App 在 classloader 命名空间里
   加载不到它** ⇒ 未经处理时**连 `dlopen` 都过不去**。 【已定论，实测列表见 §4.2】

4. **推论（这才是对 A/B 实验最要紧的一句）**：如果我们**原样**把这份 .so 放进 v55 的
   `lib/arm64-v8a/libvulkan_freedreno.so` 槽位，那么在 FCL 里它**必然失败**，而且是**在"取 ICD 入口"这一步**
   就失败 —— 失败原因与 MC12、uAPI 1.21、zenithblue 装配**全都无关**。
   这会把 A/B 做成**假阴性**（"跑通方的二进制也跑不起来 → 结论：病灶在设备侧"），**结论是错的**。 【已定论（两个阻断点）+ 推断（后果）】

5. **因此本轮给了两个 APK（都在 `/root/final/`）**：
   | 产物 | 说明 | 判定力 |
   |---|---|---|
   | **`mgl-panvk-v55.apk`** (vC 55) | **按任务书字面要求**：payload **逐字节原样**塞进驱动槽位；`libMobileGL.so`+`classes.dex` 沿用 v54；manifest env 与 v54 逐字符一致。**要求的 sha256 校验通过。** | **无**（装上去必定在 ICD 入口处失败，见 §2.4） |
   | **`mgl-panvk-v56.apk`** (vC 56) | **装载可用版**：驱动槽位换成我们写的 **~90 行转发垫片**，真 payload 以**原始字节**放在同目录 `libvulkan_panfrost.so`（**APK 内可 sha256 复核 = `126b8b61…`**），另加一个 `libhardware.so` 替身满足依赖。其余（libMobileGL/classes.dex/manifest env/签名）与 v55 **完全相同** | **★ 有** —— 建议用它做 A/B |

   v56 里**真正运行的是他们的机器码，字节未改**（我们只是在外面加了两个薄壳）—— 所以 A/B 的"干净性"保住了。

6. **阻塞点（如实报告）**：v55 **不能**用于 A/B（它不是"跑通方驱动跑不起来"，而是"装载条件不成立"）；
   v56 把装载条件补齐了，但代价是 **gralloc 路径降级**（`libhardware.so` 替身返回 `-ENOENT`，
   见 §6.3）⇒ **AHB 导入/窗口化 WSI 可能受限**；**离屏 compute/graphics 不受影响**。
   建议 A/B 用**离屏探针**（probe10 里 `l_tri` 那种形态）先做第一次判定。

---

## 1. 取回与校验（步骤 1）

发布页：<https://github.com/wonderkast02/panvk-g720-kbase-csf/releases/tag/0.1.0-beta.2>
（GitHub API `releases` 返回 `tag_name=0.1.0-beta.2`，`prerelease=false`，`published 2026-09-24`）

| 资产 | 大小 (B) | SHA-256（实测） |
|---|---|---|
| `PanVK-G720-0.1.0-beta.2.zip` | 4,666,597 | `fc1d69647c071ca3fe30ae2fb450e95c91c08e90779eb32c865fa384dff5aaca` |
| `PanVK-G720-0.1.0-beta.2-MANIFEST.txt` | 1,916 | `82d1436ef6873a184c8ac2f142d7b48e0437763fec52c32107f37939b7b0606b` |
| `SHA256SUMS.txt` | 197 | （上游自述，内容与上面两行一致） |
| **包内 `libvulkan_panfrost.so`** | **21,497,392** | **`126b8b6124a8677298469f520cd6c825de88b498fe93a38bd358ef3b557882d3`** |

- **与 19 号报告 `fc1d6964…` 的对照：一致（逐字符）** ✅；与上游 `SHA256SUMS.txt` 对照：一致 ✅
- 上游 MANIFEST 自述（与实测一致）：`SONAME libvulkan_panfrost.so PASS`、`NEEDED libdrm NO`、
  `GLIBC version refs NO`、`undefined panthor NO`、`Min Android API: 35`、
  `GNU Build ID: 68c3733fda7a231cea5d9e94673adb49a2265203`、
  `driverVersion: Mesa 26.3.0-devel / PanVK G720 0.1.0-beta.2`。
- `file` 判定：`ELF 64-bit LSB shared object, ARM aarch64, for Android 35, built by NDK r29,
  BuildID[sha1]=68c3733f…, with debug_info, not stripped`（**未 strip，`.symtab` 完整** —— 这正是我们能
  定位那三个 LOCAL 入口地址的原因）。
- 存放：`/root/research/v55work/{PanVK-G720-0.1.0-beta.2.zip, …-MANIFEST.txt, SHA256SUMS.txt, extracted/}`

**顺带验证了历史版本（说明这不是偶发）**：`0.1.0-beta.1.9.4` zip（4,576,837 B，
sha256 `01c6304206c6e348cb069e3d04fb1c7b693195b543b4134ad7c108a33906d1fa`，与上游一致）
内嵌 `libvulkan_panfrost.so`（21,310,064 B，sha256 `05f867332924aacd91e6182cc1cc572ff04689cbcebeeba0e70bef61698dc9de`）
**同样是 HMI-only、同样 NEEDED libhardware.so** ⇒ 是该项目的**系统性构建配置**，不是 Beta 2 的偶发打包事故。 【已定论】

---

## 2. 二进制体检（步骤 1 的逐项回答）

### 2.1 `readelf -d`（原样输出摘要）

```
 RUNPATH  Library runpath: [$ORIGIN/../../android_stub]
 NEEDED   libhardware.so, liblog.so, libnativewindow.so, libsync.so, libm.so, libz.so, libdl.so, libc.so
 SONAME   libvulkan_panfrost.so
 FLAGS    SYMBOLIC BIND_NOW            FLAGS_1: NOW
```
（`$ORIGIN/../../android_stub` = 他们构建树里 Mesa `src/android_stub/` 的相对路径残留；见 §3.2。`BIND_NOW`
意味着**所有**依赖符号在 `dlopen` 时立即解析 ⇒ `libhardware.so` 找不到就是**硬失败**，不是延迟报错。）

### 2.2 三个 ICD 入口：**存在，但 LOCAL ⇒ dlsym 拿不到**（★ 核心）

`.symtab`（静态符号表）里三者都在 `section 15 (.text)`：

| 符号 | st_value | size | bind | 可见性 |
|---|---|---|---|---|
| `vk_icdGetInstanceProcAddr` | `0x9ca098` | 16 | **LOCAL** | 默认 |
| `vk_icdNegotiateLoaderICDInterfaceVersion` | `0xd1f038` | 40 | **LOCAL** | 默认 |
| `vk_icdGetPhysicalDeviceProcAddr` | `0xd1f060` | 40 | **LOCAL** | 默认 |
| `HMI`（`HAL_MODULE_INFO_SYM`） | `0x10f4660` | 248 | **GLOBAL** | 默认 ｜ **在 `.dynsym` 里** |

`.dynsym` 全表 225 项：**224 项 UND + 唯一 1 项已定义 = `HMI`**（`readelf --dyn-syms`、`nm -D --defined-only`、
以及我自己写的原始 ELF 解析脚本三者一致）。三个入口在 `.dynsym` 里**根本不存在**（不是"隐藏"，是"缺席"）。

**实证（不是靠推理）**：我按 bionic/glibc 的 `dlsym` 算法（`.gnu.hash`：`nbuckets / symoffset / buckets / chain`）
做了离线模拟，对该文件与我们的 v54 驱动做了**对照实验**：

| 查询 | **他们的 payload** | **我们的 v54 驱动（已知在机上能跑）** |
|---|---|---|
| `dlsym("vk_icdGetInstanceProcAddr")` | **NULL**（`symoffset=224`，bucket 指向 224=HMI，chain 到 224 就断） | **FOUND**（idx 238） |
| `dlsym("vk_icdNegotiateLoaderICDInterfaceVersion")` | **NULL** | **FOUND**（idx 240） |
| `dlsym("vk_icdGetPhysicalDeviceProcAddr")` | **NULL** | **FOUND**（idx 241） |
| `dlsym("HMI")` | FOUND（idx 224） | FOUND（idx 239） |

⇒ **`dlsym` 三个入口全 NULL 是确定的**，不是猜的。 【已定论】

### 2.3 G720 相关标识（`strings`）：**齐全** ✅

```
G720            Mali-G720        G710   G715   G725
Mali-G1-Ultra   Mali-G1-Premium  Mali-G1-Pro          ← 说明是 25.x/26.x 的模型表（arch 14 = G1 系列）
Mali-G715 Mali-G615 Mali-G610 Mali-G310v1..v5 …
decode_csf.c
kbase: CSF group %u tiler heap OOM notification      ← ★ 我们要判定的那条错误路径，它里面也有
kbase: CSF group %u progress timeout notification
kbase: CSF group %u fatal error: status 0x%08x (exception 0x%02x), sideband 0x%016lx
kbase: failed to query the CSF global interface
kbase_kmod_csf_group_create / _queue_kick / _tiler_heap_create / _wait_cqs64 / …
```
⇒ 与 19 号报告一致：**G720 = arch v12**，这份二进制明确带 `Mali-G720` 型号条目，
并且**带同样的 tiler heap OOM 通知代码路径**。所以"如果它也 OOM，我们能在它的日志里看到同一句话"。 【已定论】

### 2.4 结论（回答"可装载性"）

> **任务书的假设不成立**：任务书说"只要对方的 .so 是标准 ICD（导出上述两个入口）就应该能换上去 ✓，
> 请核实"。**核实结果：它不导出这两个入口**（LOCAL，dlsym=NULL），所以**原样换上去不能工作**。

我们垫片的实际装载路径（`/root/mgl_icd/vkshim_mgl.c`，静态链进 `libMobileGL.so`）：

```c
40: static void shim_init(void){
42:   char selfdir[512]={0}; Dl_info info;
43:   if(dladdr((void*)(uintptr_t)&shim_init,&info)&&info.dli_fname){ …取自身目录… }
44:   char cand0[640]={0}; if(selfdir[0]) snprintf(cand0,…,"%s/libvulkan_freedreno.so",selfdir);
45:   const char* c[]={cand0,"libvulkan_freedreno.so","/data/local/tmp/libvulkan_freedreno.so",0};
46:   for(int i=0;c[i];i++){ g_icd=dlopen(c[i],RTLD_NOW|RTLD_LOCAL); if(g_icd) break; }
47:   if(!g_icd){ FLOG("[vkshim] cannot dlopen ICD\n"); return; }
49:   neg_t neg=(neg_t)dlsym(g_icd,"vk_icdNegotiateLoaderICDInterfaceVersion"); if(neg){uint32_t v=7;neg(&v);}
50:   g_gipa=(…(*)(VkInstance,const char*))dlsym(g_icd,"vk_icdGetInstanceProcAddr");
```
⇒ 垫片就是**靠 `dlsym` 名字**取真实 ICD 的。对这份 payload：`dlopen` 会（因 `libhardware.so`）先失败；
就算绕开，第 49/50 行的 `dlsym` 也**必然 NULL** ⇒ 垫片认为"没有可用 ICD"。
（好消息：`selfdir` 由 `dladdr` 取自身路径得到，`cand0` = App 私有 native lib 目录 —— 这正是我们 v56 放
转发垫片的路径，**必然被找到**。） 【已定论】

---

## 3. 为什么只导出 HMI —— 根因（带行号）

`/root/mesa/src/vulkan/meson.build:6-15`：

```meson
if with_platform_android
  vulkan_icd_symbols = files('vulkan-icd-android-symbols.txt')
  if with_ld_version_script
    vulkan_icd_link_args += ['-Wl,--version-script', join_paths(…, 'vulkan-android.sym')]
    …
else
  vulkan_icd_symbols = files('vulkan-icd-symbols.txt')     # ← 里面才是 3 个 ICD 入口
  if with_ld_version_script
    vulkan_icd_link_args += ['-Wl,--version-script', … 'vulkan.sym']
```

`/root/mesa/src/vulkan/vulkan-android.sym` 全文：

```
{
	global:
		# Andoid looks for this global in HAL modules. In the source it occurs
		# as HAL_MODULE_INFO_SYM (which is just a #define for HMI) and it's an
		# instance of struct hwvulkan_module_t.
		HMI;
	local:
		# When static linking LLVM, all its symbols are public API.
		# That may cause symbol collision, so explicitly demote everything.
		*;
};
```
另外 `/root/mesa/src/panfrost/vulkan/meson.build:241` 有 `gnu_symbol_visibility : 'hidden'`，
所以"默认隐藏 + 版本脚本只放行 HMI"⇒ **导出集 = {HMI}**，与实测完全吻合。

**我们这边为什么没中招**：我们的 v54 驱动导出的是
`vk_icdGetInstanceProcAddr` / `vk_icdNegotiate…` / `vk_icdGetPhysicalDeviceProcAddr` / `HMI` 四个
（即走了 `vulkan-icd-symbols.txt` 那条 / 没套 Android HAL 版本脚本）。
⇒ **这也解释了 19 号报告 §5.3 记的"ABI 世代差一整档"之外的又一条装配差异：他们的构建目标是 Android HAL，
我们的构建目标是 App 可加载 ICD。** 【已定论】

**为什么他们的发布门禁没拦住**：他们 MANIFEST 里列的"Qualified build gates"是
`target build / full build / Meson test / ELF AArch64 / SONAME libvulkan_panfrost.so / NEEDED libdrm NO /
GLIBC version refs NO / undefined panthor NO` —— **没有一条检查 ICD 入口是否被导出**。
（而且 `vulkan_icd_symbols` 在 meson 里只用于 `with_symbols_check` 的 `symbols-check` 测试，
`src/panfrost/vulkan/meson.build:242-253`；而 Android 那份 symbols 文件里**只有 HMI**，本身就自洽地
"不期望"导出 ICD 入口。） 【已定论（门禁清单）+ 推断（逃逸原因）】

### 3.1 与 19 号报告的关系

19 号报告把这份产物当作"**可直接替换的 ICD 二进制**"来推荐（§0.7"把 wonderkast02 的 Beta 2 二进制原样装到
本机 PHZ110 上跑"）。**本报告修正这一点**：它**不是**可直接替换的 ICD，需要一层转发（或重编）才能装载。
19 号报告的其余结论（架构判定、源码同源、H1/H2 移植缺陷定性）**不受影响**。

---

## 4. 依赖可装载性核对（步骤 2）

### 4.1 逐个 NEEDED × 设备实况

设备：PHZ110，`ro.build.version.release=16`（`ro.build.version.sdk=36`），`ro.product.cpu.abi=arm64-v8a`。

| NEEDED | 设备上位置 | 在 `public.libraries.txt` 里？ | 能否被 App 加载 |
|---|---|---|---|
| `libhardware.so` | `/system/lib64/libhardware.so`（35,504 B）**存在** | **不在**（`.txt` / `-mtk.txt` / `-oplus.txt` 三份都查过） | **不能** ❌ |
| `libnativewindow.so` | `/system/lib64/`（68,248 B） | 在 | 能 ✅ |
| `libsync.so` | `/system/lib64/`（51,232 B） | 在 | 能 ✅ |
| `liblog.so` | `/system/lib64/`（103,672 B） | 在 | 能 ✅ |
| `libm.so` / `libz.so` / `libdl.so` / `libc.so` | `/system/lib64/`（`libdl`/`libc` 软链到 `/apex/com.android.runtime/…`） | 都在 | 能 ✅ |

**推导**：未 root 的 App 拿不到真实 gralloc HAL（它在 `/vendor/lib64/hw/gralloc.<hw>.so`，
只有 vendor 进程能取）。Mesa 里唯一从 `libhardware` 引进来的符号就是 **`hw_get_module`**（见 4.3），
所以"补一个 `hw_get_module` 替身"是**唯一**不 root 的出路 —— 这就是 v56 里那个 5 KB 的 `libhardware.so`。 【已定论】

### 4.2 符号 → 库的归属（确认只缺 libhardware 一个）

payload 里**不带版本号的 UND 符号**（带 `@LIBC/@LIBC_R/@LIBC_P` 的属于 libc，其余按名归属）：

```
AHardwareBuffer_acquire/allocate/describe/getNativeHandle/isSupported/release   → libnativewindow
crc32 / deflate / deflateInit_ / deflateEnd / inflate / inflateInit_ / inflateEnd → libz
sync_merge                                                                        → libsync
__android_log_write                                                               → liblog
hw_get_module                                                                     → libhardware   ← ★ 唯一
```
⇒ **只需替身提供 `hw_get_module`。** 【已定论】

### 4.3 libc 版本需求与"我们已知能跑"的 v54 一模一样

`readelf -V` 的 `VERNEED` 对比：两份都是 3 条（`libm.so: LIBC v4` / `libdl.so` / `libc.so`），
条目与版本号一致 ⇒ 我们能用的 libc 版本面，它也能用。
（这是"装载兼容性"层面的旁证；v54 已在真机跑过。） 【已定论（对照）+ 推断（可用性）】

---

## 5. `/root/final/mgl-panvk-v55.apk` —— 按任务书字面交付

`/root/research/v55work/src/pack_v55.sh`（可重放；以 `/root/pack_v54.sh` 为模板）。

| 项 | 值 |
|---|---|
| 包名 / 签名 | `com.dsh.plugin.driver.g720`；keystore `/root/dsh-driver.keystore`（alias `dshdriver`）——**与 v54 同一签名** |
| 签名者证书 SHA-256 | `eba5095017e1b5266e4a60aba48196dd4056bb328e680262ce01bde2de09e565`（v54/v55/v56 三者一致 ✅） |
| 签名方案 | v2 `true`、v3 `true`（v1 `false`，与 v54 同款构建方式；targetSdk 34 满足 Android 11+ 的 v2 要求） |
| versionCode / versionName | **55** / `5.5-wonderkast-g720-beta2` |
| `lib/arm64-v8a/libvulkan_freedreno.so` | **payload 原样**（21,497,392 B） |
| `lib/arm64-v8a/libMobileGL.so` | 取自 v54（16,956,584 B，sha256 `72919c73a7e07630f329bb4ad9605c606a9aac9e2fc6596683ee7b9f9c76848b`） |
| `classes.dex` | 取自 v54（1,328 B，sha256 `6bd3abde2c53506f1b54bb88a8069c3cc394c2fb08440e4ca475cbf8fd64f4ad`） |
| APK 大小 / SHA-256 | 10,576,431 B / **`e1d5b0d52c96583299ed1dce3c42a546e8dc3b6a067fedb55e4717d73abb7af7`** |

**任务书要求的校验（★）**：

```
unzip -p /root/final/mgl-panvk-v55.apk lib/arm64-v8a/libvulkan_freedreno.so | sha256sum
  126b8b6124a8677298469f520cd6c825de88b498fe93a38bd358ef3b557882d3   ← 非空 ✅
sha256sum /root/research/v55work/extracted/libvulkan_panfrost.so
  126b8b6124a8677298469f520cd6c825de88b498fe93a38bd358ef3b557882d3   ← 与跑通方 .so 一致 ✅
```
`libMobileGL.so` 与 `classes.dex` 的 APK 内取回哈希也与 v54 内的一致 ✅（逐项打印在脚本输出里）。

**manifest env 逐字符一致**（`aapt2 dump xmltree` 对比 v54 → v55，**唯一差异就是版本号**）：

```
--- v54 -> v55 diff:
<  …versionCode=54
<  …versionName="5.4-p2-tiler-heap-renew"
---
>  …versionCode=55
>  …versionName="5.5-wonderkast-g720-beta2"
```
`pojavEnv` / `boatEnv`（含 `MESA_DEBUG=1:PANVK_DEBUG=1:LIBGL_DEBUG=1:EGL_LOG_LEVEL=debug`，
**没有** `MESA_VK_WSI_HEADLESS_SWAPCHAIN`）、`renderer`、`minMCVer=1.17`、`maxMCVer=""`
与 v54 完全一致 ✅。APK 内条目只有 `AndroidManifest.xml / resources.arsc / lib/…×2 / classes.dex / META-INF/*`，
**无残留** ✅。

> ⚠️ **v55 的用途仅限"满足字面要求 + 留档"**。它**不能**用于 A/B：装上去会先因 `libhardware.so` 不可加载、
> 再因 `dlsym` 三个入口全 NULL 而失败，**与我们要判定的病因无关**。判定请用 v56。

---

## 6. `/root/final/mgl-panvk-v56.apk` —— 装载可用版（建议用它做 A/B）

### 6.1 设计

```
lib/arm64-v8a/libvulkan_freedreno.so   ← 我们写的转发垫片（9,000 B）
lib/arm64-v8a/libvulkan_panfrost.so    ← ★ 跑通方 payload 原样（21,497,392 B，sha256 可复核）
lib/arm64-v8a/libhardware.so           ← 只导出 hw_get_module 的替身（5,048 B）
lib/arm64-v8a/libMobileGL.so           ← 同 v54
classes.dex                            ← 同 v54
```
- 垫片 dlopen **同目录**的 `libvulkan_panfrost.so`（用 `dladdr(self)` 推出自身目录，
  再回退裸名 `libvulkan_panfrost.so` —— 与现有 vkshim 的裸名加载同一机制，已被 v54 验证可行）。
- 由于 payload 只导出 `HMI`，垫片**用 `HMI` 反推装载基址**：
  `base = (uintptr_t)dlsym(inner,"HMI") - 0x10f4660`，再用 `.symtab` 里的 `st_value`
  跳到真实入口（`0x9ca098` / `0xd1f038` / `0xd1f060`）。
  **自检**：DSO 基址必然页对齐 ⇒ 要求 `HMI 地址 & 0xfff == 0x660`；不符即判定"不是那份 payload"并**失败关闭**
  （不会静默跑错二进制）。
- 垫片对外导出**标准 ICD 三入口**（`GLOBAL DEFAULT`），因此我们现有 vkshim 的 `dlsym` 直接命中。
- 垫片自身只 NEEDED `liblog.so / libdl.so / libc.so`（都是 public ✅）。
- **payload 的机器码一个字节都没动**；全部改动都发生在"外面"（两个新 .so 文件 + manifest 版本号）。

### 6.2 校验

| 项 | 值 |
|---|---|
| APK 大小 / SHA-256 | 10,580,682 B / **`fb6a8a873a3857dbe4fcc99a65add00fe1f0132b6a3326cf26d134c01b985cc2`** |
| **APK 内 payload sha256** | **`126b8b6124a8677298469f520cd6c825de88b498fe93a38bd358ef3b557882d3`** ✅（= 跑通方原件） |
| 垫片 sha256 | `a0edd5e6658efb20996b87a5c4bfb45b77246b4c753e6a0b72b2d49a1c08ac65` |
| `libhardware.so` 替身 sha256 | `2da1686f5b4eecdd5b60c75c223145a515461bddc532eb462d4e32517b3026d1` |
| 垫片导出（`readelf --dyn-syms`） | `vk_icdGetInstanceProcAddr` / `vk_icdNegotiateLoaderICDInterfaceVersion` / `vk_icdGetPhysicalDeviceProcAddr`（三者 `GLOBAL DEFAULT`）✅ |
| 垫片 `dlsym` 可达性（gnu.hash 模拟） | 三者**全部 FOUND** ✅（`symoffset=15`，bucket→15；v55 那份同样查询三个**全 NULL**） |
| 替身导出 | `hw_get_module`（唯一）✅ |
| versionCode / versionName | **56** / `5.6-wonderkast-g720-beta2-icd-reexport`（**刻意不用 55**，避免与 v55 同版本号混淆） |
| 签名 | 与 v54/v55 同一证书 ✅；v2+v3 ✅ |
| manifest env | 与 v54 逐字符一致（`diff` 只显示版本号两行）✅ |

源码与可重放脚本：`/root/research/v55work/src/{icd_reexport.c, hardware_stub.c, pack_v55.sh}`。

### 6.3 v56 的三条**必须知道**的限定

1. **gralloc 降级（设计取舍）**：`libhardware.so` 替身让 `hw_get_module` 返回 `-ENOENT` ⇒ Mesa 的
   fallback gralloc 会打印 `No gralloc hwmodule detected (video buffers won't be supported)` 然后继续。
   ⇒ **AHardwareBuffer 导入/导出、窗口化 WSI 可能不可用**；**离屏 compute/graphics 不受影响**。
   （替身为何不返回 0：Mesa 自己的 build-time stub `src/android_stub/hardware_stub.cpp` 是"返回 0 且不写
   `*module`"，那会让 `u_gralloc_fallback.c:184` 解引用 NULL 而崩；返回 `-ENOENT` 才是走到"无 gralloc 但
   继续"的那条分支。） 【已定论（Mesa 源码行）+ 推断（AHB/WSI 影响面）】
2. **payload 是给 r49p1 / uAPI 1.30 调的**，我们内核是 **uAPI 1.21**（≈r42–r44，19 号报告 §5.3）。
   ⇒ 若它**也**OOM，说明"配置面/uAPI 世代"是主因；若它**跑住**，说明"装配线"是主因。**这正是实验要分辨的。**
3. **v56 与 v55 的差异被刻意限制在"装载条件"上**（多两个薄 .so），
   `libMobileGL.so` / `classes.dex` / manifest env / 签名**完全一致** —— 所以 v56 的结论可以直接回答 A/B 问题。

---

## 7. 给主 agent 的实验操作建议（我不装、不碰 UI）

1. **先装 v56**（`/root/final/mgl-panvk-v56.apk`），跑**离屏**探针（probe10 `l_tri` 那种形态）。
2. **第一眼先看我们的垫片日志**（它会把装载链讲清楚）：
   - `/sdcard/MG/v55icd.log`（垫片专用）与 logcat tag `v55icd`
   - 成功：`pinned wonderkast02 beta2 payload ready: base=0x… gipa=0x… neg=0x… pdpa=0x…`
   - 失败：会带 `dlopen("…libvulkan_panfrost.so") failed: <dlerror>` 或
     `HMI@… low bits …!= expected 0x660: not the pinned payload`
3. 再看既有三处日志：`/sdcard/MG/mgl.log`、`/sdcard/MG/vkshim.log`、logcat（`MESA_DEBUG/PANVK_DEBUG` 已开）。
4. **判读**：
   - 出现 `kbase: CSF group N tiler heap OOM notification` → **病灶在 MC12 / uAPI 1.21 这一侧**（他们这份
     r49p1-向的二进制在我们内核上也 OOM）⇒ 力气转 14 号报告 H1/H2 那一类 uAPI 协商。
   - 一路跑到出图/无 OOM → **病灶在 zenithblue 装配**（缺 `csf-v12/`、kbase 文件从 beta-1.9.4 逐字拷入）
     ⇒ 对齐他们那个 commit 的补丁集。
   - 若卡在 `No gralloc hwmodule detected` 之后的 AHB/WSI 相关失败 → 那是 §6.3-1 的**已知副作用**，
     **不影响**"tiler OOM 与否"的判定；此时请只跑离屏路径。
5. **不要**用 v55 做判定（§5 的警告）。

---

## 8. 若 v56 也不理想：替代方案（按代价排序）

| # | 方案 | 做法/代价 | 评价 |
|---|---|---|---|
| A | **用他们源码在我们构建配置下重编** | 仓库有 `mesa-snapshot/src/panfrost/{vulkan/csf,kmod/kbase_kmod.c}` + `patches/0001-panvk-kbase-g720-register-count.patch`。把这份 snapshot 编成"App 可加载 ICD"（**不套** `vulkan-android.sym`、用 `-Dwith_ld_version_script=false` 或直接以 `vulkan-icd-symbols.txt` 为准；u_gralloc 用 fallback 并用我们自己的搭桥），即可得到一个**导出三入口**的驱动 | 最彻底，但代价大（构建树、符号导出、u_gralloc/android_stub 接线）；**而且会引入"我们的工具链/我们的配置"这一新变量**，A/B 就不再是"他们的二进制 vs 我们的二进制"了 |
| B | **对 payload 做 ELF 手术**：把三个 LOCAL 符号补进 `.dynsym` + `.dynstr` + 重建 `.gnu.hash`（必要时同步 `.gnu.version`）| 纯元数据改动，**机器码零改动**；但需要新增 PT_LOAD/搬段，风险中等，且**破坏"APK 内 payload 字节同一"这条证据**（无法再用 sha256 证明跑的是原件） | 备选；当前 v56 的路子（外侧转发）**不破坏字节同一性**，故本轮选 v56 |
| C | 换历史版本 | Beta `0.1.0-beta.1.9.4` **同样 HMI-only + 同样 NEEDED libhardware.so**（已实测）⇒ **无意义** | ❌ |
| D | 走 Android HAL 路径（用 `HMI`）| 需要把 .so 放到 `/vendor/lib64/hw/vulkan.*.so` 之类的系统位置 ⇒ **需要 root/系统分区写权限**，本机没有 | ❌ |
| E | 只要 libhardware 问题的兜底 | 若日后 v56 的 gralloc 降级影响判定，可改用"只把 `libMobileGL.so` 换成我们自己的（含备选 gralloc 后端）+ 保留 payload" | 会把变量从 1 个变 2 个，谨慎 |

**另有一条给上游的反馈（可选）**：他们这份产物只要把 Android HAL 版本脚本去掉（或改用
`vulkan-icd-symbols.txt` + 动态符号表放行）就能得到标准 ICD；这是**一个构建标志就能修**的事，
并且他们的发布门禁缺一条 "ICD 入口导出" 检查。

---

## 9. 证据/产物清单

| 路径 | 内容 |
|---|---|
| `/root/research/20-v55-working-driver-package.md` | 本报告 |
| `/root/research/20-evidence.txt` | 本轮所有原始命令输出汇集（163 行：哈希、readelf、dynsym、gnu.hash 模拟、签名、依赖核对…） |
| `/root/research/v55work/PanVK-G720-0.1.0-beta.2.zip`（4,666,597 B） | 跑通方发布包原件（sha256 `fc1d6964…`） |
| `/root/research/v55work/PanVK-G720-0.1.0-beta.2-MANIFEST.txt`、`SHA256SUMS.txt` | 上游清单/校验和 |
| `/root/research/v55work/extracted/libvulkan_panfrost.so`（21,497,392 B） | ★ 解出的 ICD 驱动（sha256 `126b8b61…`） |
| `/root/research/v55work/src/icd_reexport.c` | v56 转发垫片源码（含根因与偏移说明） |
| `/root/research/v55work/src/hardware_stub.c` | v56 `hw_get_module` 替身源码 |
| `/root/research/v55work/src/pack_v55.sh` | 可重放打包脚本（编译 + 打两个 APK + 全部校验） |
| `/root/research/v55work/out/…` | 编译好的两个薄 .so（+ sha256） |
| `/root/research/v55work/{gnusim.py, elfparse.py}` | 我写的 `dlsym` 离线模拟器 / 原始 ELF 解析器（本报告 §2.2 的实证工具） |
| `/root/research/v55work/manifest-v5{4,5,6}.txt` | `aapt2 dump xmltree` 的 manifest 三方对比件 |
| `/root/research/v55work/b194.zip`、`b194/` | 历史版本 Beta 1.9.4（用于证明"系统性"） |
| **`/root/final/mgl-panvk-v55.apk`**（10,576,431 B，sha256 `e1d5b0d5…`） | vC 55，字面要求版（**不可装载**） |
| **`/root/final/mgl-panvk-v56.apk`**（10,580,682 B，sha256 `fb6a8a87…`） | vC 56，**装载可用版（建议用它做 A/B）** |

## 10. 纪律/只读声明（与 19 号报告同规格）

- **未编译、未修改** `/root/mesa`、`/root/zenithblue`、`/root/MobileGL`（三棵树全程只读）。
- **未 `rm -rf`** 任何既有目录（只在 `v55work/{stage,out,src}` 这些**本轮新建**目录里做定点 `rm -f`）。
- **未在手机上安装任何 APK**（安装由主 agent 执行）；**未做任何 adb/UI 操作**；**未触碰红线三件套**。
- 设备侧只用了只读命令（`getprop` / `ls` / `cat` / `find`）。
- 新增写入仅 `/root/research/**` 与任务指定的 `/root/final/mgl-panvk-v55.apk`、`/root/final/mgl-panvk-v56.apk`
  （及 apksigner 自动生成的同名 `.idsig`）。
