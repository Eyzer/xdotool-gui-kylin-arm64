#!/bin/bash
# 第四阶段：修正 XTest 探测后重新编译、打包、完整验证、导出
set -uo pipefail

HOSTDIR=/mnt/c/Users/Administrator/WorkBuddy/2026-10-12-17-09-29
GUI_HOST="$HOSTDIR/kylin-build"
R=/opt/kylin-arm64

echo "===== 0/3 暂存最新 GUI 源码 ====="
rm -rf "$R/build/src_backup"
mkdir -p "$R/build/src_backup"
cp -a "$GUI_HOST/src/." "$R/build/src_backup/"
find "$R/build/src_backup" -type f -exec sed -i 's/\r$//' {} +
ls -l "$R/build/src_backup"

echo "===== 1/3 重新编译 GUI ====="
chroot "$R" /bin/bash -eux <<'EOF'
export LC_ALL=C.UTF-8
rm -rf /build/gui
mkdir -p /build/gui
cp /build/src_backup/main.cpp /build/src_backup/xdotool-gui.pro /build/gui/
cd /build/gui
qmake xdotool-gui.pro && make -j"$(nproc)"
ls -l xdotool-gui
EOF

echo "===== 2/3 重新打包 ====="
chroot "$R" /bin/bash -eux <<'EOF'
export LC_ALL=C.UTF-8
P=/build/pkg
rm -f "$P/usr/bin/xdotool-gui"
install -m 755 /build/gui/xdotool-gui "$P/usr/bin/xdotool-gui"
rm -f /build/xdotool-gui_1.0.0-1_arm64.deb
dpkg-deb --root-owner-group -Zxz -b "$P" /build/xdotool-gui_1.0.0-1_arm64.deb
dpkg-deb -c /build/xdotool-gui_1.0.0-1_arm64.deb
EOF

echo "===== 3/3 端到端验证 ====="
chroot "$R" /bin/bash <<'EOF'
export LC_ALL=C.UTF-8
dpkg -i /build/xdotool-gui_1.0.0-1_arm64.deb >/dev/null 2>&1

pkill -f 'Xvfb :99' || true; sleep 1
Xvfb :99 -screen 0 1260x860x24 -nolisten tcp >/build/xvfb.log 2>&1 &
sleep 4
export DISPLAY=:99

echo "--- [1] 启动 GUI，确认 XTest 探测 ---"
xdotool-gui --tab 0 >/build/app0.log 2>&1 &
sleep 10
echo "进程存活: $(pgrep -c -f 'xdotool-gui --tab' || echo 0)"
cat /build/app0.log
import -display :99 -window root /build/shot_mouse.png

echo "--- [2] 通过注入鼠标点击切换到「窗口」页 → 再切「键盘」页"
xdotool-gui-cli mousemove --sync 184 151; sleep 1
xdotool-gui-cli click 1; sleep 2
import -display :99 -window root /build/shot_window.png

echo "--- [3] 在「窗口」页点「搜索」按钮 (坐标 420,290)"
xdotool-gui-cli mousemove --sync 420 290; sleep 1
xdotool-gui-cli click 1; sleep 3
import -display :99 -window root /build/shot_search.png

echo "--- [4] 切换「键盘」页并截图"
xdotool-gui-cli mousemove --sync 125 151; sleep 1
xdotool-gui-cli click 1; sleep 2
import -display :99 -window root /build/shot_keyboard.png

echo "--- [5] 切换「其他」页并截图"
xdotool-gui-cli mousemove --sync 250 151; sleep 1
xdotool-gui-cli click 1; sleep 2
import -display :99 -window root /build/shot_misc.png

echo "进程仍在运行: $(pgrep -c -f 'xdotool-gui --tab' || echo 0)"
echo "--- [6] 键鼠注入功能汇总 ---"
echo "指针: $(xdotool-gui-cli getmouselocation)"
xdotool-gui-cli key --clearmodifiers ctrl+c; echo "key rc=$?"
xdotool-gui-cli type --delay 10 "ok";  echo "type rc=$?"
xdotool-gui-cli click 1;                echo "click rc=$?"
echo "可见窗口数: $(xdotool-gui-cli search --onlyvisible --name '.' | wc -l)"

pkill -f 'xdotool-gui --tab' || true
sleep 1
pkill -f 'Xvfb :99' || true
echo "=== 验证结束 ==="
EOF

mkdir -p "$GUI_HOST/out/shots"
rm -f "$GUI_HOST/out"/*.deb "$GUI_HOST/out/shots"/*
cp -f "$R/build/xdotool-gui_1.0.0-1_arm64.deb" "$GUI_HOST/out/"
for f in shot_mouse shot_window shot_search shot_keyboard shot_misc; do
  cp -f "$R/build/$f.png" "$GUI_HOST/out/shots/" 2>/dev/null || true
done
cp -f "$R/build/app0.log" "$GUI_HOST/out/shots/" 2>/dev/null || true
ls -lh "$GUI_HOST/out/" "$GUI_HOST/out/shots/"
echo EXIT_OK
