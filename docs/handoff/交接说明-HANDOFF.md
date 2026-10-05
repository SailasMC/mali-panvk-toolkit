# Mali PanVK 自编驱动 · 项目交接说明（HANDOFF）

> 面向"接手这个项目的下一个 AI / 下一个会话"。
> 写完时间：2026-10-05 晚（本会话内）。
> 配套：本地 `研究论文/`（约 40 篇）+ 服务器 `/root/research/`（01–43 号报告）+ git 仓库。
> **任何与本文件冲突的口述，一律以"有日志/哈希证据的一方"为准。**

---

## 0. 一句话现状

**自编 Mesa PanVK（kbase/CSF 后端）已经能在真机上跑真实 Minecraft（判据行正常、真交换链 2376x1080、连续渲染数十秒），
但会在几十秒后卡死：fence 永远等不到提交完成 → 用户态 10 秒看门狗 → `VK_ERROR_DEVICE_LOST`。
当前已被钉死到：故障发生在【被调用的 callee 流内部】——固件进入了它，却再也没有返回。**

---

## 1. 目标与红线

**目标**：不 root，用自编开源 Mali 驱动跑 Minecraft Java（26.3 Fabric + Sodium…），
经 ZalithLauncher2（ZL2，Pojav 内核）+ MobileGL 的 DirectVulkan 后端呈现；目标是**稳定可玩**，而不只是"能出一帧"。

**红线（绝对不要碰）**：
- 三个 App 进程**不得**结束/清理：`com.netease.cloudmusic`、`roro.stellar.manager`、`com.dsharnessmobile.shell`
  （分别承载 ul / 无障碍 / shell 服务）
- 不要修改这些服务器目录（视为只读的构建输入）：
  `/root/mesa`（**不是**我们的构建树）、`/root/MobileGL`
- 不要 `git checkout/stash/reset`、不要 `rm -rf` 既有目录（被拦就换名，别重试）
- 不要把任何 API 密钥写进仓库/工作区/报告

---

## 2. 设备与软件栈（已实测）

| 项 | 值 |
|---|---|
| 设备 | OPPO PHZ110（MT6989），**Immortalis-G720 MC12 = Panfrost arch v12**（注意：arch14 是 2025 G1 系列，别搞混）|
| 系统 | Android 16 / API 36，内核 6.1.157，**无 root** |
| GPU 节点 | `/dev/mali0` 权限 0666（O_RDWR 可用）|
| kbase uAPI | **1.21**（实测日志：`kbase: tiler heap renewal (uAPI 1.21, …)`）|
| 厂商 blob（对照用）| `Mali-G720-Immortalis MC12`、Vulkan 1.3.247、Driver 44.1.0 |
| 我们的驱动 | `Mali-G720 MC12`、Vulkan **1.4.363**、Driver **26.2.99**（= Mesa fork "zenithblue"）|
| 判据行（确认谁在跑）| `OpenGL Renderer: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)` |
| 真交换链 | `Swapchain created, extent = 2376x1080, swapchain imageCount = 3` |

**游戏/启动器**：ZL2 包名 `com.movtery.zalithlauncher.v2`；版本目录
`…/Android/data/com.movtery.zalithlauncher.v2/files/.minecraft/versions/26.3 Fabric/ZalithLauncher/`（日志 `latest_game.log`）。

---

## 3. 交付物与产物位置

### 3.1 插件 APK（我们的驱动以"渲染器插件"形式提供）
- 包名 `com.dsh.plugin.driver.g720`，签名 keystore `/root/dsh-driver.keystore`（pass `android`，alias `dshdriver`）
- **同签名 + versionCode 递增 ⇒ 可静默覆盖安装**（`cmd package install -r -t`）✓
- 新包名首次安装必须**人手点一次** ✗
- manifest 关键：`fclPlugin=true`、`renderer=magma_panvk:libMobileGL.so:libMobileGL.so`、`des`、
  `pojavEnv`/`boatEnv`（**只有 pojavEnv 被 ZL2 解析**），env 用 `K=V:K=V` 冒号分隔
- 工作 env（v50 起一致）：
  `LIBGL_ES=3:POJAV_RENDERER=opengles3:MOBILEGL_BACKEND_TYPE=DirectVulkan:MOBILEGL_ESPRYT_USE_ANGLE=0:MOBILEGL_MAGMA_R11G11B10F_FALLBACK=0:MOBILEGL_LOG_FILE_PATH=/sdcard/MG/mgl.log:MESA_DEBUG=1:PANVK_DEBUG=1,kbase_diag:LIBGL_DEBUG=1:EGL_LOG_LEVEL=debug:PANVK_KBASE_HEAP_RENEW_INTERVAL=32`
  （`PANVK_DEBUG=1` 本身是空操作 ✗，必须写成 `PANVK_DEBUG=1,kbase_diag` ✓）

### 3.2 ⚠️ 真机"实际加载位"（最重要的一条工程事实）
```
转发垫片找 ICD 的顺序 = [自身目录, 裸名, /data/local/tmp]
⇒ 真机上"自身目录"拿不到（dladdr 给的是 APK 内路径）
⇒ ★【实际生效的驱动 = /data/local/tmp/libvulkan_freedreno.so】✓✓
   ⇒ 每次上机必须：unzip -p <apk> lib/arm64-v8a/libvulkan_freedreno.so > /data/local/tmp/libvulkan_freedreno.so
     并 md5/sha 校验 ✓（否则你以为在测新版，其实在测旧版 ✗ —— 本会话曾因此让 v50–v54 五轮白跑 ✗）
```

### 3.3 服务器
```
ssh -i $HOME/.ssh/id_ed25519 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@64.81.112.146
```
- 构建树 `/root/zenithblue/work/mesa`（**不是** `/root/mesa`）；构建目录 `/root/zenithblue/build/android-v4`
- 编译前必须：`export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH`
- 增量编译用 `ninja`；APK 打包脚本：`/root/pack_v6x.sh`（改 versionCode/versionName 即可复用 ✓）
- 报告库 `/root/research/`（01–43 号）；产物 `/root/final/mgl-panvk-vNN.apk`
- `ssh_exec` 工具通道时好时坏 ⇒ **统一用本地 bash 里的 ssh** ✓

### 3.4 手机侧日志（**App 通道可读** ✓ Shizuku 掉了也能读）
```
/sdcard/MG/cap.txt      ← logcat -b all 落盘（含 tag MESA 的驱动诊断）★最重要
/sdcard/MG/mgl.log      ← MobileGL 日志
/sdcard/MG/vkshim.log   ← 垫片日志（现状被 SurfaceCaps 刷屏 ✗ 用处有限）
/sdcard/MG/timeline.txt ← 自建采样器（每 20s 一行：renew/timeouts/frames…）
/sdcard/MG/status.txt   ← 状态牌内容（见 §8）
```
启动采集：
```
setsid nohup sh -c 'logcat -b all -v time > /sdcard/MG/cap.txt 2>&1' >/dev/null 2>&1 < /dev/null &
```
（**别写到 `/data/local/tmp/cap.txt`** ✗ —— Shizuku 掉了 App 通道读不到 ✓）

---

## 4. 现状：已验证有效 / 已证伪（关键！）

### ✅ 必须保留的有效改动
| 名称 | 内容 | 证据 |
|---|---|---|
| **C2** | `csf/panvk_vX_cmd_draw.c` 的 `get_tiler_desc()` 内加 `cs_wait_slots(b, dev->csf.sb.all_iters_mask);`（上游 MR **!44173**）| 卡点从 seqno 432 推到 576/636/748；存活从秒级到分钟级 |
| **P2** | 让 `kbase_renew_tiler_heap()` 真正触发（去掉 `submit->tiler_work_estimate &&` 前置门）| `tiler heap renewal` 周期出现（uAPI 1.21, submits 32）· OOM 归零 |
| **P2 的区间** | env `PANVK_KBASE_HEAP_RENEW_INTERVAL=32`（默认 128 太晚 ✗）| 关掉它（=100000）⇒ `tiler heap OOM` **立刻复现** ⇒ 反证有效 |
| WSI 回退 | `vk_android.c` 的 AHB 自描述回退 ⇒ `AHB layout fallback: … -> DRM_FORMAT_MOD_LINEAR` | 真交换链可用（2376x1080）|

### ❌ 已证伪 / 有害（**不要再试**）
| 名称 | 内容 | 结论 |
|---|---|---|
| **C1** | 在 kbase 上抑制 `cs_vt_end`/`cs_finish_fragment`/`cs_frag_end` + TILER_OOM 注册 | **有害** ✗（第 3~4 个作业就卡死；因为这三条 heap ops 正是迭代槽的 signal 来源）|
| 门铃快速路径删除 | 删 `kbase_subqueue_publish` 的 fast path → 无条件 kick（v57）| **更糟** ✗（~5 秒崩 + `exception 0xc3` MMU fault）|
| 2 行缓存失效 | 读 `*active` 前加 `kbase_cache_invalidate_range`（v58）| **无效** ✗ |
| **P5** | 把 AHB modifier 回退从 LINEAR 改成 **AFBC `0x0800000000000072`** | **有害** ✗（触发 `exception 0xc3`）|
| 删 `SB_MASK_STREAM` 重写 | v65 | **无效** ✗（不解释卡点）|
| `CS_EXTRACT_INIT` | 曾疑为主因 | **非本因** ✗（内核在页分配时写 0，此后无人碰；显式写 0 与现状逐位等价）|
| 诊断位图按位解读 | "progress 317 = 1+12×26+4 ⇒ 第 27 个 barrier" | **无效解码** ✗（progress 用 `cs_sync32_add` **累加** ⇒ 会进位到无关位）|

### ⚠️ 两条必须记住的"读法陷阱"
1. **user_io 页 2 不镜像内核 CS 寄存器块** ⇒ `CS_STATUS_*`/`CS_HEAP_*`/`CS_FAULT` 全 0 **不是**"无异常"的证据 ✗
   （uAPI 只承诺 `CS_EXTRACT`/`CS_ACTIVE`）
2. `CS_ACTIVE` 语义是"启动时的 extract"，而我们从不写 `EXTRACT_INIT` ⇒ 它恒 0，**不代表"未激活"** ✗

---

## 5. 当前卡点：被调用流（callee）永不返回 —— 实测证据链

版本 v70 的真机读数（**逐字**，来自 `/sdcard/MG/cap.txt`）：
```
kbase: ENTRY subqueue 1 extract pointer: ring offset 896 (gpu_va 0x5ff9a35380), extract 896,
   delta-from-extract 0 B, 16 raw bytes: 00 00 00 00 7e 7c 00 20 …
   first instr 0x20007c7e00000000 opcode 0x20 (CALL) wait_mask 0x0000 => NORMAL-CLASS
kbase: ENTRY subqueue 1 last-job head: ring offset 768 (gpu_va 0x5ff9a35300), extract 896, delta -128 B,
   first instr 0x2200000000000002 opcode 0x22 (REQ_RESOURCE)
kbase: ENTRY subqueue 1 geometry: last_job_offset 768 (entry 256/256 bytes), stream 0x5ff8af0000/2840,
   flush 906606, extract 896 (ring offset 896, +128 B into entry)
kbase: RESCHED subqueue 1 ring 0x5ff9a35000: pre-kick extract 896 seqno 3 | kick ioctl 47 us rc 0 |
   immediately after extract 896 seqno 3 | +200 ms extract 896 seqno 3
kbase: SLOWCALL kbase-sync wait … -> -4 after 10070 ms
kbase: SLOWCALL   wait[0] sync 0x… state 4 targets 339/339/339
kbase: timeout on subqueue 0: seqno 338, target 339；insert 87040, extract 86912
（早期版本另测：callee 流内 progress bit0 = CMDBUF_START 已 SET；CS_STATUS_WAIT 0x0；ring 层 barrier mask 0x0）
```
**推论链**：
1. extract 停在 entry 内 **+128 B**，下一条正是 **CALL**，且 CALL **不是等待类** (wait_mask=0)
2. **强制 resched（rc=0）后 extract/seqno 完全不动** ⇒ 不是"没被调度"，而是**推不动了**
3. 早期读数显示 **callee 的 CMDBUF_START 已 SET** ⇒ **被调用流确实被进入过** ✓
⇒ **结论（原表述，已被 Kimi 审计削弱 ✗）：固件进入了 callee，但再也没返回；且我们对 callee 内部没有可用仪器**（旧 progress 会累加进位 ✗）

> ⚠️ **审计批注（2026-10-05，Kimi 第 4 次命中，详见 43 号）**：
> 上面这条结论**包含了跨运行批次拼接**（"进入 callee"来自**早期版本另测**，与 v70 这次卡死**非同一次运行**），
> 严格来说**不成立**。更严谨的表述是：
> **v70 读数只证明 —— extract 冻结在 subqueue 1 的 entry 内、下一条是非等待类 CALL、且 resched 无效。**
> **"是否已进入 callee"在 v70 这次运行中未被观测**；"进入未返回"与"压根没进 callee"**在现有读数上不可区分**。
> ⇒ 解药正是 **v71**（同一次运行内的不累加检查点 ID）。**在 v71 结果出来前，§5 结论按"待定"处理，不要当作已证事实引用。**

**已排除的替代解释**：
- "MGL 在 >16384 draw 拆分路径自己死锁" ✗（栈在 `kbase-sync wait`，不在 MGL）
- "只是没被调度" ⚠️（被 RESCHED 无效削弱）
- 页 2 不可读 ⇒ 我们**看不到 CS_FAULT/CS_STATUS**（这是本项目的核心盲区 ✗）

---

## 6. 进行中的工作（截至本文件写入）

| 版本/任务 | 内容 | 状态 |
|---|---|---|
| **v71**（子智能体 `3545a524`）| 把**累加型 progress** 换成**不累加的唯一检查点 ID**（STORE 唯一常量到 seqno cell 的 **offset 56**，避开 v68 诊断用的 52），埋点覆盖：流起始 / 各 `SYNC_WAIT`、scoreboard 等待前后 / `cs_defer_indirect` 相关（`cs_vt_end`/`cs_finish_fragment`/`cs_frag_end`）前后 / `wait_finish_tiling` / fragment·compute 的 RUN 与收尾 / tiler heap 世代切换前后 ⇒ **一次运行就能读出"卡在哪个检查点"** | 在编（服务器）|
| **44 号研究报告**（子智能体 `859dd44c`）| 系统补课：CSF 调用/流返回机制、能让 callee 永不返回的机制清单、上游 Mesa MR/issue 检索（panvk csf hang / scoreboard / iter / wait_slots / cs_defer_indirect / CS_EXTRACT / CSG suspend）、先例对照 ⇒ 写 `研究论文/44-callee-stream-hang-research.md` | 在跑 |
| 一键脚本（用户提议）| `/sdcard/MG/fetch.sh`（App 侧 bash：ssh 取包+校验+解包）+ `/sdcard/MG/run.sh`（特权 shell：装包+放驱动+起采集+拉起 ZL2+注入点火+等+打印判据）| 未做 |

**版本哈希账本（驱动 `.so` sha256 前 16）**：
`v54 a9cba64a` · `v63 c03f0e7b`（= v54+C2）· `v64`（v63+renew32 的 APK 变体）· `v66 24bd7ccd`（+诊断）·
`v67 c134b54e`（+§5 标记诊断）· `v68 dcda738f`（+修复A+诊断D）· `v69 f2e98dcb`（+慢调用看门狗）· **`v70 6610c2a7`**（+ENTRY/RESCHED）
APK 在 `/root/final/` 与手机 `/storage/emulated/0/Mali驱动项目/驱动/`（本地也留了每版 `.so`）。

---

## 7. 工程方法论（本会话最贵的教训）

1. **确定性对照**：每次改动都要做"撤掉本改动重编 ⇒ **必须逐位等于上一版**"。这是单变量的唯一保证 ✓
2. **诊断优先于猜测**：本会话靠猜浪费了三轮（C1 ✗、缓存失效 ✗、SB_MASK_STREAM ✗）；而 v66/v69/v70 三个**诊断版**每次都直接给出事实 ✓
3. **判据提前锁死**：上机前先把"什么观测算赢/算输"写下来 ⇒ 结果出来不许滑动目标（例如"删得还不够"）✗
4. **症状消失 ≠ 修好**：要区分"错误没了"与"问题解决"（本项目出现过"超时消失但变成静默冻结"✗）
5. **空载荷 APK 陷阱**：必须 `unzip -p <apk> lib/arm64-v8a/libvulkan_freedreno.so | sha256sum` 校验非空且等于构建产物 ✗
6. **旧日志混淆陷阱**：`grep -c` 统计会包含**上一轮残留**的日志行 ⇒ 上机前先 `rm` 采集文件，或按**时间戳**过滤 ✗
7. **动作前先取树**：`android_ui_click` 的 `ref` 需要**最近一次 dump** ✓

---

## 8. 辅助设施（本会话额外做的）

### 8.1 状态悬浮窗（用户要求"能随时看到 AI 在干什么"）
- **安卓 App**：包名 `com.dsh.overlay.status`，label「DSH 状态」，v3（`5ce07b8b…`），
  已装 + `SYSTEM_ALERT_WINDOW: allow` ✓ + 前台服务在跑 ✓
  · 显示内容 = 读 **status.txt**（候选路径顺序：① `getExternalFilesDir(null)/status.txt`
    ② `/sdcard/Android/media/com.dsh.overlay.status/status.txt` ③ `/sdcard/MG/status.txt`）
  · 两行约定：`⏳ 当前：…` / `➡️ 下一步：…`；忙态行首有 13dp 转圈动效 ✓ 空闲态静态小点
  · 可拖动/位置记忆/单击折叠/长按隐藏 60s；**牌外触摸穿透**（无 `FLAG_NOT_TOUCHABLE` 的反面）
  · ⚠️ 私有目录（`/sdcard/Android/data/…/files/`）会被反复清掉 ✗ ⇒ **三条路径都写**才稳 ✓
- 写入方法（shell uid 可写）：
  `printf '%s\n' "⏳ 当前：…" "➡️ 下一步：…" > /sdcard/Android/media/com.dsh.overlay.status/status.txt`
- **约定：每推进一步就更新它** ✓（用户明确要求过"刷新太慢"✗）

### 8.2 审计桥（独立模型做"证据对账"）★ 已修复 2026-10-05
- **⚠️ 现行主体已从 Kimi 换成 GLM（2026-10-05 晚）**：
  · 原因：Moonshot org `89cb…6341` 被限 **RPM=3**，桥经常"卡死/空回复"（实测：连续请求必限流 ✗）
  · 现行：角色卡 **「审计员」**（id `1fd5e58a…`，由"Kimi审计"改名）→ 绑定 **「辅助模型」= 智谱 `glm-4-plus`**
    （config `4cba5618…` · endpoint `https://open.bigmodel.cn/api/paas/v4/chat/completions`）
  · 质量对比（实测同一条 v70 结论）：kimi-k3 ✪✪✪ ＞ glm-4-plus ✪✪ ＞ glm-4-flash ✪（flash 只会套话 ✗）
  · **glm-4-plus 可用 ✓**：能命中"没进 callee vs 进了没返回"的核心歧义；但 ③ 实验建议常不可执行（它不知道"读不到 PC"约束）⇒ **需我方兜底过滤**。
- **调用方式（现行 · 已实测通 ✓）**：用 `extended_chat:chat_with_agent`，传角色卡名 `审计员`
  （⚠️ 旧名 `Kimi审计` 已改，若见旧名请改用 `审计员`）
  ```
  extended_chat:chat_with_agent(
      character_card_name = "审计员",
      message             = <要审计的原文>,
      timeout             = 180 )
  ```
- **桥的本质** = **一张绑定 `FIXED_CONFIG` 的角色卡**（`Kimi审计`，id `1fd5e58a-ba9b-4dda-9b87-94ba9d9d6287`），
  把该角色卡的对话模型**钉死**到 `kimi` 配置的 `kimi-k3` ✓
- `chat_with_agent` **一角色一会话**：首次自动建会话并返回 chat_id，后续同卡复用（会话 id `5800e73a-8b59-4f6c-92fc-610b10ad0885`）
- **实测回包自带**：`provider":"ANTHROPIC_GENERIC/kimi"` · `modelName":"kimi-k3"` ✓✓
- 底层模型配置：config **`kimi`**（id `f59dbc65-5a5e-4cc0-aa0f-e472b8763533`）
  · endpoint `https://api.moonshot.cn/v1/chat/completions` · model `kimi-k3`（模型列表 index **1**）
- 可用模型（实测 `/v1/models`）：`kimi-k3` · `kimi-k2.7-code` · `kimi-k2.7-code-highspeed` · `kimi-k2.6`
- ⚠️ **旧记录作废** ✗：
  · provider `moonshot-kimi` **不存在**（本机只有 `默认配置` / `deepseek` / `kimi` 三条）
  · `workflow` 列表**为空** ⇒ 旧写法 `agent(prompt,{provider:"moonshot-kimi"})` **必然 `ok:false`**
  · 根因**不是密钥被轮换**（已否证）：`kimi` 配置的 **chat ✓ + tool_call ✓** 连通测试均通过
  ⇒ 旧写法失败=**根本找不到那个门牌号**，不是连不上网 ✓
- **审计协议**：只回三句 —— ① 哪些结论证据不足？② 最强替代解释？③ 什么实验能否证？（已写进角色卡 system prompt）
- **必给原文**，不要给我的转述 ✓
- 它已经命中 3 次（推翻位图解码 ✗、否证"MGL 拆分死锁" ✗、指出"进了流"是过度断言 ✗）

### 8.2.1 ★ 功能性模型改绑（修复"总结/记忆后卡住"）2026-10-05
- **症状**：对话"总结记忆后卡住"（用户报告）。
- **根因**：`MEMORY`/`SUMMARY`/`TITLE_GENERATION` 等**自动功能**原绑 **`default`（Moonshot，org RPM=3）**
  ⇒ 每 16 条消息触发记忆总结 → 调 Moonshot → **429 限流 → 卡住** ✗
  （注意：**不是"总结"本身的 bug，是它调的模型被限流**）
- **修复**：把下列功能从 Moonshot 改绑到 **「辅助模型」= glm-4-plus**（智谱，不限流）：
  `MEMORY` · `SUMMARY` · `TITLE_GENERATION` · `GREP` · `TRANSLATION` · `UI_CONTROLLER` · `ROLE_RESPONSE_PLANNER`
- **保留**：`IMAGE_RECOGNITION`/`AUDIO_RECOGNITION`/`VIDEO_RECOGNITION` 仍绑 `default`（GLM 文本模型无视觉）
- **CHAT 不动**：仍 `deepseek`。
- **教训**：**任何"自动/后台功能"都不该绑在会被限流的 key 上** —— 它们会静默抢配额、把主流程拖死。
  ⇒ 选型原则：**CHAT 可高质高限；后台功能必须走"量大管饱"的模型** ✓

### 8.3 手机控制通道（本会话验证过）
| 能力 | 状态 | 备注 |
|---|---|---|
| `android_shell_exec`（特权 shell / Shizuku）| ✅ | 装包/放文件/起 logcat/`dumpsys` 可用；**重启设备后需重新启动 Shizuku** |
| `android_app_launch` / `monkey` | ✅ | 拉起 ZL2 ✓ |
| `android_ui_dump` / `android_ui_click`（无障碍）| ⚠️ | 读树可用；**ACTION_CLICK 常被目标拒绝** ✗（任务卡、启动按钮都拒过）|
| `android_act_input`（原始 ADB 注入）| ✅ | **点火用它** ✓ |
| `android_web_dump` | ✅ | 读 DSH 自有 Web UI 的 DOM |
- ★★ **坐标系陷阱（"你没点到"的真因）**：无障碍工具的归一化坐标按**竖屏 1080×2376**换算 ✗，
  而 ZL2/游戏是**横屏 2376×1080** ✓ ⇒ 归一化点击必偏 ✗
  ⇒ **用 `android_act_input` 传绝对像素**（横屏空间）：启动游戏按钮中心 ≈ **(2002, 948)** ✓✓
- 屏幕范围权限（virtual-only / real）**会被用户改动** ⇒ 被拒时先看错误原文 ✗
- 点火前提：**前台必须是 ZL2**（否则点会落到别的 App 上 ✗）

---

## 9. 待办（按优先级）

1. **v71 上机**：读检查点 ⇒ 定位 callee 内部卡在哪一步（这是当前唯一靶心）
2. **44 号报告** 出来后按其"候选修复/实验"排序推进；把原文交 Kimi 审计
3. **git**：仓库在 `/storage/emulated/0/Mali驱动项目/github-repo/mali-panvk-toolkit`；
   权威提交已推到 GitHub（`b9c7a33`，父 `305b707`）✓；手机侧 `local main` 仍停在旧旁支
   `b5a0b1a` ✗（内容已同步）⇒ 待用户执行 `git reset --hard origin/main` 收敛指针；
   **待补提交**：v65–v70 的实测结果 + 33–44 号报告 + 本交接文件
4. **一键脚本**（§6 表末）+ Kimi 密钥修复复测
5. 画面几何错乱（大片三角板条）仍未单独解决 —— 需在卡死修好后回头处理
   （线索：探针像素是**精确正确**的 ✓ ⇒ 不是驱动整体画错，而是重负载下的 tiler heap 数据问题 ✓）

---

## 10. 参考资料索引

- **本地**：`/storage/emulated/0/Mali驱动项目/研究论文/`（约 40 篇，含 `01-10子智能体报告/`）
- **服务器**：`/root/research/01…43-*.md`（13/14/15/18/22/23/31/32/33/41/42 与本问题最相关）
  · 先例差分 `15-panvk-mtk-diff.md` · 先例补丁 `/root/panvk-mtk/` · 报告 20 号含"workerkast02 驱动不是纯 ICD"的结论
  · 探针 `/root/research/probe10/`（`panvk_wsi_probe`，`--mode=render/ahb/win/va`）
- **git 仓库**：`mali-panvk-toolkit`（`docs/09` 是工程实录，续写用 § 编号；`MANIFEST.md` 记哈希账本，**不放二进制** ✗）
- **关键 env 开关**：`PANVK_KBASE_HEAP_RENEW_INTERVAL`（P2 区间，实测生效 ✓）·
  `PANVK_DEBUG=1,kbase_diag`（诊断，含 v66/v67 的 DIAG 输出 ✓）·
  `PANVK_GRALLOC_AFBC_FALLBACK`（P5 开关；**保持关闭/LINEAR** ✓）

---

## 附：本文件的可信度声明
- 文中所有"实测"均有 `/sdcard/MG/cap.txt`、`mgl.log`、或服务器产物哈希为据 ✓
- 标 ⚠️/✗ 的条目是**已被证据否证或存在已知缺陷**，请勿重复尝试 ✓
- 凡涉及"下一步该做什么"的判断，均标注了依据；执行前建议再做一次**判据锁定** ✓

---

## 变更记录

### 2026-10-05 深夜 · 目录整理 + 44 号外部调研（GLM 会话，由用户委托）
- **目录已重组**：根目录 13 篇编号报告中 11 篇（与 `研究论文/服务器报告-01至43/` md5 相同）移入
  `_trash/根目录重复报告-与服务器报告01至43相同/`；42 号（与服务器版 md5 **不同**）与 43 号（服务器尚无）
  移入 `研究论文/本地散件报告/`；历史脚本 11 个移入 `脚本/`；overlay APK ×3 移入 `驱动/`；
  0 字节垃圾（`=`、`csi_handlers`、`pending=1`）移入 `_trash/`。**只移动、零删除** ✓
- **权威路径地图**：根目录 `README-目录指南.md`（下个会话找不到文件先看它，别猜路径）
- **新增 44 号报告**：`44-外部调研-CSF生态与挂死线索.md` —— 纯网络文献调研（Collabora Tyr/CSF 系列、
  panthor/kbase 生态、FCL-Mesa-Plugin 先例等），核心指向"**固件事件闭环未完成 ⇒ fence 无进展**"假说与五条可证伪检验；未动机、未碰服务器 ✓
- 本记录由外部会话追加；与上文冲突处，仍以有日志/哈希证据的一方为准（沿用原规则）

