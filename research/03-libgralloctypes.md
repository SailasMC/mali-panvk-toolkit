# 03 — libgralloctypes 能力研究

> 问题：Mesa 的 `u_gralloc` 把 AHardwareBuffer 转 DRM 描述时依赖 gralloc metadata（IMapper / libgralloctypes）。
> 能否用 NDK 的 AHardwareBuffer API 完全绕开 gralloc/IMapper，填出 `vk_gralloc_to_drm_explicit_layout()` 想要的东西？

- 研究时间/环境：SSH root@64.81.112.146；只读 `/root/mesa`，产物写 `/root/research/`
- Mesa 侧版本：`/root/mesa` @ `6598829019c0746aa8e473b4ae1c980cbfa6ea4b`（`VERSION = 26.3.0-devel`）
- AOSP 侧版本：`platform/frameworks/native` @ `refs/heads/main`（tip-of-tree），相关文件已落盘 `/root/research/aosp-gralloc/`

---

## 0. 结论速览（TL;DR）

1. **`Gralloc4.h`（libgralloctypes）里真正被 Mesa 用的只有 4 个函数**：`decodePixelFormatFourCC`（fourcc）、`decodePixelFormatModifier`（modifier）、`decodePlaneLayouts`（plane 的 offset/stride）、以及颜色相关的 `decodeDataspace` / `decodeChromaSiting`。
2. 这些函数**本身是纯函数**（只吃 `hidl_vec<uint8_t>` 字节流 + metadata type 常量，做 LE 解码 + 前缀校验），**但字节流只有一个来源：IMapper@4.0 HIDL 的 `get()` 跨进程调用**。libgralloctypes 里**没有任何**"从 `native_handle` 自己算出 fourcc/modifier/plane layout"的函数。
3. **AHardwareBuffer(NDK) 给不出 modifier**，这是硬缺口——整个 Android 公开 API（NDK 与 VNDK）都没有任何函数返回 DRM modifier。它能给的只有 `width/height/layers/format(HAL_PIXEL_FORMAT 原值)/usage/stride(像素)`。
4. 因此**不能"完全绕开"**：可以做到"不依赖 IMapper 也能构造 `VkImageDrmFormatModifierExplicitCreateInfoEXT`"的，只有**线性布局单平面（或多平面非压缩）**这一受限子集，等价于 Mesa 现成的 `U_GRALLOC_TYPE_FALLBACK` 后端——`modifier` 只能写 `DRM_FORMAT_MOD_LINEAR`/`INVALID`，`offset` 只能写 0，`rowPitch = desc.stride × bpp`。
5. 若要求"只用纯 NDK（只链 `libandroid.so`）"：连 **dma-buf fd 都拿不到**（`AHardwareBuffer_getNativeHandle` 只存在于 VNDK 头 `vndk/hardware_buffer.h`），`VkImportMemoryFdInfoKHR` 无法构造 → **完全不可行**；必须至少 VNDK 或走 `ANativeWindowBuffer`（EGL/WSI 路径自带 `native_handle`）。
6. 对 AFBC / UBWC / CCS 这类压缩或带 metadata plane 的 buffer，**NDK-only 方案必然给出错误布局**，在 panvk 上会走到 `assert(panvk_image_can_use_mod(...))`（`src/panfrost/vulkan/panvk_image.c:307`）或静默读错像素。

---

## 1. 拉取的 AOSP 源码（证据）

| 路径（AOSP） | 本地副本 | 说明 |
|---|---|---|
| `frameworks/native/libs/gralloc/types/include/gralloctypes/Gralloc4.h` (433 行) | `/root/research/aosp-gralloc/include/gralloctypes/Gralloc4.h` | libgralloctypes 公开头 |
| `frameworks/native/libs/gralloc/types/Gralloc4.cpp` (1345 行) | `/root/research/aosp-gralloc/Gralloc4.cpp` | 编解码实现 |
| `frameworks/native/libs/gralloc/types/Android.bp` | （见下） | `cc_library{name:"libgralloctypes", vendor_available:true, min_sdk_version:29, srcs:["Gralloc4.cpp"], shared_libs:["android.hardware.graphics.mapper@4.0","libhidlbase","liblog"]}` |
| `frameworks/native/libs/nativewindow/include/android/hardware_buffer.h` (636 行) | `hardware_buffer.h` | **NDK** AHardwareBuffer API |
| `frameworks/native/libs/nativewindow/include/vndk/hardware_buffer.h` | `vndk_hardware_buffer.h` | **VNDK** 扩展（`AHardwareBuffer_getNativeHandle` / `getDataSpace`） |
| `frameworks/native/libs/nativewindow/AHardwareBuffer.cpp` (814 行) | `AHardwareBuffer.cpp` | `describe` / `lockPlanes` / 格式转换实现 |
| `frameworks/native/libs/ui/GraphicBufferMapper.cpp` / `Gralloc4.cpp` / `Gralloc5.cpp` | 同名文件 | mapper4 / mapper5 两条实现路径 |
| `hardware/interfaces/graphics/mapper/stable-c/implutils/include/android/hardware/graphics/mapper/utils/IMapperMetadataTypes.h` (627 行) | `IMapperMetadataTypes.h` | **mapper5 的 metadata 解码器（header-only，取代 libgralloctypes）** |
| `hardware/interfaces/graphics/common/aidl/.../PlaneLayout.aidl` / `StandardMetadataType.aidl` / `PlaneLayoutComponent.aidl` | （会话内已读） | metadata 语义与字节序规范 |

Mesa 侧关键文件（只读引用）：
`src/util/u_gralloc/{u_gralloc.c,u_gralloc.h,u_gralloc_internal.c,u_gralloc_internal.h,u_gralloc_imapper4_api.cpp,u_gralloc_imapper5_api.cpp,u_gralloc_fallback.c,u_gralloc_cros_api.c,u_gralloc_qcom.c}`、
`src/vulkan/runtime/vk_android.c`、`src/panfrost/vulkan/panvk_android.c`、`src/panfrost/vulkan/panvk_image.c`。

---

## 2. `Gralloc4.h` 公开 API 清单（namespace `android::gralloc4`）

### 2.1 `MetadataType_*` 常量（= `{GRALLOC4_STANDARD_METADATA_TYPE, StandardMetadataType 值}`）

| 常量 | StandardMetadataType | 值 | 类型 | 被 Mesa u_gralloc 使用 |
|---|---|---|---|---|
| `MetadataType_BufferId` | BUFFER_ID | 1 | uint64 | |
| `MetadataType_Name` | NAME | 2 | string | |
| `MetadataType_Width` | WIDTH | 3 | uint64 | |
| `MetadataType_Height` | HEIGHT | 4 | uint64 | |
| `MetadataType_LayerCount` | LAYER_COUNT | 5 | uint64 | |
| `MetadataType_PixelFormatRequested` | PIXEL_FORMAT_REQUESTED | 6 | int32(HIDL V1_2 PixelFormat) | |
| **`MetadataType_PixelFormatFourCC`** | PIXEL_FORMAT_FOURCC | **7** | **uint32** | ✅ imapper4 backend |
| **`MetadataType_PixelFormatModifier`** | PIXEL_FORMAT_MODIFIER | **8** | **uint64** | ✅ imapper4 backend |
| `MetadataType_Usage` | USAGE | 9 | uint64 | |
| `MetadataType_AllocationSize` | ALLOCATION_SIZE | 10 | uint64 | |
| `MetadataType_ProtectedContent` | PROTECTED_CONTENT | 11 | uint64 | |
| `MetadataType_Compression` | COMPRESSION | 12 | ExtendableType | |
| `MetadataType_Interlaced` | INTERLACED | 13 | ExtendableType | |
| `MetadataType_ChromaSiting` | CHROMA_SITING | 14 | ExtendableType | ✅ |
| **`MetadataType_PlaneLayouts`** | PLANE_LAYOUTS | **15** | **vector\<PlaneLayout\>** | ✅ imapper4 backend |
| `MetadataType_Crop` | CROP | 16 | vector\<Rect\> | |
| `MetadataType_Dataspace` | DATASPACE | 17 | int32(Dataspace) | ✅ |
| `MetadataType_BlendMode` | BLEND_MODE | 18 | int32 | |
| `MetadataType_Smpte2086` | SMPTE2086 | 19 | optional | |
| `MetadataType_Cta861_3` | CTA861_3 | 20 | optional | |
| `MetadataType_Smpte2094_40` | SMPTE2094_40 | 21 | optional | |
| `MetadataType_Smpte2094_10` | SMPTE2094_10 | 22 | optional | |
| （无对应常量） | STRIDE | 23 | uint32 | ⚠️ AIDL 有定义但 libgralloctypes **无编解码函数** |

### 2.2 函数清单

**A. 每个标准 metadata 的 `encodeX`/`decodeX` 对**（`Gralloc4.h:255-350` 段 → `Gralloc4.cpp:934-1148`）
`BufferDescriptorInfo`、`BufferId`、`Name`、`Width`、`Height`、`LayerCount`、`PixelFormatRequested`、**`PixelFormatFourCC`**、**`PixelFormatModifier`**、`Usage`、`AllocationSize`、`ProtectedContent`、`Compression`、`Interlaced`、`ChromaSiting`、**`PlaneLayouts`**、`Crop`、`Dataspace`、`BlendMode`、`Smpte2086`、`Cta861_3`、`Smpte2094_40`、`Smpte2094_10`。

**B. 通用底层 codec（internal，`Gralloc4.cpp:195-800`）**
`encodeMetadataType` / `validateMetadataType`、`encode<T>` / `decode<T>`、`encodeMetadata` / `decodeMetadata`（**带 metadata type 前缀校验 + `hasRemainingData()` 完整性校验**）、`encodeOptionalMetadata` / `decodeOptionalMetadata`、`encodeInteger<T>`/`decodeInteger<T>`、`encodeString`/`decodeString`、`encodeByteVector`/`decodeByteVector`、`encodeExtendableType`/`decodeExtendableType`、`encodeXyColor`/`decodeXyColor`、`encodeRect`/`decodeRect`、`encodePlaneLayoutComponent(s)`/`decodePlaneLayoutComponent(s)`、`encodePlaneLayout(s)`/`decodePlaneLayout(s)`、`encodeCrop`/`decodeCrop`。

**C. vendor/私有 metadata 泛型 codec（`Gralloc4.h` 末尾）**
`encodeUint32/decodeUint32`、`encodeInt32/decodeInt32`、`encodeUint64/decodeUint64`、`encodeInt64/decodeInt64`、`encodeFloat/decodeFloat`、`encodeDouble/decodeDouble`、`encodeString/decodeString`（都带 `MetadataType` 参数，走 `isStandardMetadataType` 分派到标准或 vendor 前缀）。

**D. 谓词 / 取值 / 名字 helper**
`isStandardMetadataType`、`isStandardCompression`、`isStandardInterlaced`、`isStandardChromaSiting`、`isStandardPlaneLayoutComponentType`；
`getStandardMetadataTypeValue`、`getStandardCompressionValue`、`getStandardInterlacedValue`、`getStandardChromaSitingValue`、`getStandardPlaneLayoutComponentTypeValue`；
`getCompressionName`、`getInterlacedName`、`getChromaSitingName`、`getPlaneLayoutComponentTypeName`。

### 2.3 Mesa 用到的 4 个解码函数的字节格式（`Gralloc4.cpp`）

| Mesa 调用点 | 函数 | 字节流内容 |
|---|---|---|
| `u_gralloc_imapper4_api.cpp:106` | `decodePixelFormatFourCC(vec, uint32_t*)` | `[len=44][name][value=7]` + `uint32 fourcc` (4B LE) |
| `u_gralloc_imapper4_api.cpp:118` | `decodePixelFormatModifier(vec, uint64_t*)` | header + `uint64 modifier` (8B LE) |
| `u_gralloc_imapper4_api.cpp:68` | `decodePlaneLayouts(vec, vector<PlaneLayout>*)` | header + `int64 count` + 每平面：`int64 ncomp`，每个 comp `header(PlaneLayoutComponentType)+int64 offsetInBits+int64 sizeInBits`，随后依次 `int64 offsetInBytes, sampleIncrementInBits, strideInBytes, widthInSamples, heightInSamples, totalSizeInBytes, horizontalSubsampling, verticalSubsampling` |
| `u_gralloc_imapper4_api.cpp:194/201` | `decodeDataspace` / `decodeChromaSiting` | header + `int32` / ExtendableType(string+int64) |

⚠️ 两个易踩的规范坑：
- `StandardMetadataType.aidl` 的 PLANE_LAYOUTS 注释里**漏了 `heightInSamples`**（写的是 `widthInSamples, totalSizeInBytes, ...`），实际代码 `Gralloc4.cpp:699-776` 是**含 `heightInSamples` 的**。以代码为准。
- `decodeMetadata()` 会先 `validateMetadataType()` 校验流里的 metadata type 必须等于调用方传入的常量；mismatch 直接 `BAD_VALUE`。→ 这意味着**字节流必须整体来自 mapper，不能把 payload 单独抠出来喂进去**。

---

## 3. 纯函数，还是必须经 IMapper？

### 3.1 分层结论

| 层 | 是纯函数吗 | 依赖 | 说明 |
|---|---|---|---|
| `android::gralloc4::decodeXxx(byte stream, out*)` | ✅ **纯函数** | 链接期：`libgralloctypes` + `libhidlbase` + `android.hardware.graphics.mapper@4.0`；头文件 `gralloctypes/Gralloc4.h` | 无 I/O、无 binder、无全局状态；只做 LE 解码与断言。**可离线单测/可自己重写** |
| 拿到 byte stream | ❌ **必须 IMapper 服务** | `IMapper@4.0::get(handle, metadataType, cb)` → binder/HIDL 跨进程 | gralloc HAL 进程必须起得来；`u_gralloc_imapper4_api.cpp:47-60` 就是这层薄封装 |
| mapper5 路径 | ❌ 必须 IMapper(AIDL) | `android.hardware.graphics.mapper.IMapper` (stable-C) | 返回 `byte[]`，解码器换成 `<android/hardware/graphics/mapper/utils/IMapperMetadataTypes.h>` 的 `StandardMetadata<T>::value::decode()`，header-only，**libgralloctypes 在这条路径上完全不被使用** |

### 3.2 因此"能直接用"与"必须先拿到字节"的划分

**可以直接用（把 libgralloctypes 当纯解码库）**：
- 所有 `decodeXxx` / `encodeXxx`、`decodeUint32/Int32/Uint64/...`、`isStandardXxx`、`getStandardXxxValue`、`getXxxName`。
- Mesa 已经在 `u_gralloc_imapper4_api.cpp` 里这么用——但它**必须先有 IMapper 才能拿到参数**。

**必须先经 IMapper（无法绕）**：
- `PixelFormatFourCC`、`PixelFormatModifier`、`PlaneLayouts`、`Dataspace`、`ChromaSiting`、`Usage`、`AllocationSize`、`ProtectedContent`、`Compression` 的**取值**。
- libgralloctypes **没有** `native_handle → 上述字段` 的任何函数；它的输入类型就是 `hidl_vec<uint8_t>`，即"已从 mapper 取回的字节"。

### 3.3 对 Mesa 的对应关系（代码证据）

- mapper4 路径：`src/util/u_gralloc/u_gralloc_imapper4_api.cpp`，`#include <gralloctypes/Gralloc4.h>`，`GetMetadata()` 包 `mapper->get(...)`，再调 `decodeXxx`。由 `USE_IMAPPER4_METADATA_API` 编译开关控制（`u_gralloc.c:26-28`）。
- mapper5 路径：`src/util/u_gralloc/u_gralloc_imapper5_api.cpp`，**不 include gralloctypes**，改用 `android::GraphicBufferMapper::get().getPixelFormatModifier() / getPlaneLayouts() / getDataspace()`（libui），解码在 `libs/ui/Gralloc5.cpp` 内用 AIDL 版解码器完成。AOSP 侧 `libs/ui/Gralloc5.cpp:25` 就是 `#include <android/hardware/graphics/mapper/utils/IMapperMetadataTypes.h>`。
- 两条路径**都指向同一个 gralloc HAL 服务**，只是协议不同（HIDL vs AIDL）。**没有任何一条路是纯本地计算。**

### 3.4 部署事实（用于判断可行性）

- `libgralloctypes`：`vendor_available: true`、`min_sdk_version: 29`、`double_loadable: true` → vendor 分区里的 Mesa 可以链接它；但它会把 `libhidlbase` + `android.hardware.graphics.mapper@4.0` 拖进来（HIDL 运行时），在 Android 13+ 上是"为用 4 个解码函数引入整个 HIDL 栈"。
- mapper5 的解码器 `libimapper_providerutils` 是 **header-only**（`cc_library_headers`，`export_include_dirs: ["implutils/include"]`）+ `libimapper_stablec`（也是 headers）→ 理论上可以只要头文件、自己实现 binder 调用，但仍是跨进程。

---

## 4. AHardwareBuffer(NDK) 能提供什么？

### 4.1 `AHardwareBuffer_describe()` 返回结构体（`android/hardware_buffer.h:365-382`）

```c
typedef struct AHardwareBuffer_Desc {
    uint32_t width;   // 像素
    uint32_t height;  // 像素
    uint32_t layers;  // 图层数
    uint32_t format;  // 见下方说明
    uint64_t usage;   // AHardwareBuffer_UsageFlags 位域
    uint32_t stride;  // 行 stride，单位＝像素（不是字节！）
    uint32_t rfu0;    // 保留，0
    uint64_t rfu1;    // 保留，0
} AHardwareBuffer_Desc;   // 共 8 个字段
```

实现证据（`AHardwareBuffer.cpp:177-191`）：
```c
outDesc->format = AHardwareBuffer_convertFromPixelFormat(uint32_t(gbuffer->getPixelFormat()));
outDesc->stride = gbuffer->getStride();
outDesc->usage  = AHardwareBuffer_convertFromGrallocUsageBits(gbuffer->getUsage());
```
而 `convertFromPixelFormat/convertToPixelFormat/convertFromGrallocUsageBits` 全是**恒等函数**（`AHardwareBuffer.cpp:740-747`, `795-797`）：
```c
uint32_t AHardwareBuffer_convertFromPixelFormat(uint32_t hal_format) { return hal_format; }
```
→ **`desc.format` 就是原始 `HAL_PIXEL_FORMAT_*` 值**（可能是 `IMPLEMENTATION_DEFINED`=0x22、`YV12`=0x32315659 等），这恰好是 `u_gralloc_buffer_handle.hal_format` 需要的、
**`desc.stride` 单位是像素**，与 `u_gralloc_buffer_handle.pixel_stride` 语义一致（Mesa fallback 就是 `stride = pixel_stride * bpp`）。

### 4.2 其它 NDK / VNDK API 能给什么

| API | 档位 | 给什么 |
|---|---|---|
| `AHardwareBuffer_allocate/acquire/release` | NDK | 生命周期 |
| `AHardwareBuffer_describe` | NDK(26) | 上表 8 字段 |
| `AHardwareBuffer_lock` / `lockAndGetInfo` | NDK(26/29) | 单平面 CPU 指针 + bytesPerPixel/bytesPerStride；需 CPU usage |
| `AHardwareBuffer_lockPlanes` | NDK(29) | `planeCount` + 每平面 `{data, pixelStride, rowStride}`；需 CPU usage；YUV 走 `lockAsyncYCbCr`，否则单平面 |
| `AHardwareBuffer_isSupported` | NDK(29) | 可分配性 |
| `AHardwareBuffer_getId` | NDK(31) | 唯一 id（与布局无关） |
| `AHardwareBuffer_send/recvHandleToUnixSocket` | NDK(26) | 跨进程传引用（不是 dma-buf fd） |
| **`AHardwareBuffer_getNativeHandle`** | **仅 VNDK**（`vndk/hardware_buffer.h`） | `const native_handle_t*` → 才能拿 dma-buf fd |
| `AHardwareBuffer_getDataSpace/setDataSpace` | 仅 VNDK（API V 起） | dataspace |
| `AHardwareBuffer_to_GraphicBuffer` / `to_ANativeWindowBuffer` | libui（非公开 NDK） | GraphicBuffer，可 `getStride/getUsage/getPixelFormat`，**仍无 modifier** |

**没有任何 API（NDK、VNDK、libui、GraphicBuffer）返回 DRM fourcc / DRM modifier / plane offset。**

### 4.3 一一对应表：Mesa 想要的输出 ← AHB 能给的输入

Mesa 目标（`src/vulkan/runtime/vk_android.c:138-196` `vk_gralloc_to_drm_explicit_layout()`）：

```c
VkImageDrmFormatModifierExplicitCreateInfoEXT {
    uint64_t drmFormatModifier;              // ← info.modifier
    uint32_t drmFormatModifierPlaneCount;    // ← info.num_planes
    const VkSubresourceLayout* pPlaneLayouts; // ← {offset, rowPitch}
};
// VkSubresourceLayout 共 5 字段：offset / size / rowPitch / arrayPitch / depthPitch
// Mesa 只填 offset 与 rowPitch，其余 memset 为 0
```

| Mesa 需要的字段 | 来源（IMapper 路径） | AHardwareBuffer 可得？ | AHB 侧表达式 | 缺口 / 风险 |
|---|---|---|---|---|
| `drmFormatModifier` (uint64) | `decodePixelFormatModifier` (PIXEL_FORMAT_MODIFIER=8) | ❌ **完全不可得** | — | 无任何公开 API。只能猜 `DRM_FORMAT_MOD_LINEAR`(0) 或 `DRM_FORMAT_MOD_INVALID`。AFBC/UBWC/CCS/PVR 全部丢失 |
| `drmFormatModifierPlaneCount` | `layouts.size()` | ⚠️ 部分 | 由 `desc.format` 推平面数（NV12/0x11→2，YV12/0x32315659→3，0x23→3，RGB→1）；或 `lockPlanes().planeCount` | 压缩格式的 metadata plane / 多 dma-buf 拆分不可知 |
| `pPlaneLayouts[i].offset` | `layouts[i].offsetInBytes` | ⚠️ 部分 | `lockPlanes().planes[i].data - planes[0].data`（要求 buffer 可 CPU 锁） | GPU-only usage、`PROTECTED_CONTENT`、AFBC 缓冲都返回 `planeCount=0`/失败；拿不到就只能 0 |
| `pPlaneLayouts[i].rowPitch` | `layouts[i].strideInBytes` | ⚠️ 部分 | RGB 单平面：`desc.stride × bpp(desc.format)`；YUV/可锁：`lockPlanes().planes[i].rowStride` | `desc.stride` 只有**一个**值且单位是像素；YUV 的 chroma stride 只能靠 lockPlanes；`bpp` 需自己查表（Mesa `get_hal_format_bpp()`） |
| （调用方还要）`drm_fourcc` | `decodePixelFormatFourCC` (7) | ⚠️ 可推导 | `HAL format → DRM fourcc` 查表（Mesa `get_fourcc_from_hal_format()` / `droid_yuv_formats[]`） | `IMPLEMENTATION_DEFINED`(0x22) 无法解析，只能硬编码猜 RGBX_8888/NV12（Mesa 里有 `issuetracker.google.com/32077885` 的 HACK 注释）；vendor 私有格式不能解析 |
| dma-buf fds（`VkImportMemoryFdInfoKHR`） | `native_handle->data[fd_index]` | ⚠️ 仅 VNDK | `AHardwareBuffer_getNativeHandle(ahb)` | **纯 NDK 拿不到 fd**；`sendHandleToUnixSocket` 只传引用 |
| 颜色：dataspace / chroma siting | `decodeDataspace` / `decodeChromaSiting` | ⚠️ 仅 VNDK | `AHardwareBuffer_getDataSpace()`（API V） | chroma siting 无 API → 只能默认 MIDPOINT |
| `size` / `arrayPitch` / `depthPitch` | — | 不需要 | Mesa 自己也不填 | 无影响 |

### 4.4 为什么不能用 `desc.stride` 单独构造完整 `VkSubresourceLayout`

- 单位不同：`VkSubresourceLayout.rowPitch` 是**字节**，`desc.stride` 是**像素**；需要 `× bpp`，而 bpp 又要由 `desc.format` 查表（`IMPLEMENTATION_DEFINED` 就查不到）。
- 只有一行 stride：多平面 YUV 的 chroma plane 有各自的 stride。
- 只有 `offset`/`rowPitch` 之外的信息（`size`）缺失不影响 Mesa，但 **modifier 缺失直接决定整个图像的内存解释方式**——这是语义级的缺口，不是精度问题。

---

## 5. 结论：能否完全绕开 gralloc/IMapper？

### 5.1 三种"绕开"的强度对比

| 方案 | 依赖 | 能填出 `VkImageDrmFormatModifierExplicitCreateInfoEXT`？ | 适用范围 |
|---|---|---|---|
| **纯 NDK**（只 `libandroid.so`） | NDK | ❌ 不行：**连 dma-buf fd 都拿不到**，`VkImportMemoryFdInfoKHR` 无法构造 | — |
| **VNDK + NDK**（`AHardwareBuffer_getNativeHandle` + `describe` + 可选 `lockPlanes`） | VNDK | ⚠️ 仅对**线性、无压缩、平面可枚举**的 buffer | RGB 单平面、可锁的 YUV |
| **Mesa `U_GRALLOC_TYPE_FALLBACK`**（已经是"绕开 IMapper"的实现） | gralloc0 hwmodule（`lock_ycbcr`）+ native_handle | ⚠️ 同上，且 `modifier = DRM_FORMAT_MOD_INVALID`（`u_gralloc_fallback.c:159`），仅 Freedreno 特判 UBWC | 同上 + Freedreno UBWC |
| **走 IMapper（mapper4 libgralloctypes / mapper5 IMapperMetadataTypes）** | gralloc HAL 服务 | ✅ 完整（fourcc+modifier+每平面 offset/stride） | 全部，含压缩 |

### 5.2 缺口清单（"完全绕开"做不到的原因）

1. **modifier 无 API**：NDK/VNDK/libui 全都没有；这是唯一"无论怎么努力都拿不到"的字段，而它决定了 AFBC/UBWC/CCS 等布局。→ 无法构造正确的 explicit modifier 图像。
2. **fd 只有 VNDK**：`AHardwareBuffer_getNativeHandle` 不在 NDK。→ 纯 NDK 方案在第一步就断。
3. **plane offset/stride 只在可 CPU 锁定时近似可得**：`lockPlanes` 要求 CPU usage flags；GPU-only、受保护、压缩 buffer 拿不到。
4. **HAL format → fourcc 的推导是有洞的映射**：`IMPLEMENTATION_DEFINED`(0x22)、vendor 私有格式无法解析（Mesa 自己只有 8 条硬编码映射 + YUV 需要在 `lock_ycbcr` 返回的指针顺序/chroma_step 上二次判定）。
5. **多个 dma-buf 的 disjoint 情形无法判定**：Mesa 的 disjoint 检测依据正是 `offsets[i] == 0`（来自 PlaneLayouts），绕开后无法检测，只能当单 fd 处理。

### 5.3 对 Panfrost (panvk) 的实际后果

- `panvk_android.c:121`（ANB）与 `:220+`（AHB）都调用 `vk_android_get_anb_layout` / `vk_android_get_ahb_layout`，其最终落点就是 `vk_gralloc_to_drm_explicit_layout`。失败即 `VK_ERROR_INVALID_EXTERNAL_HANDLE`。
- 若"猜"出来一个 modifier，`panvk_image_get_explicit_mod()`（`panvk_image.c:319-331`）里
  `assert(panvk_image_can_use_mod(image, iusage, mod, false))` 会校验该 modifier 必须属于 Panfrost/内核（panthor）支持的集合。用 `DRM_FORMAT_MOD_LINEAR` 去解释一个实际是 AFBC 的 buffer，**assert 可能通过但像素全错**（更糟）；用 `DRM_FORMAT_MOD_INVALID` 则不在支持集合内 → assert/失败。
- 结论：**fallback 路线只适合 WSI/线性 scanout buffer**；一旦 compositor 或 App 传来压缩 buffer，必须走 IMapper。

### 5.4 给父任务的可执行建议

1. **最省事的正确路线**：保留 Mesa 的 `u_gralloc` + mapper backend（mapper5/AIDL 优先，mapper4+libgralloctypes 次之，CrOS perform 最快）。若目标设备是 MTK/Mali，`U_GRALLOC_TYPE_GRALLOC4` 或 `U_GRALLOC_TYPE_CROS` 都不存在时，`U_GRALLOC_TYPE_QCOM` / `FALLBACK` 会退化成"猜四平面/猜 modifier"，这正是 panvk 在 Android 上 import 失败的高发点。
2. **如果确实要"无 gralloc 依赖"的自研路径**：要么在 vendor 侧直接调 gralloc HAL（等价于 IMapper，还是绕不开），要么：
   - 只支持 `desc.format ∈ {RGBA_8888, RGBX_8888, RGB_565, BGRA_8888, RGBA_1010102, RGBA_FP16}` 的线性 buffer；
   - 强制 `modifier = 0 (LINEAR)`、`num_planes = 1`、`offset = 0`、`rowPitch = desc.stride × bpp`；
   - fd 从 `AHardwareBuffer_getNativeHandle`（VNDK）或 `ANativeWindowBuffer.handle` 取；
   - 对 `YUV`（0x11/0x23/0x32315659）走 `lock_ycbcr`/`lockPlanes` 拿 offsets/strides，并在不可锁时直接拒绝而不是猜。
   这条路线与 `u_gralloc_fallback.c` 等价——**可以直接复用 Mesa 已有后端，不必新写**。
3. **验证手段**：用 `dumpsys SurfaceFlinger` / `gralloc` metadata dump（`libs/ui/Gralloc5.cpp:348 dumpBufferCommon`）在生产设备上打印真实 PIXEL_FORMAT_MODIFIER，与自研推导值对比，误差应为 0。

---

## 6. 复现命令

```bash
# AOSP 文件拉取（gitiles base64 通道）
B=https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/libs/gralloc/types
curl -s "$B/include/gralloctypes/Gralloc4.h?format=TEXT" | base64 -d
curl -s "$B/Gralloc4.cpp?format=TEXT" | base64 -d
# NDK / VNDK AHardwareBuffer
curl -s "https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/libs/nativewindow/include/android/hardware_buffer.h?format=TEXT" | base64 -d
curl -s "https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/libs/nativewindow/include/vndk/hardware_buffer.h?format=TEXT" | base64 -d
# mapper5 解码器
curl -s "https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/graphics/mapper/stable-c/implutils/include/android/hardware/graphics/mapper/utils/IMapperMetadataTypes.h?format=TEXT" | base64 -d

# Mesa 侧证据
grep -n "u_gralloc_get_buffer_basic_info" -r /root/mesa/src
sed -n '138,196p' /root/mesa/src/vulkan/runtime/vk_android.c        # vk_gralloc_to_drm_explicit_layout
sed -n '620,640p' /root/mesa/src/vulkan/runtime/vk_android.c        # vk_android_get_ahb_layout
sed -n '307,331p' /root/mesa/src/panfrost/vulkan/panvk_image.c      # modifier assert
```
