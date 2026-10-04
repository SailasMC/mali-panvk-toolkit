# 免 root 驱动 Mali kbase：原理与实测

> 结论先行：**普通 App（非 root、非系统）可以完整驱动 `/dev/mali0`**，
> 包括 GPU 内存分配与 mmap 读写。前提是设备把 `mali0` 的权限开给了应用（很多
> 联发科/展锐机型如此）。

---

## 1. 为什么是 kbase 而不是 DRM

Linux 上 Mali 有两条内核通路：

| 通路 | 设备节点 | 免 root 可用性 |
|---|---|---|
| **kbase**（Arm 下游驱动） | `/dev/mali0` | ✅ 常见 `0666`，普通 App 可读写 |
| **panthor / panfrost**（上游 DRM） | `/dev/dri/card0`、`renderD128` | ❌ 需要 root / 特定 SELinux 放行 |

所以在**免 root** 前提下，唯一现实的路是 kbase。
`/dev/dri/card0` 在实测设备上直接 `EACCES`，且压根没有 `renderD128` 节点。

检查你的设备：

```bash
ls -l /dev/mali0            # 看权限位
cat /proc/mali/version      # 部分机型有
```

## 2. 握手：一个容易踩的坑

kbase 要求**第一条 ioctl** 就是版本握手，且方向位是 `_IOWR`（dir=3），
不是常见的 `_IOW`：

```c
#define KBASE_IOCTL_VERSION_CHECK_CSF \
        _IOWR(KBASE_IOCTL_TYPE, 52, struct kbase_ioctl_version_check)
// KBASE_IOCTL_TYPE = 0x80
```

在实测设备上该 ioctl 展开为 `_IOWR(0x80, 52, 4)`。
**顺序错了会直接失败**，且不会给出有用的报错。

## 3. 实测数据（OPPO PHZ110 / 天玑 9300 / Immortalis-G720 MC12）

```
设备            : OPPO PHZ110 / MT6989 / Android 16 (SDK 36) / 无 root
/dev/mali0      : 存在，mode 0666，普通 App 可 open
GPU             : Immortalis-G720 MC12
PRODUCT_ID      : 0xc870
GPU_ID          : 0xc8700000
CSF             : RAW_JS_PRESENT = 0      → 确认是 CSF 前端（不是 JM）
一致性          : RAW_COHERENCY_MODE = 0  → 非一致
L2              : 512 KB / 4 slices
频率            : 1.3 GHz
uAPI 版本       : 1.21
```

已实测可用的 ioctl：

| ioctl | 展开值 | 结果 |
|---|---|---|
| `VERSION_CHECK_CSF` | `_IOWR(0x80,52,4)` | ✅ 握手成功 |
| `GET_GPUPROPS` | — | ✅ 723 字节 / 83 个属性 |
| `SET_FLAGS` | — | ✅ |
| `MEM_JIT_INIT` / `MEM_EXEC_INIT` | — | ✅ |
| `MEM_ALLOC_EX` | `_IOWR(0x80,59,64)` = `0xC040803B` | ✅ |
| `MEM_FREE` | — | ✅ |
| `CS_GET_GLB_IFACE` | — | ✅ 8 组 / 64 流 |
| mmap + 读写回读校验 | — | ✅ 数据一致 |

## 4. 最小验证流程

```bash
# 1) 快速判断有没有通路（纯 Python，不需要编译）
python3 tools/kbase_probe.py

# 2) 完整自检（需要 NDK 编译）
aarch64-linux-android26-clang -O2 -o kbase_selftest tools/kbase_selftest.c
./kbase_selftest
```

`kbase_selftest` 会依次做：

```
open /dev/mali0
  → VERSION_CHECK_CSF（必须是第一条 ioctl）
  → GET_GPUPROPS（打印 GPU 名/product id/L2/是否 CSF）
  → SET_FLAGS
  → MEM_JIT_INIT / MEM_EXEC_INIT
  → MEM_ALLOC_EX（申请一块 GPU 内存）
  → mmap 同一块内存
  → 写入魔数 → 回读比对
  → MEM_FREE
全部 PASS 才算真正"能驱动 GPU"
```

## 5. 常见失败

| 现象 | 原因 |
|---|---|
| `open` 返回 `EACCES` | 设备把 `mali0` 收紧给系统组了，免 root 路线不通 |
| 握手 ioctl 返回 `EINVAL` | 方向位写成了 `_IOW`，或不是第一条 ioctl |
| `GET_GPUPROPS` 缓冲区太短 | 按 723 字节准备；太短会被截断 |
| mmap 成功但读写异常 | 非一致设备（`RAW_COHERENCY_MODE=0`）需要按驱动要求 flush/invalidate |

## 6. 下一步

kbase 通了只代表**内核接口可用**。要跑 Vulkan 还需要一个**实现了 kbase 后端**
的用户态驱动 —— 见 [`04-build-g720-panvk.md`](04-build-g720-panvk.md)。
