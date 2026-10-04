# 文档索引

| 文档 | 内容 |
|---|---|
| [01-why-mali.md](01-why-mali.md) | 背景：为什么 Mali 玩家需要这个项目；启动器驱动插件的来龙去脉 |
| [02-kbase-bringup.md](02-kbase-bringup.md) | 免 root 驱动 Mali kbase 的原理、握手坑、实测数据与自检方法 |
| [03-fcl-adreno-lock.md](03-fcl-adreno-lock.md) | ⭐ FCL / ZL2 的 Adreno 厂商锁：源码定位、日志证据、三种拆锁方案 |
| [04-build-g720-panvk.md](04-build-g720-panvk.md) | 构建 G720/v12 PanVK 的完整流程 + 依赖踩坑清单 |
| [05-findings.md](05-findings.md) | 两个上游成品驱动的实测对比、崩溃栈分析与结论 |
| [06-roadmap.md](06-roadmap.md) | 路线图：渲染器层次（Zink / MobileGL / 原生 Vulkan）、「自己优化驱动」的现实边界与测试顺序 |
| [07-our-build-and-results.md](07-our-build-and-results.md) | ★ 我们自己的构建结果与实测卡点（含 FCL 空指针、ZL2 加载顺序的日志证据）|
| [08-mobilegl-vulkan.md](08-mobilegl-vulkan.md) | ★ MobileGL（MGL）调研：两个后端、渲染器插件契约、如何接上我们的 PanVK |
| [09-mobilegl-integration.md](09-mobilegl-integration.md) | ★ MobileGL(DirectVulkan) 接入 PanVK 实录：编译四坑、渲染器契约差异、ICD 方案、静默安装绕过 |
