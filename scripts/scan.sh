#!/bin/bash
set -uo pipefail
R=/opt/kylin-arm64

chroot "$R" /bin/bash <<'EOF'
export LC_ALL=C.UTF-8
pkill -f 'Xvfb :99' || true; sleep 1
Xvfb :99 -screen 0 1260x860x24 -nolisten tcp >/build/xvfb.log 2>&1 &
sleep 4
export DISPLAY=:99

xdotool --version >/dev/null 2>&1 || true
/usr/bin/xdotool-gui --tab 0 >/build/app.log 2>&1 &
sleep 10
import -display :99 -window root /build/base.png

echo "--- 页签纵向坐标扫描 (x=184)"
for y in 130 138 146 152 158 166 174 182; do
  xdotool-gui-cli mousemove --sync 184 $y >/dev/null; sleep 0.6
  xdotool-gui-cli click 1 >/dev/null; sleep 1.2
  import -display :99 -window root /build/scan_$y.png
  d=$(compare -metric AE /build/base.png /build/scan_$y.png null: 2>&1)
  echo "y=$y  与基线差异像素=$d"
done

echo "--- 横向扫描 (y=最优值待定，先试 200 高度上的按钮)"
xdotool-gui-cli mousemove --sync 184 152 >/dev/null; sleep 0.5
xdotool-gui-cli click 1; sleep 1.2
import -display :99 -window root /build/after_tab.png

pkill -f 'xdotool-gui --tab' || true
sleep 1
pkill -f 'Xvfb :99' || true
EOF
for f in base scan_130 scan_138 scan_146 scan_152 scan_158 scan_166 scan_174 scan_182 after_tab; do
  cp -f "$R/build/$f.png" /mnt/c/Users/Administrator/WorkBuddy/2026-10-12-17-09-29/kylin-build/out/shots/ 2>/dev/null || true
done
echo SCAN_DONE
