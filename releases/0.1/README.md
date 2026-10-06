# Mali PanVK Toolkit 0.1 —— 半成品快照

> 在**免 root** 的 Android 手机上，让 Mali GPU 用上开源驱动（Mesa PanVK / kbase 后端），
> 并把它接进 Minecraft 启动器（Zalith Launcher 2 / FCL）。
>
> **这是一个诚实的半成品：游戏目前还没跑通。**

## 一句话状态

驱动能加载、能建 Vulkan 设备、能过采样/拷贝/呈现全链路；
**但一次真实的三角形绘制就会让 GPU 翻译失败**（CSF MMU exception `0xc2`/`0xc3` ⇒ `VK_ERROR_DEVICE_LOST`）。

## 0.1 的边界（已知卡点）

- **渲染**：分辨率、几何、采样经专用探针实测 **2566080/2566080 = 100.000%** 像素精确匹配
  ⇒ 楔形/拉伸**不在** WSI、pitch、AFBC、覆盖、IR 这些方向（均已逐条实测排除）。
- **根因已收窄成一句可执行的话**：
  *软件侧"绑定成功（ret=0）"的某个 **72 KiB（0x12000）** GPU VA 区间，
  内核没有为它的**最后一页**建立页表项 ⇒ GPU 一访问就翻译失败。*
  （三轮单变量实测：故障**永远**跟着那个区间的末页走，与 tiler 描述符/几何缓冲无关。）
- **卡死族**（CS 停在 fence 前 ⇒ 看门狗 `-4`）：机制已用**逐秒采样**定位到
  "三个跨子队列 syncobj 冻结"，修复未完成。
- **顺带发现一个独立缺陷**：几个 VA 绑定区间**互相重叠** —— 已列为"画面面板/重复内容"的头号候选。

## 0.1 里有什么

| 目录 | 内容 |
|---|---|
| `source/panvk-diffs/` | 驱动侧改动（panvk / kbase / gpu_queue / physical_device 的 patch 与 diff）|
| `source/shim/`、`source/pack/` | Vulkan 转发垫片源码 + 驱动插件 APK 打包器 |
| `apk/MANIFEST-0.1.md` | **113 个**驱动插件 APK 的台账（名称 / 体积 / sha256 前 16 位）|
| `tools/` | 上机准备、取件、连服务器脚本 + **接力卡** + 交接说明（完整方法论）|
| `docs/`、`research/` | 9 号工程实录（28 节）+ 专题研究 + 综述论文 |

## 环境（全部真机实测）

`OPPO PHZ110` · 天玑 9300（MT6989）· **Immortalis-G720 MC12** · Android 16 (SDK 36) · **无 root**

## 怎么用

1. `tools/上机准备.sh` —— 探测设备与授权通道
2. `tools/取文件.sh` —— 从构建机取驱动产物
3. 按 `apk/MANIFEST-0.1.md` 选对应版本的驱动插件 APK 安装
4. 游戏侧：Zalith Launcher 2 / FCL + MobileGL（DirectVulkan）

## 免责

半成品，仅供研究与复现，不保证任何设备可用。
本项目所有"未验证 / 不一致"的内容均如实标注，**没有把推测写成结论**。
