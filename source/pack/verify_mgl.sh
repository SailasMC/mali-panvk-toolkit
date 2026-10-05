#!/usr/bin/env bash
# 验证 MobileGL + PanVK 是否生效：读手机侧日志（需由手机侧执行；这里给出判据模板）
LOG="$1"
echo "=== 1) 渲染器/后端 ==="
grep -aE "MobileGL|Direct \(Vulkan\)|Backend library|OpenGL Renderer|OpenGL Version" "$LOG" | tail -6
echo "=== 2) 是否用上我们的 PanVK（设备名不带 -Immortalis）==="
grep -aoE "Mali-G720(-Immortalis)? MC12" "$LOG" | sort | uniq -c
echo "=== 3) PanVK 自己的输出 ==="
grep -acE "panvk is not a conformant|panvk" "$LOG"
echo "=== 4) 崩溃 ==="
grep -acE "SIGSEGV|Problematic frame|Game crashed|hs_err" "$LOG"
