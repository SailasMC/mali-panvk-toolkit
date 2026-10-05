#!/usr/bin/env bash
# ============================================================================
#  问模型.sh —— 直接调用外部模型做审计/分析（绕过 DSH 的 provider 覆盖路由 ✗）
#  位置: /storage/emulated/0/Mali驱动项目/问模型.sh
#  用法: bash 问模型.sh <glm|kimi> "<问题文本>" [max_tokens]
#        bash 问模型.sh glm "$(cat 我的问题.txt)" 8000
#  说明:
#    · DSH 的 workflow provider 覆盖对 glm 走不通 ✗（密钥/端点/模型都对，直连 200 ✓）
#      故改为直连各家 /chat/completions ✓
#    · ★ 推理型模型务必给足 max_tokens（默认 8000）✗ 否则 reasoning 会吃光额度、正文被截断
#    · 密钥从 DSH 凭据库读取 ✓ 全程不打印密钥 ✓
# ============================================================================
set -uo pipefail
CRED=/data/data/com.dsharnessmobile.shell/files/home/.dsh/.credentials.yaml
WHO="${1:?用法: 问模型.sh <glm|kimi> \"问题\" [max_tokens]}"
ASK="${2:?缺少问题文本}"
MAXTOK="${3:-8000}"

read_key() { grep -m1 "^[[:space:]]*$1:" "$CRED" 2>/dev/null | sed 's/.*: *//' | tr -d '"'; }

case "$WHO" in
  glm)
    URL="https://open.bigmodel.cn/api/paas/v4/chat/completions"
    MODEL="glm-5.3-flash"; KEY="$(read_key GLM_API_KEY)";;
  kimi)
    URL="https://api.moonshot.cn/v1/chat/completions"
    MODEL="kimi-k3"; KEY="$(read_key KIMI_CODING_API_KEY)";;
  glm-5.3) URL="https://open.bigmodel.cn/api/paas/v4/chat/completions"; MODEL="glm-5.3"; KEY="$(read_key GLM_API_KEY)";;
  *) echo "未知模型: $WHO（可选 glm | glm-5.3 | kimi）" >&2; exit 2;;
esac
[ -n "$KEY" ] || { echo "✗ 凭据库里没有对应的 API key" >&2; exit 3; }

SYS='你是 Mali CSF / Mesa panvk 方向资深工程师。直接给最终答案，不要展示推理过程。'
if [ -z "${ASK##*【审计】*}" ]; then
  SYS='你是独立审计员。只回答三点，每点不超过 4 行：① 哪些结论证据不足 ② 最强替代解释 ③ 最便宜且能一次定论的否证实验。禁止复述、禁止恭维、禁止编造寄存器语义。'
fi

# 用 python3 或 sed 做 JSON 转义（优先 python3 ✓ 更稳）
if command -v python3 >/dev/null 2>&1; then
  BODY=$(ASK="$ASK" SYS="$SYS" MODEL="$MODEL" MAXTOK="$MAXTOK" python3 -c '
import json,os
print(json.dumps({"model":os.environ["MODEL"],"temperature":0.1,"max_tokens":int(os.environ["MAXTOK"]),
 "messages":[{"role":"system","content":os.environ["SYS"]},{"role":"user","content":os.environ["ASK"]}]}))')
else
  esc() { printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' | awk 'BEGIN{ORS="\\n"}{print}'; }
  BODY="{\"model\":\"$MODEL\",\"temperature\":0.1,\"max_tokens\":$MAXTOK,\"messages\":[{\"role\":\"system\",\"content\":\"$(esc "$SYS")\"},{\"role\":\"user\",\"content\":\"$(esc "$ASK")\"}]}"
fi

RESP=$(curl -s -m 300 -X POST "$URL" -H "Authorization: Bearer $KEY" -H "Content-Type: application/json" -d "$BODY")

if command -v python3 >/dev/null 2>&1; then
  RESP="$RESP" python3 -c '
import json,os,sys
try:
    d=json.loads(os.environ["RESP"])
except Exception as e:
    print("[原始响应]", os.environ["RESP"][:800]); sys.exit(0)
if "error" in d: print("[API 错误]", d["error"]); sys.exit(0)
c=d["choices"][0]; u=d.get("usage",{})
print(c["message"]["content"])
rt=u.get("completion_tokens_details",{}).get("reasoning_tokens",0)
print("\n--- usage: completion=%s reasoning=%s ---" % (u.get("completion_tokens"), rt))
if rt and rt >= u.get("completion_tokens",0)-50:
    print("⚠️ reasoning 几乎吃光 max_tokens ⇒ 正文可能被截断，请加大 max_tokens（用第 3 个参数，如 12000）")
'
else
  printf '%s' "$RESP" | sed -e 's/\\n/\n/g' -e 's/\\"/"/g' | tail -c 6000
fi
