# 为什么 Mali 用户需要这个项目

> 面向：在 Android 上玩 Java 版 Minecraft（FCL / Zalith Launcher / PojavLauncher），
> 但手机是 **Mali GPU**（联发科天玑、三星 Exynos、紫光展锐、部分麒麟）的玩家与开发者。

---

## 1. 安卓启动器的「驱动插件」是怎么回事

Java 版 Minecraft 在 Android 上不是原生跑的，而是：

```
启动器（Java 层）
  → LWJGL 桌面 OpenGL/Vulkan 绑定
    → 系统图形栈（libEGL / libvulkan）
      → GPU 厂商驱动（闭源 blob）
```

于是在**高通 Adreno** 设备上，社区做出了极有价值的替代品：

- **Turnip**（Mesa 的 freedreno Vulkan 驱动）—— 用开源驱动替换高通闭源 Vulkan
- **Zink**（Mesa 在 Vulkan 之上实现 OpenGL）—— 让游戏跑在 Vulkan 上

组合起来 `Zink + Turnip` 在 Adreno 上能显著修复厂商驱动的 bug、补上缺失的
OpenGL 扩展（Minecraft 的高版本 + Sodium/Voxy 很依赖这些）。

**FCL 和 Zalith Launcher 都内置了这套**：在设置里选一个驱动插件（一个 APK），
启动器就把该 APK 里的驱动注入进游戏进程。

## 2. 但 Mali 被官方「不支持」

启动器注入自定义驱动的代码长这样（FCL 与 ZL2 同源）：

```c
bool checkAdrenoGraphics() {
    bool is_adreno = (vendor && renderer
                      && strcmp(vendor, "Qualcomm") == 0
                      && strstr(renderer, "Adreno") != NULL);
    return is_adreno;
}
```

Mali 设备的 `GL_VENDOR` 是 `ARM`，`GL_RENDERER` 是 `Mali-*` —— **永远进不去这个分支**。
即：**Mali 用户连试的机会都没有**（详见 [`03-fcl-adreno-lock.md`](03-fcl-adreno-lock.md)）。

## 3. 而 Mesa 其实早就有 Mali 的开源 Vulkan 驱动

| 驱动 | 面向 | 内核接口 | Android 免 root |
|---|---|---|---|
| **Panfrost** | Mali Midgard / Bifrost | DRM（`/dev/dri`）| ❌ 需要 root |
| **Panthor** | Mali CSF（G710+） | DRM | ❌ 需要 root |
| **PanVK + kbase** | **Mali CSF（G610/G615/G720…）** | **`/dev/mali0`** | ✅ **本项目走的路** |

关键点在于 **kbase**：Arm 下游内核驱动暴露的 `/dev/mali0`，在很多机型上
普通应用就能打开（`0666`）。也就是说 —— **不用 root，也能把 GPU 用起来**。

## 4. 为什么以前没人做

- 启动器把 Mali 挡在门外（第 2 节），所以「Mali 也能用」这件事**没人验证过**；
- kbase 的 uAPI 版本多、握手 ioctl 方向位反直觉、文档稀少；
- PanVK 的 kbase 后端在 Mesa 主线里是较新的东西，且需要针对具体 GPU
  （panArch v11 / v12 / v13…）做适配；
- 各种「能不能用」的判断混杂着**启动器缓存指针**造成的假阳性日志，
  很容易误判（见 [`03`](03-fcl-adreno-lock.md) 的假阳性小节）。

## 5. 本项目做了什么

1. **证明免 root 可以**：`tools/kbase_probe.py` + `tools/kbase_selftest.c`
   在真实设备上跑通 kbase 全链路（握手 → 属性 → 分配 → mmap 读写）。
2. **把「驱动能不能用」变成可测量**：`vkicd_probe` / `zink_check` / `wsiprobe`
   三个小工具，几秒钟给出形态、扩展、WSI 结论。
3. **定位并拆掉那把锁**：给出可发布、可逆、不改启动器的方案。
4. **给出构建配方**：`g720-v12-csf` profile 的完整构建流程与全部依赖坑。
5. **如实记录失败**：两个上游成品在 G720 上的具体失败模式与崩溃栈 ——
   这对后续做适配的人是**最有价值的一手资料**。

## 6. 适用的设备

只要满足：

- Mali **CSF** 架构（G610 / G615 / G710 / G715 / G720 …，即 5th Gen 及以后）
- `/dev/mali0` 对普通应用可读（多数联发科/三星/展锐机型）
- kbase uAPI ≥ 1.21

就可以按本仓库的流程试。Mali Midgard/Bifrost（JM 前端）需要另外的 profile
（仓库里有 `g52-v7-jm` / `g57-v9-jm` 等，同样未在真机验证）。

## 7. 免责声明

- 开源驱动在移动 GPU 上属于**实验性**使用，可能崩溃、可能导致画面异常；
- 请自行备份存档；
- 本项目与 Arm / 高通 / 联发科 / OPPO / FCL 团队均无关联。
