#!/bin/bash
# 在 arm64 chroot 内用 Xvfb 对打包产物做端到端冒烟验证，并抓取界面截图。
set -euo pipefail

HOSTDIR=/mnt/c/Users/Administrator/WorkBuddy/2026-10-12-17-09-29
GUI_HOST="$HOSTDIR/kylin-build"
R=/opt/kylin-arm64

chroot "$R" /bin/bash -eux <<'EOF'
export DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8
apt-get -o Acquire::AllowInsecureRepositories=true install -y --allow-unauthenticated --no-install-recommends \
  xvfb x11-utils x11-apps imagemagick fonts-wqy-zenhei fonts-dejavu-core
dpkg -i /build/xdotool-gui_1.0.0-1_arm64.deb
dpkg -l xdotool-gui | tail -2

pkill -f 'Xvfb :99' || true
sleep 1
Xvfb :99 -screen 0 1280x820x24 -nolisten tcp >/build/xvfb.log 2>&1 &
sleep 3
export DISPLAY=:99
xdpyinfo -display :99 | grep -iE 'XTest|dimensions' || true

for t in 0 1 2 3; do
  pkill -f 'xdotool-gui --tab' || true
  sleep 1
  /usr/bin/xdotool-gui --tab $t >/build/app$t.log 2>&1 &
  sleep 10
  echo "--- tab=$t alive=$(pgrep -c -f 'xdotool-gui --tab' || true)"
  cat /build/app$t.log || true
  import -display :99 -window root /build/tab$t.png || true
done

echo "=== CLI / libxdo 功能验证 ==="
xdotool-gui-cli getmouselocation
xdotool-gui-cli mousemove 640 400
xdotool-gui-cli getmouselocation
xdotool-gui-cli search --onlyvisible --name '.' | wc -l
xdotool-gui-cli getactivewindow || true
xdotool-gui-cli getactivewindow getwindowname || true

pkill -f 'xdotool-gui --tab' || true
sleep 1
pkill -f 'Xvfb :99' || true
ls -l /build/tab*.png
EOF

mkdir -p "$GUI_HOST/out/shots"
cp -f $R/build/tab*.png "$GUI_HOST/out/shots/" 2>/dev/null || true
cp -f $R/build/app*.log "$GUI_HOST/out/shots/" 2>/dev/null || true
ls -lh "$GUI_HOST/out/shots"
