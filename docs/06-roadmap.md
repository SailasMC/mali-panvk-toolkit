# 路线图与「自己优化驱动」的现实边界

> 这篇回答一个常见想法：**「实在不行就自己写一个驱动」**。
> 先把概念对齐，再说哪些是现实可行、且价值最大的。

---

## 1. 先分清三层，不然会白费力气

```
Minecraft Java  ──┐
                  │  ① 渲染器（renderer）：把桌面 OpenGL 调用翻译掉
   Zink           │     · Zink      = GL → Vulkan（Mesa 官方）
   MobileGL (MGL) │     · MobileGL  = "A desktop OpenGL implementation on Mobile"，GL → Vulkan，移动端优化
   GL4ES/VirGL    │     · GL4ES     = GL → OpenGL ES 2/3（用厂商 GLES，不碰 Vulkan）
   MobileGlues    │     · MobileGlues = GL → 宿主 GLES 3.2（同上，不碰 Vulkan）
                  │
                  ├── ② Vulkan 驱动：本项目的战场
                  │     · PanVK (Mesa) ── kbase 后端 ── /dev/mali0
                  │     · 或厂商 blob
                  │
                  └── ③ 内核接口：kbase（免 root 可达）/ panthor（需 root）
```

**关键认知**：`Zink` 与 `MobileGL` 是**同一层**的替代品（都是 GL→Vulkan）。
所以「把驱动和 MGL 结合起来」= **换掉第 ① 层**，第 ② 层仍然是 PanVK。
这不是两件难事相加，而是**换一个更好用的翻译层**。

> ⚠️ 注意区分两个名字很像的项目：
> - **MobileGL**（`MobileGL-Dev/MobileGL`）：桌面 GL 跑在 **Vulkan** 上 → 我们要的就是它
> - **MobileGlues**（`MobileGL-Dev/MobileGlues-release`）："on Mobile, GL uses ES"，
>   跑在**宿主 OpenGL ES** 上 → 它依赖厂商驱动，跟我们想要的无关

## 2. 「游戏原生 Vulkan 后端」是效率最高的路

Minecraft 26.x 自带 Vulkan 后端，配置项在 `.minecraft/options.txt`：

```
preferredGraphicsBackend:"vulkan"
```

这条路**完全绕开 OpenGL / Zink / MobileGL**：游戏直接调 Vulkan → 我们的 PanVK。
理论上效率最高、层级最少。代价是：
- 部分模组（Sodium 等）对非 GL 后端支持不完整；
- 启动器仍会在启动时初始化 EGL（所以驱动必须至少能撑过 `eglInitialize`）。

## 3. 「从零写驱动」不现实，但「改+调」非常现实

PanVK 是 Mesa 团队多年成果，从零实现一个能跑 Minecraft 的 Vulkan 驱动是**数年**工作量。
但有价值且可行的是下面这些 —— 它们正是本项目在做的事：

| 目标 | 现实做法 | 状态 |
|---|---|---|
| 拿到 26.3 的可运行驱动 | 用 zenithblue 仓库（它 pin 的正是 **Mesa 26.3.0-devel**）按正确 profile 编 | ✅ 进行中 |
| 适配**你的** GPU | 选对 profile（`g720-v12-csf` = panArch 12 + CSF + kbase） | ✅ |
| 合并 G720/v12 的关键修复 | 把 wonderkast02 的 G720 增量（`cs_reg_count` 等）并进基线 | ⏳ 待测 |
| 针对设备调优 | feature 开关、CS 配置、L2/一致性策略、着色器缓存 | ⏳ |
| 补 WSI / quirks | Android surface / AHB / dmabuf 路径的针对性修补 | ⏳ |
| 换更好的翻译层 | 用 MobileGL 替代 Zink，或直接用 MC 原生 Vulkan 后端 | ⏳ |
| 上游化 | 把 G720 profile 的验证结果与修补回馈给上游仓库 | ⏳ |

**换句话说**：不是「重写驱动」，而是「**把上游的 26.3 驱动在你的 G720 上真正调通并优化**」。
这件事没人做过（上游的 g720 profile 标注的是 `planned`，还没有真机验证结果）。

## 4. 拿到驱动后的测试顺序（效率从高到低）

```
1. MC 原生 Vulkan 后端 + PanVK        ← 层级最少，效率最高
2. MobileGL (MGL) + PanVK             ← GL 路径，比 Zink 更贴移动端
3. Zink + PanVK                       ← 保底（也是启动器内置选项）
```

三种都建立在**同一个 PanVK** 之上 —— 所以第一优先级永远是
「让 PanVK 在 G720 上稳定跑起来」，其余都是换上层。

## 5. 参考与其它可用的输入

- [MobileGL-Dev/MobileGL](https://github.com/MobileGL-Dev/MobileGL) —— 桌面 GL on Vulkan
- [MobileGL-Dev/MobileGlues-release](https://github.com/MobileGL-Dev/MobileGlues-release) —— GL on 宿主 GLES
- [zenithblue-oss/panvk-kbase-android](https://github.com/zenithblue-oss/panvk-kbase-android) —— 本项目的构建基线
- [wonderkast02/panvk-g720-kbase-csf](https://github.com/wonderkast02/panvk-g720-kbase-csf) —— G720/v12 参考
- [lfdevs/mesa-for-android-container](https://github.com/lfdevs/mesa-for-android-container) —— 另一个 26.3 的 Android 构建来源（可作对照）
