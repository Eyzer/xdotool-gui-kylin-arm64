#!/bin/bash
set -uo pipefail
R=/opt/kylin-arm64

chroot "$R" /bin/bash <<'EOF'
export LC_ALL=C.UTF-8
pkill -f 'Xvfb :99' || true; pkill -f xev || true; sleep 1
Xvfb :99 -screen 0 1260x860x24 -nolisten tcp >/build/xvfb.log 2>&1 &
sleep 4
export DISPLAY=:99

xev -geometry 300x200+100+100 >/build/xev2.log 2>&1 &
sleep 3

echo "A. 起点"; xdotool-gui-cli getmouselocation
echo "B. mousemove 200 200 (落在 xev 窗口内)"; xdotool-gui-cli mousemove 200 200; echo "rc=$?"
sleep 1
echo "C. 读回"; xdotool-gui-cli getmouselocation
echo "D. xev MotionNotify=$(grep -c MotionNotify /build/xev2.log) EnterNotify=$(grep -c EnterNotify /build/xev2.log)"

echo "E. mousemove_relative 50 50"; xdotool-gui-cli mousemove_relative 50 50; echo "rc=$?"
sleep 1
echo "F. 读回"; xdotool-gui-cli getmouselocation
echo "G. xev MotionNotify=$(grep -c MotionNotify /build/xev2.log)"

echo "H. 用 XWarpPointer 路径 (--screen 1 会走 warp 分支；此处用 screen 0 对照)"; 
xdotool-gui-cli mousemove --screen 0 700 700; sleep 1
echo "I. 读回"; xdotool-gui-cli getmouselocation

echo "J. 独立校验：xwininfo 查询指针"? true
echo "K. Xvfb 日志"; tail -5 /build/xvfb.log
pkill -f xev || true
pkill -f 'Xvfb :99' || true
EOF
