# 16 · P1 落地报告：CSF group_create 走 uAPI 1.18 布局并置 `csi_handlers`

> 对象：构建树 `/root/zenithblue/work/mesa`（= `build/android-v4` 的来源树）；
> 产物 `/root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so`。
> 设备（未操作）：OPPO PHZ110 / MT6989 / Immortalis-G720 MC12 / Android 16 / kbase **uAPI 1.21**。
> 本轮**未**操作手机、**未**改 `/root/mesa`、`/root/MobileGL`、**未**动手机三件套。
> 改动**只有一处**：`src/panfrost/lib/kmod/kbase_kmod.c` 的 `kbase_kmod_csf_group_create()`。
> **p2 未实施**（理由见 §6）。

---

## 0. TL;DR

1. **P1 已落地并编译通过**（`ninja` 增量，`NINJA_EXIT=0`，只重编 `kbase_kmod.c` + 重链）。
   uAPI 版本阶梯从 `1.25 / 1.6` 两档补成 **`1.25 / 1.18 / 1.6`** 三档；uAPI 判断**原样保留**，
   `< 1.18` 的老设备路径完全不变（1.6 兜底仍在最后）。
2. **新 `.so`**：size `20005600`，md5 `7f3a0e8f8a2ec70d31f11fa6fe3ab404`，
   sha256 `58ef996f9cfd5dbd88c37daea4f02bbde4fdee5818561526d21cf9adadefbec4`；
   `readelf -d` 的 **SONAME / NEEDED 与 v52 逐字节相同**（diff 为空）。
3. **插件 APK v53 已产出并逐位校验**：
   `/root/final/mgl-panvk-v53.apk`（`versionCode=53`，`versionName=5.3-p1-tiler-oom-csi`，
   sha256 `9c99af82d51b54c3ca7f97d1ffd206f5edad0f534490afa7c34be1dfc073d48e`）。
   `unzip -p ... lib/arm64-v8a/libvulkan_freedreno.so | sha256sum` = 新 `.so` 的 sha256（**非空载荷，已核**）。
4. **两条任务书里的对账信息需要更正**（不影响交付，见 §5）：
   - 任务书给的 md5 `4417b369591fc2b3df27e22019ccf3a2` 是 **OOM 当轮（01:51）的旧 `.so`**，
     现树里是 v50/v52 件 `e08e07645c16d8ebaa11ca70a09884fd`。
   - `/root/v52/` **不存在**；v52 的 manifest 是用 `aapt2 dump xmltree` 从 v52.apk 反解后重建的。
5. **并发风险（请父级协调）**：另一个会话在 11:36–11:40 已写出 `14-tiler-heap-oom.md` 与
   `tiler-work/patches/fixA|fixB|fixC`，其中 **fixA 就是同一个 P1**（还带 A/B 开关）。
   它**尚未**打进树；本轮已先把 P1 打进树，若那个会话随后按旧 md5 盲目套 patch，会冲突（§7）。

---

## 1. 改了什么（唯一一处）

**文件**：`/root/zenithblue/work/mesa/src/panfrost/lib/kmod/kbase_kmod.c`
（**该文件未被 git 跟踪**：`git status` 里是 `??`，所以回滚**不能** `git checkout`）
**函数**：`kbase_kmod_csf_group_create()`（改前 `:509-577`，改后 `:509-605`）

三处编辑：

| # | 位置（改后行号） | 内容 |
|---|---|---|
| 1 | `:517-530`（注释块） | 把"flag 试过、在本内核无效 / 不申请 CSI handler"这段**与代码矛盾的注释**重写为 Experiment P1 的说明；明确写出**1.18 布局才有 `csi_handlers`**、1.6 兜底结构体里**没有这个字段**、因此 uAPI 判断必须保留 |
| 2 | `:556`（1.25 分支结构体内） | 补 `+ .csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG,`；并把该分支成功日志 `mesa_logd` → `mesa_logi`，内容改为 `"… with TILER_OOM CSI handler (uAPI >= 1.25 layout)"` |
| 3 | `:571-600`（1.25 分支之后、1.6 兜底之前） | **新增 1.18 分支**：`if (pan_kmod_driver_version_at_least(&dev->driver, 1, 18)) { union kbase_ioctl_cs_queue_group_create_1_18 req18 = {… .csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG …}; ioctl(fd, KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_18, &req18) }`，成功打 `mesa_logi("kbase: created CSF group %u with TILER_OOM CSI handler (1.18 layout, ioctl 58)")`，失败打 `mesa_logw("kbase: 1.18 CS_QUEUE_GROUP_CREATE failed: %s; falling back to the 1.6 ABI")` 后**照旧落到 1.6** |

**版本阶梯（改后，已在目标码里逐条核实）**

```
        driver.version
   >= 1.25 ────────────► 1.25 布局(112B) ioctl 0xc070803a  csi_handlers=1
   >= 1.18 ────────────► 1.18 布局( 40B) ioctl 0xc028803a  csi_handlers=1   ← 本机 uAPI 1.21 走这条
   <  1.18 ────────────► 1.6  布局( 32B) ioctl 0xc020802a  无该字段（老设备路径，未动）
```

**完整 diff**：`/root/research/16-p1.diff`（85 行；备份件→现件的 unified diff）。核心：

```diff
@@ -534,19 +544,49 @@
             .cs_fault_report_enable = 1,
+            .csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG,
          },
       };
…
       mesa_logw("kbase: current CS_QUEUE_GROUP_CREATE failed: %s; "
-                "falling back to the 1.6 ABI",
-                strerror(errno));
+                "trying the 1.18 ABI", strerror(errno));
+   }
+
+   if (pan_kmod_driver_version_at_least(&dev->driver, 1, 18)) {
+      union kbase_ioctl_cs_queue_group_create_1_18 req18 = {
+         .in = {
+            … .cs_min = cs_queue_count, .priority = 0,
+            .tiler_max = 1, .fragment_max = 64, .compute_max = 64,
+            .csi_handlers = BASE_CSF_TILER_OOM_EXCEPTION_FLAG,
+         },
+      };
+      if (ioctl(dev->fd, KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_18, &req18) == 0) {
+         *group_handle = req18.out.group_handle;
+         mesa_logi("kbase: created CSF group %u with TILER_OOM CSI handler "
+                   "(1.18 layout, ioctl 58)", *group_handle);
+         return 0;
+      }
+      mesa_logw("kbase: 1.18 CS_QUEUE_GROUP_CREATE failed: %s; "
+                "falling back to the 1.6 ABI", strerror(errno));
    }
```

**为什么这能治**（与 14 号报告独立得到同一结论，公开 kbase r43p0 支撑）：
`mali_kbase_csf.c:handle_oom_event()` 只在
`(group->csi_handlers & BASE_CSF_TILER_OOM_EXCEPTION_FLAG) && pending_frag_count==0 && err∈{-ENOMEM,-EBUSY}`
时把 `new_chunk_ptr=0`（**可恢复的增量渲染**）交还固件；否则 `err` 非零 ⇒
`kbase_queue_oom_event()` 里 `term_queue_group()` + `report_tiler_oom_error()`
⇒ 我们抓到的那行 `kbase: CSF group 0 tiler heap OOM notification`，组已死。
panvk 侧的 handler **早就实现并注册好了**（`panvk_vX_cmd_draw.c:4500-4531` 注册/注销、
`panvk_vX_exception_handler.c:176-365` 增量渲染本体）——只是 flag 从未送达内核。

**ABI 二次自证（本轮新做，非引用）**：`/root/research/16-p1-layout-check.c`
（`gcc -O2 -I/root/zenithblue/work/mesa/include`，与 14 号的 `flagcheck` 独立）：

```
sizeof=112 offsetof(csi_handlers)=29 offsetof(neural_max)=30 offsetof(cs_fault_report_enable)=31
in bytes 24..35: 04 00 01 40 40 01 00 01 00 00 00 00      ← csi_handlers=1, neural_max=0, cs_fault=1
1.18 sizeof=40 bytes 24..39: 04 00 01 40 40 01 00 00 …    ← csi_handlers=1 @29, padding 30-31=0
KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_18=0xc028803a  KBASE_IOCTL_CS_QUEUE_GROUP_CREATE=0xc070803a
```

---

## 2. 编译（增量，exit 0）

```bash
cd /root/zenithblue/build/android-v4
export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
ninja -j2
```

日志 `/root/research/16-p1-build.log`（全文）：

```
[1/19] Generating src/git_sha1.h with a custom command
[2/4] Compiling C object src/panfrost/lib/kmod/libpankmod_lib.a.p/kbase_kmod.c.o
[3/4] Linking static target src/panfrost/lib/kmod/libpankmod_lib.a
[4/4] Linking target src/panfrost/vulkan/libvulkan_panfrost.so
NINJA_EXIT=0
```

零 warning/error；只有目标文件重编 + 一次重链（其余 1107 个 target 未动）。

### 2.1 目标码级验证（证明新分支真的进了二进制，而不是只改到源码）

`llvm-objdump -d --disassemble-symbols=kbase_kmod_csf_group_create`（新 `.so`）：

```
f6ea34:  cmp w8, #0x19                    ← 1.25 门槛
f6ea64:  mov w8, #0x803a
f6ea68:  movk w8, #0xc028, lsl #16        ← 0xc070803a（1.25 布局，+0x480000）
f6ea5c:  sturh w8, [sp, #0x1d]            ← 写 01 00 ⇒ csi_handlers(29)=1, neural_max(30)=0
f6ea60:  strb w8, [sp, #0x1f]             ← cs_fault_report_enable(31)=1
…
f6eac8:  cmp w8, #0x12                    ← 1.18 门槛（新增）
f6ead8:  mov w1, #0x803a
f6eae8:  movk w1, #0xc028, lsl #16        ← 0xc028803a = KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_18
f6eaf8:  stur s0, [sp, #0x19]             ← priority/tiler_max/fragment_max/compute_max
f6eafc:  strb w8, [sp, #0x1d]             ← csi_handlers(29) = 1  ★
f6eb00:  strh wzr, [sp, #0x1e]            ← padding(30,31) = 0
f6eb04:  str xzr, [sp, #0x20]             ← dvs_buf(32) = 0
f6eb08:  bl ioctl
…
f6eb50:  movk w1, #0xc020, lsl #16        ← 0xc020802a = 1.6 布局（未动）
```

`strings` 里两处新日志文本都在（`grep -c "TILER_OOM CSI handler"` = 2）。

### 2.2 产物指纹与 ELF 元数据

| 项 | 旧件（v52 / P1 前） | 新件（v53 / P1 后） |
|---|---|---|
| size | 20005320 | **20005600** |
| md5 | `e08e07645c16d8ebaa11ca70a09884fd` | **`7f3a0e8f8a2ec70d31f11fa6fe3ab404`** |
| sha256 | `a0b2451ee15a17bf25b91e195e0b59f4ad93732d7fa771afb6a2ac7be3853f2d` | **`58ef996f9cfd5dbd88c37daea4f02bbde4fdee5818561526d21cf9adadefbec4`** |
| 备份位置 | `/root/backup/libvulkan_panfrost.so.v52-1791171723`、…`.so.bak-v52-1791171723` | `/root/backup/v53/`、`/root/dist-v53/` |

```
readelf -d 新件 | grep -E 'SONAME|NEEDED'  与旧件 diff ⇒ 空
SONAME  : libvulkan_panfrost.so
NEEDED  : liblog.so libnativewindow.so libsync.so libm.so libz.so libdl.so libc.so   （逐条一致）
ELF     : ELF64 little-endian, Machine AArch64, Type DYN
```

> 注：`/root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so`（探针工作流的落地点）
> **本轮故意没动**（仍是 `e08e0764`），以免污染另一个会话正在做的对照。要用探针验 v53 时再
> `cp /root/dist-v53/libvulkan_panfrost.so /root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so`。

---

## 3. 插件 APK v53

打包脚本 `/root/pack_v53.sh`（照 `pack_v50.sh` 结构写，未覆盖任何既有脚本）：

* 载荷 = 新 `.so` 改名 `libvulkan_freedreno.so` + `/root/v50/lib/arm64-v8a/libMobileGL.so`
  + `/root/v50/classes.dex`（v52 用的就是这两件，md5 `2dfbe8d7…` / `45ce9eb9…`）。
* manifest 参照 `/root/v52/AndroidManifest.xml` —— **该目录不存在**，故用
  `aapt2 dump xmltree --file AndroidManifest.xml /root/final/mgl-panvk-v52.apk` 反解后重建，
  逐字符对齐；`versionCode=53`、`versionName=5.3-p1-tiler-oom-csi`。
* 调试 env **保留 v52 原样**，已用同一个 dump 做 diff 验证**完全一致**：

```
LIBGL_ES=3:POJAV_RENDERER=opengles3:MOBILEGL_BACKEND_TYPE=DirectVulkan:MOBILEGL_ESPRYT_USE_ANGLE=0:
MOBILEGL_MAGMA_R11G11B10F_FALLBACK=0:MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log:
MESA_DEBUG=1:PANVK_DEBUG=1:LIBGL_DEBUG=1:EGL_LOG_LEVEL=debug
```

* `MESA_VK_WSI_HEADLESS_SWAPCHAIN` **未加回**（dump 里出现次数 = 0，已核）。
* 签名：`apksigner`（keystore `/root/dsh-driver.keystore`，alias `dshdriver`）；
  `apksigner verify` 通过，签名者 `CN=DSH Mali Driver`，cert SHA-256
  `eba5095017e1b5266e4a60aba48196dd4056bb328e680262ce01bde2de09e565`（与 v52 同一把）。

### 3.1 载荷逐位校验（重点：上次出过空载荷 APK）

```
$ unzip -l /root/final/mgl-panvk-v53.apk | grep -E 'arm64-v8a|classes.dex'
  16956584  lib/arm64-v8a/libMobileGL.so
  20005600  lib/arm64-v8a/libvulkan_freedreno.so
      1328  classes.dex
```

```
$ unzip -p /root/final/mgl-panvk-v53.apk lib/arm64-v8a/libvulkan_freedreno.so | sha256sum
58ef996f9cfd5dbd88c37daea4f02bbde4fdee5818561526d21cf9adadefbec4  -
$ sha256sum /root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so
58ef996f9cfd5dbd88c37daea4f02bbde4fdee5818561526d21cf9adadefbec4  …    ✅ 一致
$ unzip -p … lib/arm64-v8a/libMobileGL.so | sha256sum
72919c73a7e07630f329bb4ad9605c606a9aac9e2fc6596683ee7b9f9c76848b   =  /root/v50/…/libMobileGL.so ✅
$ unzip -p … classes.dex | sha256sum
6bd3abde2c53506f1b54bb88a8069c3cc394c2fb08440e4ca475cbf8fd64f4ad   =  /root/v50/classes.dex  ✅
```

| 交付件 | 路径 | size | sha256 |
|---|---|---|---|
| 插件 APK v53 | `/root/final/mgl-panvk-v53.apk` | 10187311 | `9c99af82d51b54c3ca7f97d1ffd206f5edad0f534490afa7c34be1dfc073d48e` |
| 新驱动 `.so` | `/root/zenithblue/build/…/libvulkan_panfrost.so`（另存 `/root/dist-v53/`） | 20005600 | `58ef996f9cfd5dbd88c37daea4f02bbde4fdee5818561526d21cf9adadefbec4` |
| 旧驱动备份 | `/root/backup/libvulkan_panfrost.so.v52-1791171723` | 20005320 | `a0b2451e…` |
| 旧源码备份 | `/root/backup/kbase_kmod.c.v52-1791171723` | 72941 | md5 `e2e92db65be6b8f8fd87b9c7c8927c39` |
| P1 后源码 | `/root/backup/v53/kbase_kmod.c` | 74767 | — |

---

## 4. 回滚路径（三步任选，全部不依赖 git）

该文件**未被 git 跟踪**（`??`），所以**不能**用 15 号报告 §5 建议的 `git checkout --`。

```bash
# R1（最快）源码回滚到 P1 之前
cp -f /root/backup/kbase_kmod.c.v52-1791171723 \
      /root/zenithblue/work/mesa/src/panfrost/lib/kmod/kbase_kmod.c

# R2 二进制回滚（不想重编时：把旧 .so 放回原位）
cp -f /root/backup/libvulkan_panfrost.so.v52-1791171723 \
      /root/zenithblue/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so

# R3 只回滚 payload（插件侧）：v52 就是本次的"对照组 APK"
#     /root/final/mgl-panvk-v52.apk  (driver md5 e08e0764, versionCode 52, 无 flag)

# 回滚后重新编译
cd /root/zenithblue/build/android-v4 && \
  PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH ninja -j2
# 期望 md5 回到 e08e07645c16d8ebaa11ca70a09884fd

# 反向：重新应用 P1（已留证）
#   patch -p1 -d /root/zenithblue/work/mesa < /root/research/16-p1.diff   ← 注意此 diff 是
#   备份件→现件的完整文件 diff，path 头是绝对路径，用 `patch -p0` 或直接 cp /root/backup/v53/kbase_kmod.c
```

当前状态：**树里是 P1 版**（`/root/backup/v53/kbase_kmod.c` 是同一份，md5 可用 `sha256` 对比），
`ninja` 再跑一次应为 `no work to do`。

---

## 5. 与任务书/既有材料的三处对账更正

1. **任务书给的 `.so` md5 `4417b369591fc2b3df27e22019ccf3a2` 与现树不符**。
   `4417b369…` 是 **OOM 当轮（10-05 01:51）** 的旧件——它还留在
   `/root/zenithblue/dist/android-g720-v12-csf/libvulkan_panfrost.so.bak-1791169250`，
   以及 `/root/v13b … /root/v49` 一大批早期 staging 里。现树/v52 的件是
   `e08e07645c16d8ebaa11ca70a09884fd`（20005320 B，10-05 11:03，= v50 的 WSI 补丁件）。
   14 号报告 §1.2 已把这个关系写清，并明确"结论对两个件同时成立（v50 只动 WSI/u_gralloc）"。
   ⇒ **本轮的改动基线取 `e08e0764`（= v52），正确且可复现**。
2. **`/root/v52/` 目录不存在**（只有 APK）。manifest 由 `aapt2 dump xmltree` 反解重建，env 已 diff 验证一致。
3. **验证判据要比 15 号报告 §5/§6 更严**：14 号报告 §6.1 指出
   `tri` 崩的是 CSF exception `0xc3`（TRANSLATION_FAULT_L3，`research/12 §25.6`），
   logcat 里**没有** tiler heap OOM 行 ⇒ **`tri` 复现不了本 OOM**。
   15 号报告 §5 P1 的判据（"`l_tri.txt` 里 OOM 行是否变化"）**不成立**，应改用
   §8 的场景（真机游戏 / MGL 主界面，即父级现场拿到该行的那个负载）。

---

## 6. P2 为什么本轮不做 + 已备好的 v54 补丁

**不做 P2 的理由**（三条，都是纪律性的）：
1. **一次只改一个自变量**。P1 与 P2 是两条独立的因果链（P1 = 把 fatal OOM 变成可恢复增量渲染；
   P2 = 让堆被周期重置、少发生 OOM）。同包同发就无法分辨是哪一条起了作用，尤其
   panvk 的 TILER_OOM handler **在本机从未被激活过**（14 号 §8.3），P1 首次上机本身就需要
   一个干净的 A/B。
2. **P1 已有现成的对照组**：v52（同树、同 payload 结构、无 flag）就是 P1 的对照件，
   v52↔v53 是完美的单变量 A/B，不需要再造开关。
3. P2 的"正统"做法（恢复 `tiler_work_estimate` 生产者）涉及 **3 个文件 4 处**，而且要标定
   `PANVK_KBASE_HEAP_RENEW_WORK` 的单位（15 号报告 §5 与 14 号 §5 B2 的取法还不一样：
   一处用 `vertex.count*instance.count`，一处用 `MAX2(vertex,index)*MAX2(instance,1)`）——
   属于"改了要标定"的活，不该塞进 v53。

**v54 建议直接上"最小 2 行"版**（14 号报告 Fix B1；不需要标定单位、语义明确），
落点 `src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c:2792-2795`：

```diff
-   if (submit->tiler_work_estimate &&
-       (queue->kbase_tiler_submit_count >=
-           kbase_tiler_heap_renew_interval() ||
-        (renew_work && queue->kbase_tiler_work_count >= renew_work))) {
+   /* submit->tiler_work_estimate has no producer anywhere in this tree
+    * (panvk_cmd_buffer.h declares it, nothing writes it), so the old guard
+    * made kbase_renew_tiler_heap() unreachable: the heap could only grow,
+    * hit max_chunks and have kbase term_queue_group() on -ENOMEM.  Renew on
+    * the submit interval; keep the work threshold for when a producer
+    * exists. */
+   if (queue->kbase_tiler_submit_count >= kbase_tiler_heap_renew_interval() ||
+       (submit->tiler_work_estimate && renew_work &&
+        queue->kbase_tiler_work_count >= renew_work)) {
```

（15 号报告 P2 的"恢复生产者"版本 = 14 号 Fix B2，仍留在后备：`panvk_vX_cmd_draw.c:3457`
加 `account_tiler_work()` + `panvk_vX_gpu_queue.c:2521 panvk_queue_submit_init_cmdbufs()` 里累加。
两条都**未**落树。P3/P4/P5/P6 同样**未**落树。）

---

## 7. ⚠️ 并发会话冲突（请父级处理）

本轮开工时发现另一个会话在 **11:36–11:40** 产出：

* `/root/research/14-tiler-heap-oom.md`（604 行，独立得出同一结论：**1.18 档缺失导致 flag 从未送达**）
* `/root/research/tiler-work/patches/fixA-kbase-group-csi-handlers.diff` ← **与本次 P1 等价的同一处改动**
  （差别：fixA 用 `getenv("PANVK_KBASE_TILER_OOM_HANDLER")` 做 A/B 开关，日志打 `req.in.csi_handlers`）
* `fixB-renew-tiler-heap.diff`、`fixC-D-sizing-and-fastfail.diff`（= P2 与 P3 的草稿）
* `kbase_group_create_flag_check.c` + 编译好的 `flagcheck`（ABI 自证，结论与本轮 §1 一致）

**当前事实**：三个 fix 都**没有**进树；树里是**本轮的 P1**。
`fixA` 的 diff 头部写的是旧 md5 `e2e92db65be6b8f8fd87b9c7c8927c39`，若那个会话随后按该指纹
无条件套 patch，会因上下文不匹配而失败（或更糟：用 fuzz 硬套，产生**两个** 1.18 分支）。
请父级仲裁：**只保留一份 P1**（建议保留树里这份，因为它已编译+打包+验完），并告知那个会话
P1 已完成。

---

## 8. 上机建议（本轮未操作手机，仅列出判据）

**对照组 = v52（无 flag），实验组 = v53（有 flag）**；其余一切不变（同一 libMobileGL/classes.dex/env）。

| 配置 | 期望 logcat | 结论 |
|---|---|---|
| v52（对照） | `kbase: CSF group 0 tiler heap OOM notification` → ~10 s → `VK_ERROR_DEVICE_LOST` | 复现基线（父级现场已有此证据） |
| v53（实验） | **先找** `kbase: created CSF group N with TILER_OOM CSI handler (1.18 layout, ioctl 58)` | flag 送达内核 ✅ |
| v53 若出现 `kbase: 1.18 CS_QUEUE_GROUP_CREATE failed: … falling back to the 1.6 ABI` | 内核拒收 40 B 的 ioctl 58 ⇒ P1 无效，转 P3（打印 heap create 返回值）与 §3 的 `-EINVAL` 路线 | |
| v53 若**两条都没有** | 说明协商到的 `driver.version` **< 1.18** ⇒ 新档根本没跑（此时要去看 `kbase_kmod.c:1181 VERSION_CHECK_CSF` 的协商值） | |
| v53 正常跑 | **无** OOM 行、**不再** `-4`（可能表现为多趟 fragment 的性能下降 = 增量渲染在起作用） | P1 成立 |
| v53 仍 OOM 且无 `-4` | 增量渲染在跑但堆仍被打满 ⇒ 正是 v54(P2/Fix B1) 的用武之地 | |

⚠️ 首次上机**不要**同时上 Fix C（压天花板）——它是"复现装置"，会把变量搞混；
若要"秒级复现 OOM 来验证 P1 是否挡住"，那是 **v54 之后**的事。

---

## 9. 本轮改动/新增文件一览（全部可回滚）

| 文件 | 性质 | 说明 |
|---|---|---|
| `…/work/mesa/src/panfrost/lib/kmod/kbase_kmod.c` | **改**（唯一） | P1；备份 `kbase_kmod.c.bak-1791171723`（已移出源码树） |
| `…/build/android-v4/src/panfrost/vulkan/libvulkan_panfrost.so` | **重建** | 旧件 `.bak-v52-1791171723` + `/root/backup/` |
| `/root/final/mgl-panvk-v53.apk`（+ `.idsig`） | 新 | versionCode 53 |
| `/root/pack_v53.sh` | 新 | 打包脚本（未覆盖任何既有脚本） |
| `/root/research/16-p1.diff` | 新 | P1 unified diff（85 行） |
| `/root/research/16-p1-build.log` | 新 | ninja 全文（exit 0） |
| `/root/research/16-p1-layout-check.c` | 新 | ABI/字段偏移自证程序 |
| `/root/backup/`、`/root/backup/v53/`、`/root/dist-v53/` | 新 | 回滚件与 v53 二进制 |
| `/root/research/16-p1-p2-implementation.md` | 新 | 本报告 |

未触碰：`/root/mesa`、`/root/MobileGL`、`/root/v50`、`/root/final/mgl-panvk-v52.apk`、
`/root/zenithblue/dist/**`（探针落地点保持 `e08e0764`）、`/root/research/tiler-work/**`（另一会话的）、
以及手机（无 adb、无安装、三件套未动）。
