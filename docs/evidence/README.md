# 证据：自编 PanVK 真机渲染 Minecraft

判据行：`Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)`
环境：MC 26.3 Fabric · Sodium 0.9.3+mc26.3 · Java 25.0.5 · MT6989 · 2376x1080
画面：平坦世界，几何/天空/方块/HUD 全部正常
同帧另两处读数：`Allocated: 100% 5052MiB`（堆满）· `0 fps T: inf`（帧时间无穷 ⇒ 即卡死瞬间）
意义：渲染正确性无问题；问题是重负载下 停滞→重试→绘制爆炸→崩溃
