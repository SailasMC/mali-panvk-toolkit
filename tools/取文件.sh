#!/usr/bin/env bash
# ============================================================================
#  标准取件流程（手机侧）—— 服务器切分 + 多路并行 + 拼接校验
#  位置: /storage/emulated/0/Mali驱动项目/取文件.sh
#  用法: bash 取文件.sh <服务器分片前缀> <本地输出文件名> [期望sha256(可省)] [份数=8]
#  例:   bash 取文件.sh v71p8_ mgl-panvk-v71.apk 918928cc989bebde79abac58 8
#  说明:
#    · 对应服务器侧步骤（由 AI 用 ssh 执行）：
#        cd /data/dsh_downloads && split -n 8 -d <原文件> <前缀>     # 生成 前缀00 前缀01 …
#        （HTTP 服务: python3 -m http.server 18080 --directory /data/dsh_downloads）
#    · 本脚本只做: 并行下载分片 → 校验大小 → 拼接 → 校验 sha256 → 解出 APK 内驱动
#    · 不删除任何文件（避免触发安全拦截）✓
# ============================================================================
set -uo pipefail
PREFIX="${1:?用法: 取文件.sh <分片前缀> <本地输出> [期望sha] [份数]}"
OUT="${2:?缺少本地输出文件名}"
EXPECT="${3:-}"
N="${4:-8}"
BASE="http://64.81.112.146:18080"
WORK="$(dirname "$(readlink -f "$0")")/.dlparts"
mkdir -p "$WORK"

echo "① 并行下载 $N 个分片（前缀 $PREFIX）"
S=$(date +%s)
for i in $(seq 0 $((N-1))); do
  IDX=$(printf "%02d" "$i")
  curl -s -m 600 -o "$WORK/${PREFIX}${IDX}" "$BASE/${PREFIX}${IDX}" &
done
wait
E=$(date +%s); echo "   完成，用时 $((E-S)) 秒"

echo "② 校验分片"
BAD=0
for i in $(seq 0 $((N-1))); do
  IDX=$(printf "%02d" "$i"); F="$WORK/${PREFIX}${IDX}"
  sz=$(stat -c%s "$F" 2>/dev/null || echo 0)
  if [ "$sz" -lt 1 ]; then echo "   ✗ 分片 $IDX 为空"; BAD=1; else echo "   ✓ $IDX = $sz B"; fi
done
[ "$BAD" -eq 0 ] || { echo "⇒ 有分片缺失 ⇒ 重跑本脚本或减少份数"; exit 1; }

echo "③ 拼接"
cat $(for i in $(seq 0 $((N-1))); do printf "%s/%s%02d " "$WORK" "$PREFIX" "$i"; done) > "$OUT"
echo "   输出: $OUT  $(stat -c%s "$OUT") 字节"

echo "④ 校验 sha256"
GOT=$(sha256sum "$OUT" | cut -d' ' -f1)
if [ -n "$EXPECT" ]; then
  case "$GOT" in "$EXPECT"*) echo "   ✓ 匹配 ($(echo "$GOT" | cut -c1-16)…)";;
                 *) echo "   ✗ 不匹配: 实际 ${GOT:0:16}… / 期望 ${EXPECT:0:16}…"; exit 2;; esac
else
  echo "   sha256 = $(echo "$GOT" | cut -c1-16)…  （未提供期望值，请自行比对）"
fi

echo "⑤ 若是插件 APK，则解出驱动"
if unzip -l "$OUT" 2>/dev/null | grep -q "lib/arm64-v8a/libvulkan_freedreno.so"; then
  DRV="$(dirname "$OUT")/$(basename "$OUT" .apk).drv.so"
  unzip -p "$OUT" lib/arm64-v8a/libvulkan_freedreno.so > "$DRV" 2>/dev/null \
    && echo "   ✓ 驱动已解出: $DRV  $(stat -c%s "$DRV") 字节  sha256 $(sha256sum "$DRV" | cut -c1-16)…"
else
  echo "   （非插件 APK，跳过）"
fi
echo "完成 ✓"
