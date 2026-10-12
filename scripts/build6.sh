#!/bin/bash
# 第六阶段：修正坐标解析后，由注入点击驱动四页走查
set -uo pipefail

HOSTDIR=/mnt/c/Users/Administrator/WorkBuddy/2026-10-12-17-09-29
GUI_HOST="$HOSTDIR/kylin-build"
R=/opt/kylin-arm64

chroot "$R" /bin/bash <<'EOF'
export LC_ALL=C.UTF-8
dpkg -i /build/xdotool-gui_1.0.0-1_arm64.deb >/dev/null 2>&1
pkill -f 'Xvfb :99' || true; sleep 1
Xvfb :99 -screen 0 1260x900x24 -nolisten tcp >/build/xvfb.log 2>&1 &
sleep 4
export DISPLAY=:99

xdotool-gui --dump-geom > /build/geom.txt 2>/dev/null
cat /build/geom.txt

/usr/bin/xdotool-gui --tab 0 >/build/app.log 2>&1 &
sleep 10
import -display :99 -window root /build/p0.png

while IFS=' ' read -r tag idx name gx gy rest; do
  [ "$tag" = "TAB" ] || continue
  px=${gx#x=}; py=${gy#y=}
  echo "点击页签 $idx=$name -> ($px, $py)"
  xdotool-gui-cli mousemove --sync "$px" "$py"
  sleep 0.6
  xdotool-gui-cli click 1
  sleep 1.8
  import -display :99 -window root "/build/p$idx.png"
done < /build/geom.txt

echo "--- 逐页差异（对比鼠标页）"
for i in 1 2 3; do
  echo "p$i vs p0 差异像素 = $(compare -metric AE /build/p0.png /build/p$i.png null: 2>&1)"
done

echo "--- 窗口搜索按钮点击测试（在窗口页）"
xdotool-gui-cli mousemove --sync 138 100; sleep 0.5; xdotool-gui-cli click 1; sleep 1.5
xdotool-gui-cli mousemove --sync 400 250; sleep 0.5; xdotool-gui-cli click 1; sleep 2.5
import -display :99 -window root /build/p_search.png
echo "搜索页 vs 窗口页差异 = $(compare -metric AE /build/p2.png /build/p_search.png null: 2>&1)"

echo "--- 应用进程存活: $(pgrep -c -f 'xdotool-gui --tab' || echo 0)"
pkill -f 'xdotool-gui --tab' || true; sleep 1
pkill -f 'Xvfb :99' || true
echo "=== 结束 ==="
EOF

rm -rf "$GUI_HOST/out/shots"; mkdir -p "$GUI_HOST/out/shots"
cp -f "$R/build/xdotool-gui_1.0.0-1_arm64.deb" "$GUI_HOST/out/"
cp -f "$R"/build/p0.png "$R"/build/p1.png "$R"/build/p2.png "$R"/build/p3.png "$R"/build/p_search.png "$GUI_HOST/out/shots/" 2>/dev/null || true
cp -f "$R/build/geom.txt" "$GUI_HOST/out/shots/" 2>/dev/null || true
cp -f "$GUI_HOST/使用说明.md" "$GUI_HOST/out/" 2>/dev/null || true
ls -lh "$GUI_HOST/out" "$GUI_HOST/out/shots"
echo EXIT_OK
