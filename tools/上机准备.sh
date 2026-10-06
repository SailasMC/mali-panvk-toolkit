#!/system/bin/sh
# ============================================================================
#  Mali PanVK 上机准备（由 Shizuku 特权 shell 执行；本脚本【不拉起游戏】）
#  位置: /storage/emulated/0/Mali驱动项目/上机准备.sh
#  用法: android_shell_exec "sh /sdcard/Mali驱动项目/上机准备.sh <apk绝对路径> [期望驱动sha256前16]"
#    例: sh /sdcard/Mali驱动项目/上机准备.sh "/sdcard/Mali驱动项目/驱动/mgl-panvk-v80.apk" bb138465b9e1cf67
#
#  本脚本把接力卡 §4 的坑全部编进去：
#    ① 真正生效的驱动 = /data/local/tmp/libvulkan_freedreno.so ⇒ 从 APK 解出 + sha256 校验
#    ② hs_err 会被清空 ⇒ 【先抄基线清单】；并把旧 cap.txt 改名（防上一轮日志混淆）
#    ③ 不从 shell 拉起游戏（Shizuku 一挂会连杀游戏）⇒ 脚本到"准备完"就停，点火交 app 层
#    ⑤ shell 通道不 sleep（会超时）⇒ 本脚本无 sleep
#    ⑦ 降级安装必须 -r -d -t，且必须取【命令本身】的退出码（管道会吞掉）
#    ⑨ pkill 用 -x（-f 会匹配到自己命令行 ⇒ 自杀）
#  退出码: 0=准备就绪  2=驱动 sha 不匹配  3=安装失败
# ============================================================================
set -u

APK="${1:?用法: 上机准备.sh <apk绝对路径> [期望驱动sha256前16]}"
EXPECT="${2:-bb138465b9e1cf67}"
MG=/sdcard/MG
ZL2=/sdcard/Android/data/com.movtery.zalithlauncher.v2
PKG=com.dsh.plugin.driver.g720
DRV=/data/local/tmp/libvulkan_freedreno.so
BASE_LIST="$MG/hs_err-baseline.txt"

mkdir -p "$MG"

echo "=== ① hs_err 基线（坑②）==="
# 旧基线先留档，避免覆盖丢证据
[ -f "$BASE_LIST" ] && mv -f "$BASE_LIST" "$MG/hs_err-baseline-prev.txt"
# hs_err 可能在版本目录下，也可能被挪走 ⇒ 在整个 ZL2 数据区里找（有界深度）
find "$ZL2/files" -maxdepth 5 -name 'hs_err_pid*.log' 2>/dev/null | sed 's|.*/||' | sort > "$BASE_LIST"
echo "   基线条目数 = $(wc -l < "$BASE_LIST")  ⇒ $BASE_LIST"
echo "   现存 hs_err（前 3）：$(head -3 "$BASE_LIST" | tr '\n' ' ')"

echo "=== ② 解驱动并校验（坑①）==="
if ! unzip -p "$APK" lib/arm64-v8a/libvulkan_freedreno.so > "$DRV" 2>/dev/null; then
  echo "   ✗ APK 里没有 lib/arm64-v8a/libvulkan_freedreno.so ⇒ APK 可能是空载荷"; exit 2
fi
SZ=$(stat -c%s "$DRV" 2>/dev/null || echo 0)
GOT=$(sha256sum "$DRV" | cut -c1-16)
echo "   驱动: $SZ 字节  sha256前16 = $GOT   期望 = $EXPECT"
if [ "$GOT" != "$EXPECT" ]; then echo "   ✗ 不匹配 ⇒ 停（否则测的是旧版 ✗）"; exit 2; fi
echo "   ✓ 匹配，/data/local/tmp 已是目标版驱动"

echo "=== ③ 安装插件 APK（坑⑦：-r -d -t + 取命令自身退出码）==="
INST_OUT=$(cmd package install -r -d -t "$APK" 2>&1)
INST_RC=$?
echo "$INST_OUT" | tail -2
if [ "$INST_RC" -ne 0 ]; then echo "   ✗ 安装失败 rc=$INST_RC"; exit 3; fi
echo "   已装：$(dumpsys package $PKG 2>/dev/null | grep -m1 versionCode | tr -d ' ')  $(dumpsys package $PKG 2>/dev/null | grep -m1 versionName | tr -d ' ')"

echo "=== ④ 采集准备（旧日志改名，防陈旧混淆）==="
pkill -x logcat 2>/dev/null
[ -f "$MG/cap.txt" ] && mv -f "$MG/cap.txt" "$MG/cap-prev.txt"
[ -f "$MG/mgl.log" ] && mv -f "$MG/mgl.log" "$MG/mgl-prev.log"
setsid nohup sh -c 'logcat -b all -v time > /sdcard/MG/cap.txt 2>&1' >/dev/null 2>&1 </dev/null &
echo "   logcat pid = $(pidof logcat 2>/dev/null || echo '未起 ✗')   cap.txt = $(stat -c%s "$MG/cap.txt" 2>/dev/null || echo 0) 字节"

echo "=== ⑤ 准备完毕（不拉起游戏 —— 坑③）==="
echo "   下一步由 AI 走 app 层：android_app_launch(ZL2) ⇒ 确认前台 ⇒ android_act_input tap 2002 948"
echo "   判据行必须出现: Magma (MobileGL Core) (Mali-G720 MC12, Vulkan 1.4.363, Driver 26.2.99)"
echo "PREP_DONE"
