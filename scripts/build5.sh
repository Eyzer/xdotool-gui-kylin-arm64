#!/bin/bash
# 第五阶段：最终编译打包 + 由注入点击驱动的完整界面走查（4 页截图）
set -uo pipefail

HOSTDIR=/mnt/c/Users/Administrator/WorkBuddy/2026-10-12-17-09-29
GUI_HOST="$HOSTDIR/kylin-build"
R=/opt/kylin-arm64

echo "===== 0/4 暂存源码 ====="
rm -rf "$R/build/src_backup"; mkdir -p "$R/build/src_backup"
cp -a "$GUI_HOST/src/." "$R/build/src_backup/"
find "$R/build/src_backup" -type f -exec sed -i 's/\r$//' {} +

echo "===== 1/4 编译 ====="
chroot "$R" /bin/bash -eux <<'EOF'
export LC_ALL=C.UTF-8
rm -rf /build/gui && mkdir -p /build/gui
cp /build/src_backup/main.cpp /build/src_backup/xdotool-gui.pro /build/gui/
cd /build/gui && qmake xdotool-gui.pro && make -j"$(nproc)"
ls -l xdotool-gui
EOF

echo "===== 2/4 打包 ====="
chroot "$R" /bin/bash -eux <<'EOF'
export LC_ALL=C.UTF-8
install -m 755 /build/gui/xdotool-gui /build/pkg/usr/bin/xdotool-gui
rm -f /build/xdotool-gui_1.0.0-1_arm64.deb
dpkg-deb --root-owner-group -Zxz -b /build/pkg /build/xdotool-gui_1.0.0-1_arm64.deb
dpkg-deb -c /build/xdotool-gui_1.0.0-1_arm64.deb
EOF

echo "===== 3/4 注入点击驱动界面走查 ====="
chroot "$R" /bin/bash <<'EOF'
export LC_ALL=C.UTF-8
dpkg -i /build/xdotool-gui_1.0.0-1_arm64.deb >/dev/null 2>&1
pkill -f 'Xvfb :99' || true; sleep 1
Xvfb :99 -screen 0 1260x900x24 -nolisten tcp >/build/xvfb.log 2>&1 &
sleep 4
export DISPLAY=:99

echo "[a] 读取页签真实坐标"
xdotool-gui --dump-geom > /build/geom.txt 2>/dev/null
cat /build/geom.txt

echo "[b] 端到端：由注入点击切换四个页签"
/usr/bin/xdotool-gui --tab 0 >/build/app.log 2>&1 &
sleep 10
import -display :99 -window root /build/page0_鼠标.png

x=0; y=0
while read -r tag idx name gx gy rest; do
  [ "$tag" = "TAB" ] || continue
  echo "点击页签 $idx=$name @ ($gx,$gy)"
  xdotool-gui-cli mousemove --sync "$gx" "$gy" >/dev/null
  sleep 0.5
  xdotool-gui-cli click 1 >/dev/null
  sleep 1.5
  import -display :99 -window root "/build/page$idx.png"
done < /build/geom.txt

echo "[c] 逐页差异校验（应与鼠标页基线显著不同）"
for i in 1 2 3; do
  d=$(compare -metric AE /build/page0_鼠标.png /build/page$i.png null: 2>&1)
  echo "page$i 与 page0 差异像素 = $d"
done

echo "[d] 键盘/鼠标注入功能"
echo "指针: $(xdotool-gui-cli getmouselocation)"
xdotool-gui-cli key --clearmodifiers ctrl+c; echo "key rc=$?"
xdotool-gui-cli type --delay 10 "ok";       echo "type rc=$?"
xdotool-gui-cli click 1;                     echo "click rc=$?"
echo "应用进程存活: $(pgrep -c -f 'xdotool-gui --tab' || echo 0)"

pkill -f 'xdotool-gui --tab' || true; sleep 1
pkill -f 'Xvfb :99' || true
echo "=== 结束 ==="
EOF

echo "===== 4/4 导出 ====="
mkdir -p "$GUI_HOST/out/shots"; rm -rf "$GUI_HOST/out/shots"/*
cp -f "$R/build/xdotool-gui_1.0.0-1_arm64.deb" "$GUI_HOST/out/"
cp -f "$R"/build/page*.png "$GUI_HOST/out/shots/" 2>/dev/null || true
cp -f "$R/build/geom.txt" "$R/build/app.log" "$GUI_HOST/out/shots/" 2>/dev/null || true
ls -lh "$GUI_HOST/out/" "$GUI_HOST/out/shots/"
echo EXIT_OK
