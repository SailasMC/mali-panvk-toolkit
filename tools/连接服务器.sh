#!/usr/bin/env bash
# ============================================================================
#  Mali PanVK 项目 · 构建服务器连接器
#  用途：让任何新容器/新会话一条命令连上项目服务器（构建 Mesa/panvk + 取报告）
#  位置：/storage/emulated/0/Mali驱动项目/连接服务器.sh
#  用法：
#     ./连接服务器.sh                  # 默认 = doctor + ping（体检并确认能连）
#     ./连接服务器.sh ping             # 只做连通性与资产盘点
#     ./连接服务器.sh cmd "命令"       # 在服务器上跑一条命令
#     ./连接服务器.sh sh               # 开一个交互式 shell
#     ./连接服务器.sh pull 远端 本地   # 从服务器拉文件/目录
#     ./连接服务器.sh push 本地 远端   # 推到服务器
#     ./连接服务器.sh key              # 打印"密钥在哪 / 如何迁移"的说明（不泄露内容）
#     ./连接服务器.sh whereis          # 打印项目关键路径清单
#  环境变量：
#     MALI_SSH_KEY   自定义私钥路径（默认 $HOME/.ssh/id_ed25519）
# ============================================================================
set -uo pipefail

HOST="64.81.112.146"
USER="root"
KEY="${MALI_SSH_KEY:-$HOME/.ssh/id_ed25519}"
OPTS=(-i "$KEY" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
      -o ConnectTimeout=25 -o ServerAliveInterval=15 -o LogLevel=ERROR)

c_ok()   { printf '  \033[32m✓\033[0m %s\n' "$*"; }
c_bad()  { printf '  \033[31m✗\033[0m %s\n' "$*"; }
c_warn() { printf '  \033[33m!\033[0m %s\n' "$*"; }
hr()     { printf '%s\n' "------------------------------------------------------------"; }

run() { ssh "${OPTS[@]}" "$USER@$HOST" "$@"; }

doctor() {
  hr; echo "① SSH 密钥体检"
  if [ -f "$KEY" ]; then
    c_ok "私钥存在: $KEY"
    local pub
    pub=$(ssh-keygen -y -f "$KEY" 2>/dev/null | cut -c1-40)
    if [ -n "$pub" ]; then c_ok "公钥指纹前缀: ${pub}…"; else c_warn "无法解析私钥（格式/口令？）"; fi
    local mode; mode=$(stat -c '%a' "$KEY" 2>/dev/null || echo '?')
    [ "$mode" = "600" ] || c_warn "权限是 $mode（建议 chmod 600）"
  else
    c_bad "私钥不存在: $KEY"
    echo "     ⇒ 迁移办法三选一："
    echo "        1) 在旧容器里执行：  ./连接服务器.sh key   （看说明）"
    echo "        2) 把旧设备的私钥文件复制到新容器的同一路径（注意保密 ✗）"
    echo "        3) 直接设置环境变量：export MALI_SSH_KEY=/path/to/your_key"
    echo "        4) 或者在服务器上另加一把新公钥（推荐 ✓ 不用搬私钥）"
    return 1
  fi
  hr; echo "② 服务器连通性"
  if run "echo OK" 2>/dev/null | grep -q OK; then
    c_ok "SSH 连通 ✓  $USER@$HOST"
  else
    c_bad "连不上 ⇒ 检查：网络 / 密钥是否被授权 / 服务器是否在线"
    return 1
  fi
  hr; echo "③ 项目资产盘点"
  run 'set -e
    echo "  服务器时间 : $(date "+%F %T")"
    echo "  负载       : $(cut -d" " -f1-3 /proc/loadavg)"
    echo "  构建树     : $([ -d /root/zenithblue/work/mesa ] && echo 存在✓ || echo 缺失✗)  /root/zenithblue/work/mesa"
    echo "  构建目录   : $([ -d /root/zenithblue/build/android-v4 ] && echo 存在✓ || echo 缺失✗)"
    echo "  已出 APK   : $(ls /root/final/mgl-panvk-*.apk 2>/dev/null | wc -l) 个"
    echo "  最新 APK   : $(ls -t /root/final/mgl-panvk-*.apk 2>/dev/null | head -1)"
    echo "  报告数     : $(ls /root/research/*.md 2>/dev/null | wc -l) 篇（最新: $(ls -t /root/research/*.md 2>/dev/null | head -1)）"
    echo "  ninja 在跑 : $(pgrep -c ninja 2>/dev/null || echo 0)"
  ' 2>/dev/null
  hr; c_ok "体检结束 —— 结论：可以直接连 ✓"
}

whereis_() {
  cat <<'EOF'
============================================================
 Mali PanVK 项目 · 关键路径清单
============================================================
【本地（手机/工作区）】
  交接文档        /storage/emulated/0/Mali驱动项目/交接说明-HANDOFF.md   ★先读这个
  连接脚本        /storage/emulated/0/Mali驱动项目/连接服务器.sh
  本地报告        /storage/emulated/0/Mali驱动项目/研究论文/
  服务器报告副本  /storage/emulated/0/Mali驱动项目/研究论文/服务器报告-01至43/
  驱动产物        /storage/emulated/0/Mali驱动项目/驱动/  (各版 APK 与 .so)
  git 仓库        /storage/emulated/0/Mali驱动项目/github-repo/mali-panvk-toolkit/
  状态牌内容      /sdcard/Android/media/com.dsh.overlay.status/status.txt

【服务器 root@64.81.112.146】
  构建树          /root/zenithblue/work/mesa         （不是 /root/mesa ✗）
  构建目录        /root/zenithblue/build/android-v4
  编译前必须      export PATH=/opt/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH
  打包脚本        /root/pack_v6x.sh                   （sed 改版本号即可复用）
  APK 产物        /root/final/mgl-panvk-vNN.apk
  报告库          /root/research/01…43-*.md
  先例补丁        /root/panvk-mtk/  ·  /root/research/15-panvk-mtk-diff.md
  探针            /root/research/probe10/panvk_wsi_probe
  签名 keystore   /root/dsh-driver.keystore（pass: android, alias: dshdriver）
  只读禁改        /root/mesa · /root/MobileGL

【真机（最容易踩的坑 ✗）】
  ★ 实际生效的驱动 = /data/local/tmp/libvulkan_freedreno.so
    ⇒ 每次上机必须：unzip -p <apk> lib/arm64-v8a/libvulkan_freedreno.so > 该路径，并校验 sha256
  采集落盘        /sdcard/MG/cap.txt（tag MESA）· mgl.log · timeline.txt
  点火            android_act_input tap x=2002 y=948 screenId=real（横屏绝对像素）
============================================================
EOF
}

key_info() {
  cat <<EOF
============================================================
 SSH 密钥：位置与迁移说明（不打印密钥内容 ✗）
============================================================
当前使用的私钥： $KEY
存在吗：         $([ -f "$KEY" ] && echo 是 ✓ || echo 否 ✗)

【新容器要连服务器，四种做法（按推荐排序）】
 ① 在服务器上加一把【新公钥】（最安全 ✓ 不搬运私钥）
      在能连上的机器上执行：
        ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_new -N ""
        ssh -i $KEY $USER@$HOST "cat >> ~/.ssh/authorized_keys" < ~/.ssh/id_ed25519_new.pub
      然后在新容器里： export MALI_SSH_KEY=~/.ssh/id_ed25519_new
 ② 复制旧容器的私钥文件到新容器同一路径（方便，但私钥要搬运 ✗）
      ⚠️ 只通过可信通道传，别放进 git ✗，别贴进聊天 ✗
      ⚠️ 粘贴密钥进对话 = 已泄露 ⇒ 建议轮换 ✓
 ③ 设置环境变量指向你已有的任意可用私钥：
      export MALI_SSH_KEY=/path/to/key
 ④ 如果新容器与旧容器共用同一个 home（同一台设备上的同一个 App 环境）
      ⇒ 密钥已经在那里了 ✓ 直接跑： ./连接服务器.sh
============================================================
EOF
}

case "${1:-doctor}" in
  doctor)     doctor ;;
  ping)       run 'echo "  时间 $(date "+%F %T")"; echo "  APK $(ls /root/final/mgl-panvk-*.apk 2>/dev/null | wc -l) 个，最新 $(ls -t /root/final/mgl-panvk-*.apk 2>/dev/null | head -1)"; echo "  报告 $(ls /root/research/*.md 2>/dev/null | wc -l) 篇"' ;;
  cmd)        shift; run "$@" ;;
  sh)         ssh "${OPTS[@]}" "$USER@$HOST" ;;
  pull)       shift; scp -r "${OPTS[@]}" "$USER@$HOST:$1" "$2" ;;
  push)       shift; scp -r "${OPTS[@]}" "$1" "$USER@$HOST:$2" ;;
  key)        key_info ;;
  whereis)    whereis_ ;;
  *)          echo "用法: $0 [doctor|ping|cmd \"命令\"|sh|pull 远端 本地|push 本地 远端|key|whereis]"; exit 2 ;;
esac
