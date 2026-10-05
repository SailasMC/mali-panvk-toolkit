# 任务 02：HIDL（imapper4）路线可行性

环境：Debian 13 (x86_64) 服务器做交叉编译；目标设备 MediaTek **mt6989 / Mali / Android 16 (SDK 36) / ro.vndk.version=34**。
目标文件：`mesa/src/util/u_gralloc/u_gralloc_imapper4_api.cpp`，meson 门槛 `dependency("android.hardware.graphics.mapper", version: ">= 4.0")`。

## 结论（先说）

**可行，而且比预期便宜。** 不需要 AOSP 全量 checkout，不需要构建 hidl-gen。唯一真实的坑是
NDK libc++（`std::__ndk1`）与平台/VNDK libc++（`std::__1`）的 ABI 命名空间差异 —— 有确定性修法，
且**已在本机实测编译 + 链接通过**（v33、v34、以及 hidl-gen 新生成的头部三套全部通过）。

预计工作量：**0.5 - 1.5 人天**。最大的剩余不确定项是「app 进程能否 dlopen /system/lib64 里的
VNDK 库」这一步（无法离线证明），但设备侧间接证据非常强（见第 4 节）。

---

## 1) prebuilts/vndk 是否预生成了 HIDL 头？—— **是**

- 仓库存活范围：`platform/prebuilts/vndk/v29 ... v34`。
  **v35、v36 不存在**（`git ls-remote` 报 `repository not found`）—— Android 15+ 去掉了 VNDK 快照。
  所以能拿到的最高 VNDK 快照就是 **v34**，正好等于本机 `ro.vndk.version=34`。
- 目录结构（注意：**没有顶层 `include/`**，在每架构目录下）：
  `v33/arm64/{Android.bp, arch-arm64-armv8-a/, arch-arm-armv8-a/, configs/, include/}`
- **确切 URL（以 v33 为例）**：
  https://android.googlesource.com/platform/prebuilts/vndk/v33/+/refs/heads/android14-d1-release/arm64/include/generated-headers/hardware/interfaces/graphics/mapper/4.0/android.hardware.graphics.mapper@4.0_genc++_headers/gen/android/hardware/graphics/mapper/4.0/IMapper.h
  浏览目录把 `?format=TEXT` 换成 `?format=JSON`；取文件用 `?format=TEXT | base64 -d`。
- 命中情况：mapper 2.0 / 2.1 / 3.0 / **4.0** 四套 HIDL genc++ 头，每套 7 个文件：
  `IMapper.h types.h hwtypes.h BnHwMapper.h BpHwMapper.h BsMapper.h IHwMapper.h`
  （4.0 的 IMapper.h 61,673 B、BpHwMapper.h 9,973 B …）
- include 根 = 该目录名里 `.../genc++_headers/gen`，所以
  `-I .../android.hardware.graphics.mapper@4.0_genc++_headers/gen` 就能 `#include <android/hardware/graphics/mapper/4.0/IMapper.h>`。
- 顺带白拿的依赖（imapper4 源码还需要它们）：
  - AIDL 头：`arm64/include/generated-headers/hardware/interfaces/graphics/common/aidl/android.hardware.graphics.common-V3-ndk-source/gen/include/aidl/...`（BufferUsage/ChromaSiting/Dataspace/ExtendableType/PlaneLayout*…）
  - `arm64/include/frameworks/native/libs/gralloc/types/include/gralloctypes/Gralloc4.h`
  - `arm64/include/frameworks/native/libs/nativewindow/include/system/window.h` 等
- **快速枚举技巧（比逐个 curl 强）**：blobless 部分克隆，只下 tree 不下 blob，1 秒完成：
  `git clone --filter=blob:none --no-checkout --depth 1 -b android14-d1-release https://android.googlesource.com/platform/prebuilts/vndk/v33 v33`
  然后 `git ls-tree -r HEAD --name-only | wc -l` → **30,673 个文件，.git 仅 532K**。
  需要落盘时 `git sparse-checkout set arm64/include arm64/arch-arm64-armv8-a && git checkout` → 130 MB（v33/v34 同）。

## 2) hidl-gen 要不要自己构建？—— **不用，AOSP 直接有预编译二进制**

- **预编译 hidl-gen（实测可用）**：
  https://android.googlesource.com/platform/prebuilts/build-tools/+/refs/heads/main/linux-x86/bin/hidl-gen
  5,446,064 B 的 x86-64 stripped ELF；它只缺宿主 `libc++.so`，配套在
  https://android.googlesource.com/platform/prebuilts/build-tools/+/refs/heads/main/linux-x86/lib64/libc++.so
  两个 curl + `LD_LIBRARY_PATH=<libdir> ./hidl-gen -h` 即可跑起来（本机已跑通并成功生成头部）。
- **Debian 也有包**：trixie 里 `hidl-gen` 10.0.0+r36-3.1（`apt-get install hidl-gen`，依赖 android-libbase / android-liblog / android-libboringssl）。
- 如果非要自己编（**不推荐**）：`system/tools/hidl/Android.bp` 的依赖闭包是
  `libhidl-gen`、`libhidl-gen-ast`（含 flex/bison 处理 `hidl-gen_l.ll` / `hidl-gen_y.yy`）、
  `libhidl-gen-hash`、`libhidl-gen-host-utils`、`libhidl-gen-utils`、`libbase`、`liblog`、`libcrypto`(boringssl)、`libjsoncpp`、`libhwbinder` 的 headers —— 约 60-70 个源文件横跨 5 个仓库
  （system/tools/hidl、system/libbase、system/logging、system/libhwbinder、external/boringssl、external/jsoncpp）。
  走 Soong 要全量 AOSP（约 100 GB、数小时）；自己写 CMake + flex/bison 约 **1-2 天**。
- **保真度实测**：用预编译 hidl-gen 重新生成 mapper@4.0，与官方 v34 快照逐一 diff ——
  `IMapper.h / types.h / hwtypes.h / BnHwMapper.h / BsMapper.h / IHwMapper.h` **逐字节相同（diff 0 行）**；
  仅 `BpHwMapper.h` 差 17 B，原因是新版 hidl-gen 多写了一行 `#include <mutex>`（无害）。
- 可用命令（已验证）：
  ```
  LD_LIBRARY_PATH=<btlib64> hidl-gen -o gen -L c++-headers \
    -r android.hardware:<hardware/interfaces> -r android.hidl:<system/libhidl>/transport \
    android.hardware.graphics.mapper@4.0
  ```
  需要 sparse clone：hardware/interfaces 的 `graphics/mapper/4.0`、`graphics/common/1.0 1.1 1.2`；
  system/libhidl 的 `transport/base/1.0`、`transport/token/1.0`。

## 3) 链接所需的 .so —— 两条路都行，且**根本不需要 adb pull**

来源 A（推荐，和 `ro.vndk.version=34` 精确对应）：`prebuilts/vndk/v34` arm64 快照自带 369 个 .so：
- `arm64/arch-arm64-armv8-a/shared/vndk-sp/`：**android.hardware.graphics.mapper@4.0.so**、libhidlbase.so、libgralloctypes.so、libutils.so、libcutils.so、libbase.so、**libc++.so**、libhidlmemory.so、libutilscallstack.so、libhidltrans… 共 40 个
- `arm64/arch-arm64-armv8-a/shared/llndk-stub/`：liblog.so、libsync.so、libnativewindow.so、libvndksupport.so、libc.so、libm.so、libdl.so…（链接桩，只有符号没有 NEEDED 依赖）

来源 B：设备 `/system/lib64`（**已逐个核实全部存在**）：
`android.hardware.graphics.mapper@4.0.so libgralloctypes.so libhidlbase.so libutils.so libcutils.so libbase.so liblog.so libsync.so libnativewindow.so libc++.so libvndksupport.so libhwbinder.so`。
若确实想用设备库当链接桩，用来源 A 即可等价替代（同一 ABI），省掉 pull 环节。

**关键纠正**：`libhwbinder` **不需要**。readelf 实测 `mapper@4.0.so` 的 NEEDED 是
`graphics.common@1.0/1.1/1.2, libhidlbase, liblog, libutils, libcutils, libc++, libc, libm, libdl` —— hwbinder 早已并入 libhidlbase。
（设备上 `/system/lib64/libhwbinder.so` 只是 35 KB 残桩；`include/system/libhwbinder` 只给 hidl-gen 用。）

### 3.1 真正的坑：libc++ ABI 命名空间（必须处理）

预编译 VNDK 库和设备库全用 **`std::__1`**；NDK 编出来的目标文件用 **`std::__ndk1`**。
两者是**不同的 mangled name**，直接链会在
`IMapper::getService(std::__ndk1::basic_string const&, bool)` 和
`gralloc4::decodePlaneLayouts(..., std::__ndk1::vector<...>*)` 上报 undefined。

修法（已实测，**只影响这一个 TU**）：用 VNDK 快照里的**平台 libc++ 头**编这一个文件：
```
-nostdinc++ -I <vndk>/arm64/include/external/libcxx/include \
-D_LIBCPP_SUPPORT_XLOCALE_POSIX_L_FALLBACK_H
```
- `__config` 里 `_LIBCPP_ABI_VERSION` 默认就是 1 → 自动得到 `std::__1`，无需再定义。
- `_LIBCPP_SUPPORT_XLOCALE_POSIX_L_FALLBACK_H` 是**必须**的：Android 13 版 libc++ 的
  `support/xlocale/__posix_l_fallback.h` 会与 NDK r27c 的 bionic `ctype.h` 重复定义
  `isalnum_l/isalpha_l/isblank_l`；该文件是 include guard 包着的，定义 guard 即可整体跳过
  （它本来就是给 NDK<=16 用的 legacy 兜底）。

### 3.2 实测结果

针对 v33、v34、以及 hidl-gen 新生成的头三套，都是 `COMPILE_RC=0` + `LINK_OK`；
两个关键符号的 mangled name 与提供方**逐字符 EXACT MATCH**。

链接产物 `DT_NEEDED`：mapper@4.0 / libgralloctypes / libhidlbase / libutils / libcutils / libbase / liblog / libsync / libnativewindow / libc++ / libm / libdl / libc（共 62 个 UND）。

**意外的好消息**：62 个 UND 里只有 **9 个**来自 libc++，而且**全是无命名空间的 C++ ABI 符号**：
`__cxa_begin_catch`、`__gxx_personality_v0`、`operator new/new[]/delete/delete[]`、`std::terminate()`、`__cxxabiv1::__class_type_info` / `__si_class_type_info` 的 vtable。
**没有任何 `std::__1::` 类型的运行期符号**（因为该 TU 里 std 容器/字符串的操作全是 header inline）。
推论：进程里已经加载的 NDK `libc++_shared.so` 就能满足它们；DT_NEEDED 里的 `libc++.so` 在设备上
解析到 `/apex/com.android.runtime/lib64/libc++.so -> /system/lib64/libc++.so`（app 可见）。
加上 shim 与 Mesa 其余部分之间**只有 `extern "C"` 边界**（`u_gralloc` ops 是 C 函数指针表），
所以「一个进程里两个 libc++」在这里是良性且可控的。若想彻底消除，可把该 TU 的 libc++ 换成
NDK 的 `libc++_shared.so` 作为链接提供方（因为只需要 ABI 符号）。

### 3.3 运行期需要的**精确符号清单**（已逐个在设备 `/system/lib64` grep 验证存在）

| 提供方 | 符号 |
|---|---|
| android.hardware.graphics.mapper@4.0.so | `IMapper::getService(std::__1::basic_string const&, bool)` |
| libgralloctypes.so | `decodePlaneLayouts`、`decodePixelFormatFourCC`、`decodePixelFormatModifier`、`decodeChromaSiting`、`decodeDataspace`、`getStandardChromaSitingValue` |
| libhidlbase.so | `hidl_string::hidl_string(const char*)`、拷贝构造、析构、`details::return_status::~return_status()` |
| libc++.so | 上述 9 个 ABI 符号 |

设备侧 ABI 核实：`libgralloctypes.so` / `libhidlbase.so` / `mapper@4.0.so` / `libc++.so` 里
`NSt3__1` 出现 47 / 284 / 79 / 1611 次，`NSt6__ndk1` **0 次**。全部一致。

## 4) 设备运行期门槛 —— 证据很强，但还差最后一步直测

设备事实：
- `/vendor/etc/vintf/manifest.xml` **明确声明**：
  ```xml
  <hal format="hidl">
      <name>android.hardware.graphics.mapper</name>
      <transport arch="32+64">passthrough</transport>
      <fqname>@4.0::IMapper/default</fqname>
  </hal>
  ```
- 厂商 passthrough 实现存在：`/vendor/lib64/hw/android.hardware.graphics.mapper@4.0-impl-mediatek.so`
  （标准 `<fqname>-impl-<instance>.so` 命名）+ AIDL gralloc5 的 `mapper.mediatek.so`。
- `lshal` 输出：`DM,FC  android.hardware.graphics.mapper@4.0::IMapper/default`，后面挂着约 300 个**客户端 PID**；
  抽样确认其中**前 40 个里有 19 个是普通 app uid**：
  `bin.mt.plus`(10569)、`com.sohu.inputmethod.sogouoem`(10147)、`com.heytap.health:SportDaemonService`(10307)、`com.mobiletools.systemhelper`(10089)。
  即：在这个具体设备上，passthrough 的 IMapper@4.0 确实被普通 app 进程拿到了。
- Android 15/16 已移除 VNDK 链接器命名空间（`/system/etc/vndk*.libraries*.txt` 不存在，库被摊平进 `/system/lib64`），
  所以这些库对 app 命名空间是可解析的。
- 客户端库本身在 `/system/lib64/android.hardware.graphics.mapper@4.0.so`（152,352 B，真身非桩）。

**唯一没能证明的一步**：把 shim 推上设备真跑一次 `IMapper::getService()`。
按任务约束（禁止改动手机）我没有 push 任何二进制，所以只能给到间接证据。
`FC`（fetched from client）标记在理论上也可能由 AIDL/gralloc5 那条路径注册同名 fqname 而点亮，
所以这最后一步建议在拿到 shim .so 后用一次 30 秒的 app 内 dlopen 冒烟测试确认。

## 5) 手写最小 IMapper 代理 —— 做得到，但严格劣于正路

从 hidl-gen 的 `c++-sources` 拿到的一手 wire 事实：
- descriptor = `"android.hardware.graphics.mapper@4.0::IMapper"`，客户端 `writeInterfaceToken(descriptor)`。
- 事务码：createDescriptor=1, importBuffer=2, freeBuffer=3, validateBufferSize=4, getTransportSize=5, …, **`get` = 11**。
- `IMapper::getService(name, getStub)` 的实现是
  `details::getServiceInternal<BpHwMapper>(serviceName, true, getStub)` ——
  **它完全走 libhidlbase + hwservicemanager，不在 mapper 库里**。也就是说手写的真正工作量不在 IMapper，而在 service 解析。
- `_hidl_get` 的客户端实现里对 `void* buffer` 有
  `LOG_ALWAYS_FATAL("Pointer is only supported in passthrough mode")` —— 本机 HAL 恰好就是 **passthrough**，
  所以真正的 `get` 调用是**进程内虚函数调用**，binder 只用在 `getService` 这一步。

因此「手写最小代理」要做的是重新实现：
1. 通过 `/dev/hwbinder` 原始 ioctl 实现 `IServiceManager::get`（binder 驱动协议、flat_binder_object、hidl_string 打包）；
2. hwservicemanager 的 **passthrough 解析**：dlopen 厂商 `<fqname>-impl-mediatek.so` + `dlsym("HIDL_FETCH_IMapper")`，
   并**按 `IMapper`/`BsMapper` 的 C++ vtable 布局调用返回的对象** —— 绕了一圈还是得复刻生成代码的类布局；
3. `hidl_vec` / `hidl_handle` / `Status` 的 parcel 编解码。

规模约 **600 - 1500 行**，风险高，而且**并不能绕开 std::__1 的名字问题**（descriptor 字符串与类型布局仍要一致）。
`libbinder_ndk`（纯 C 的 AIBinder）**用不了**：HIDL 在 `/dev/hwbinder`、有自己的 parcel 约定，AIBinder 只服务 `/dev/binder` 上的 AIDL。

**判定：不值得。** 官方路径已经靠「1 个文件 + 2 个编译开关」走通了。
只有当硬性要求「绝不加载 libhidlbase」时才考虑手写。

## 6) 结论、工作量与最可能的阻塞点

**工作量：0.5 - 1.5 人天**（不含联调排错）
- 下 v34 预编译头 + 库：10 分钟
- 把 VNDK include/lib 接进 meson android cross file（或用包一层 `android.hardware.graphics.mapper.pc`）：2-4 小时
  （主要琐碎点是**枚举正确的 include 根**：需要 genc++ 的 `gen`、aidl 的 `gen/include`、
  `frameworks/native/libs/{gralloc/types,nativewindow,nativebase,arect}/include`、
  `system/core/lib{utils,cutils,sync,system,vndksupport}/include`、`system/libbase/include`、
  `system/logging/liblog/include`、`system/libhidl/{base,transport}/include`、`system/libfmq/include` 等）
- 给 `u_gralloc_imapper4_api.cpp` 单独加 `-nostdinc++ -I <vndk>/…/external/libcxx/include -D_LIBCPP_SUPPORT_XLOCALE_POSIX_L_FALLBACK_H`：30 分钟（已解决）
- app 内 dlopen 冒烟测试：30 分钟

**阻塞点排序**
1. **app 命名空间能否 dlopen `/system/lib64` 的 VNDK 库（以及 passthrough 走 `/vendor/lib64/hw` 那一段）** —— 唯一无法离线证明的环节。
   间接证据很强（VINTF 声明 + impl 文件 + lshal 里 19/40 是 app uid），但必须实测。
   若失败：退到 AIDL imapper5 路线（meson 的 `dep_android_ui`），那是 API 35+ 的原生路径，用 NDK 安全的 AIDL，不用 HIDL。
2. **meson 在 `platform-sdk-version >= 35` 时选 imapper5 而不是 imapper4**（设备是 SDK 36）。
   必须显式 `-Dplatform-sdk-version=34`（或打补丁改 `src/util/u_gralloc/meson.build` / 顶层 meson 的 `elif dep_android_mapper4.found()` 分支）。
   注意 `ANDROID_API_LEVEL` 同时控制源码里 `ChromaSiting::COSITED_VERTICAL/COSITED_BOTH`（>=35）的编译分支，
   用 34 编会少这两支 —— HIDL 侧本身冻结无碍，但要知道这个差异。
3. `libc++.so` 进 DT_NEEDED 导致进程里出现第二个 libc++ —— 已证明低风险（只需要无命名空间的 ABI 符号）；
   想彻底避免就用 NDK 的 `libc++_shared.so` 当链接提供方。
4. **不要把该 TU 和 Mesa 其余部分合进同一个 TU/同一个 libc++ 编译单元**，必须保持独立翻译单元
   （Mesa 本来就是独立文件 `u_gralloc_imapper4_api.cpp`，天然满足）。

**一句话**：HIDL/imapper4 路线在本次调研中从「怕没有头文件」变成「已实测编译链接通过，只剩一次 app 内 dlopen 直测」。

---

## 复现脚本与产物（均在 /root/research/ 下）

- `tmpvndk/v33`、`tmpvndk/v34`：prebuilts/vndk 的 sparse checkout（各 130 MB，含头 + vndk-sp/llndk-stub 库）
- `hidl-gen-prebuilt` + `btlib64/libc++.so`：AOSP 预编译 hidl-gen（可直接运行）
- `hi/`（hardware/interfaces sparse）+ `lh/`（system/libhidl sparse）：hidl-gen 输入
- `gen/`：hidl-gen 生成的 mapper@4.0 头（与官方 v34 逐字节一致）
- `gensrc/`：hidl-gen 的 c++-sources（事务码证据来源）
- `test-imapper4-param.sh <vndk版本> [api]`：一键编译+链接+符号来源分析（本报告所有编译结论由它产生）
- `out-imapper4-v33/`、`out-imapper4-v34/`：`imapper4.o`、`libtest_imapper4.so`、`und.txt`
- `do-checkout-v33.sh`、`do-v34.sh`、`checkout-v33.log`、`v34.log`

设备侧全部为**只读**检查（getprop / ls / grep / lshal / 读 manifest），未 push、未安装、未改动手机。
