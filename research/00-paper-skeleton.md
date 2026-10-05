# 在 Android 无 root 设备上让 Minecraft 启动器使用自编开源 Mali 驱动
## —— Mesa PanVK + MobileGL DirectVulkan 的端到端实现与失败点分析

> 状态：骨架（待 01–10 号研究报告完成后合并成文）

## 摘要
（待写）在 OPPO PHZ110（MT6989 / Immortalis-G720 MC12 / Android 16）上，不依赖 root，
通过渲染器插件契约把自编 Mesa PanVK 注入 MobileGL 的 Vulkan 后端，并逐层定位失败点。

## 1. 引言
- 动机：厂商 Vulkan 驱动（Driver 44.1.0 / Vulkan 1.3.247）缺少 MC 新版本所需能力；
  开源 Mesa PanVK 可提供 1.4.363 与 181 个扩展。
- 约束：无 root、`/storage` noexec、`/dev/dri` EACCES、不存在 gralloc HAL 的常规路径。

## 2. 系统与设备背景
（设备信息、命名空间、SELinux、kbase `/dev/mali0` 0666 等）

## 3. 方法
### 3.1 渲染器插件注入（`fclPlugin` / `pojavEnv` / `DLOPEN` 预加载 / `DT_SONAME`）
### 3.2 纯 ICD 与 loader 的差距：自建 125 入口转发层
### 3.3 loader 语义：三级分派（global → instance → physical-device/device）与 thunk 表
### 3.4 可观测性：日志双写 `/sdcard/MG/vkshim.log`

## 4. 失败点分析与修复（按错误点迁移顺序）
1. `wsi_GetSwapchainImagesKHR` 段错
2. MGL 内 NULL 函数调用
3. `vkCreateDevice = -3`（队列族 count=0；转发层把物理设备当 instance）
4. ✓ 修复后：队列族 `flags=0x7`、feature `xor` 全 0、`vkCreateDevice` 成功
5. ✗ 当前：`VK_ERROR_INVALID_EXTERNAL_HANDLE`（Mesa `u_gralloc` 转换失败）

## 5. 证据
（引用 `docs/09` 各节的原文日志与源码行号）

## 6. 讨论
（01–10 号研究的结论汇总：哪条路线最优、风险与工作量）

## 7. 结论与后续工作
（待写）

## 附录
- 版本与哈希（APK/so 的 sha256 前缀）
- 复现步骤
- 参考文献（AOSP/Mesa 源码与问题单）
