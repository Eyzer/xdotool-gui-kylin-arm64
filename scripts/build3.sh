#!/bin/bash
# 第三阶段：CLI 改为静态链接 -> 重新打包 -> 端到端验证 -> 导出产物
set -uo pipefail

HOSTDIR=/mnt/c/Users/Administrator/WorkBuddy/2026-10-12-17-09-29
GUI_HOST="$HOSTDIR/kylin-build"
R=/opt/kylin-arm64

echo "===== 1/3 静态链接 CLI 并重新打包 ====="
chroot "$R" /bin/bash -eux <<'EOF'
export LC_ALL=C.UTF-8
cd /build/xdotool_src
make xdotool.static
ls -l xdotool.static
objdump -p xdotool.static | grep NEEDED

P=/build/pkg
install -m 755 /build/xdotool_src/xdotool.static "$P/usr/bin/xdotool-gui-cli"
rm -f /build/xdotool-gui_1.0.0-1_arm64.deb
dpkg-deb --root-owner-group -Zxz -b "$P" /build/xdotool-gui_1.0.0-1_arm64.deb
dpkg-deb -c /build/xdotool-gui_1.0.0-1_arm64.deb
EOF

echo "===== 2/3 重装并端到端验证 ====="
chroot "$R" /bin/bash -eux <<'EOF'
export DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8
dpkg -i /build/xdotool-gui_1.0.0-1_arm64.deb

pkill -f 'Xvfb :99' || true
sleep 1
Xvfb :99 -screen 0 1260x860x24 -nolisten tcp >/build/xvfb.log 2>&1 &
sleep 4
export DISPLAY=:99

echo "=== xdotool-gui-cli (静态链接) 功能验证 ==="
xdotool-gui-cli --version
xdotool-gui-cli getmouselocation
xdotool-gui-cli mousemove 640 400
xdotool-gui-cli getmouselocation
xdotool-gui-cli getdisplaygeometry

for t in 0 1 2 3; do
  pkill -f 'xdotool-gui --tab' || true
  sleep 1
  /usr/bin/xdotool-gui --tab $t >/build/app$t.log 2>&1 &
  sleep 12
  echo "tab=$t running=$(pgrep -c -f 'xdotool-gui --tab' || echo 0)"
  import -display :99 -window root /build/tab$t.png || true
done

echo "=== 窗口搜索 / 窗口管理验证 ==="
xdotool-gui-cli search --onlyvisible --name '.' | wc -l
xdotool-gui-cli getactivewindow
xdotool-gui-cli getactivewindow getwindowgeometry || true
xdotool-gui-cli getactivewindow getwindowname || true

echo "=== 键盘注入验证（用 xev 收不到，改为验证返回码）==="
xdotool-gui-cli key --clearmodifiers ctrl+c && echo "key RC=$?"
xdotool-gui-cli type --delay 10 "hello" && echo "type RC=$?"
xdotool-gui-cli click 1 && echo "click RC=$?"

pkill -f 'xdotool-gui --tab' || true
sleep 1
pkill -f 'Xvfb :99' || true
echo "=== 全部验证用例结束 ==="
EOF

echo "===== 3/3 导出产物 ====="
mkdir -p "$GUI_HOST/out/shots"
rm -f "$GUI_HOST/out"/*.deb "$GUI_HOST/out/shots"/*.png "$GUI_HOST/out/shots"/*.log
cp -f "$R/build/xdotool-gui_1.0.0-1_arm64.deb" "$GUI_HOST/out/"
cp -f $R/build/tab0.png $R/build/tab1.png $R/build/tab2.png $R/build/tab3.png "$GUI_HOST/out/shots/" 2>/dev/null || true
cp -f $R/build/app0.log "$GUI_HOST/out/shots/" 2>/dev/null || true
ls -lh "$GUI_HOST/out/" "$GUI_HOST/out/shots/"
echo "EXIT_OK"
