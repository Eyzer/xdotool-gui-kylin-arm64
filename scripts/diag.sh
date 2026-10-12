#!/bin/bash
set -uo pipefail
R=/opt/kylin-arm64

chroot "$R" /bin/bash <<'EOF'
export LC_ALL=C.UTF-8
pkill -f 'Xvfb :99' || true; sleep 1
Xvfb :99 -screen 0 1260x860x24 -nolisten tcp >/build/xvfb.log 2>&1 &
sleep 4
export DISPLAY=:99
echo "--- 初始位置"; xdotool-gui-cli getmouselocation
echo "--- mousemove 300 200"; xdotool-gui-cli mousemove 300 200; echo "rc=$?"
sleep 1; xdotool-gui-cli getmouselocation
echo "--- mousemove --sync 500 120"; xdotool-gui-cli mousemove --sync 500 120; echo "rc=$?"
xdotool-gui-cli getmouselocation
echo "--- 版本"; xdotool-gui-cli --version
echo "--- xev 事件捕获测试"
xev -geometry 400x300+20+20 >/build/xev.log 2>&1 &
sleep 3
WID=$(xdotool-gui-cli search --onlyvisible --name 'xev' | head -1)
echo "xev WID=$WID"
xdotool-gui-cli windowfocus "$WID"; echo "focus rc=$?"
sleep 1
xdotool-gui-cli getwindowfocus
xdotool-gui-cli key --window "$WID" a; echo "key rc=$?"
xdotool-gui-cli key --clearmodifiers --window "$WID" ctrl+c; echo "key2 rc=$?"
xdotool-gui-cli mousemove --window "$WID" 50 50
xdotool-gui-cli click --window "$WID" 1; echo "click rc=$?"
xdotool-gui-cli type --window "$WID" --delay 10 "hi"; echo "type rc=$?"
sleep 2
echo "KeyPress=$(grep -c KeyPress /build/xev.log) KeyRelease=$(grep -c KeyRelease /build/xev.log)"
echo "ButtonPress=$(grep -c ButtonPress /build/xev.log) MotionNotify=$(grep -c MotionNotify /build/xev.log)"
echo "--- 扩展列表 ---"
xdpyinfo -display :99 | awk '/number of extensions/,/^default screen/' | head -30
echo "--- xev 前 25 行 ---"
head -25 /build/xev.log
pkill -f xev || true
pkill -f 'Xvfb :99' || true
EOF
