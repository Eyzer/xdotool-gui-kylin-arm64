#!/bin/bash
# 第二阶段：重新取干净的 LF 版源码 -> 编译 -> 打包 -> Xvfb 冒烟验证 + 截图
set -euo pipefail

HOSTDIR=/mnt/c/Users/Administrator/WorkBuddy/2026-10-12-17-09-29
GUI_HOST="$HOSTDIR/kylin-build"
R=/opt/kylin-arm64
SRC_HOST=/opt/xdotool_src

step() { echo; echo "===== $* ====="; }

step "1/5 获取干净源码（LF 行尾）"
if [ ! -d "$SRC_HOST/.git" ]; then
  rm -rf "$SRC_HOST"
  git config --global core.autocrlf false || true
  git clone --depth 1 https://github.com/jordansissel/xdotool.git "$SRC_HOST"
else
  git -C "$SRC_HOST" pull --depth 1 || true
fi
sh "$SRC_HOST/version.sh" || true
head -1 "$SRC_HOST/VERSION" || true

step "2/5 重新暂存源码"
rm -rf "$R/build"
mkdir -p "$R/build"
cp -a "$SRC_HOST" "$R/build/xdotool_src"
cp -a "$GUI_HOST/src"    "$R/build/gui"
cp -a "$GUI_HOST/assets" "$R/build/assets"
cp -a "$GUI_HOST/deb"    "$R/build/deb"
find "$R/build/gui" "$R/build/assets" "$R/build/deb" -type f -exec sed -i 's/\r$//' {} +
mount | grep -q " $R/proc " || mount -t proc proc "$R/proc" || true
mount | grep -q " $R/dev "  || mount --bind /dev "$R/dev" || true
cp -f /etc/resolv.conf "$R/etc/resolv.conf" || true

step "3/5 编译"
chroot "$R" /bin/bash -eux <<'EOF'
export LC_ALL=C.UTF-8
cd /build/xdotool_src
rm -f *.o *.a && make xdo.o xdo_search.o && ar rcs libxdo.a xdo.o xdo_search.o && ls -l libxdo.a

cd /build/gui
rm -f Makefile .qmake.stash main.o moc_predefs.h main.moc
qmake xdotool-gui.pro && make -j"$(nproc)"
ls -l xdotool-gui

cd /build/xdotool_src
make xdotool && ls -l xdotool
EOF

step "4/5 打包 deb"
chroot "$R" /bin/bash -eux <<'EOF'
export LC_ALL=C.UTF-8
P=/build/pkg
rm -rf "$P"
mkdir -p "$P/DEBIAN" "$P/usr/bin" "$P/usr/share/applications" \
         "$P/usr/share/icons/hicolor/scalable/apps" "$P/usr/share/doc/xdotool-gui"
cp /build/deb/control  "$P/DEBIAN/control"
cp /build/deb/postinst "$P/DEBIAN/postinst"
chmod 755 "$P/DEBIAN/postinst"
install -m 755 /build/gui/xdotool-gui       "$P/usr/bin/xdotool-gui"
install -m 755 /build/xdotool_src/xdotool   "$P/usr/bin/xdotool-gui-cli"
install -m 644 /build/assets/xdotool-gui.desktop "$P/usr/share/applications/xdotool-gui.desktop"
install -m 644 /build/assets/xdotool-gui.svg     "$P/usr/share/icons/hicolor/scalable/apps/xdotool-gui.svg"
cp /build/xdotool_src/COPYRIGHT "$P/usr/share/doc/xdotool-gui/copyright"
echo "--- glibc 符号版本需求 ---"
objdump -T "$P/usr/bin/xdotool-gui" | grep -o 'GLIBC_[0-9.]*' | sort -u | tail -5
echo "--- 动态库依赖 ---"
objdump -p "$P/usr/bin/xdotool-gui" | grep NEEDED
rm -f /build/xdotool-gui_*.deb
dpkg-deb --root-owner-group -Zxz -b "$P" /build/xdotool-gui_1.0.0-1_arm64.deb
dpkg-deb -I /build/xdotool-gui_1.0.0-1_arm64.deb | head -20
dpkg-deb -c /build/xdotool-gui_1.0.0-1_arm64.deb
EOF

step "5/5 Xvfb 冒烟验证 + 截图"
chroot "$R" /bin/bash -eux <<'EOF'
export DEBIAN_FRONTEND=noninteractive LC_ALL=C.UTF-8
apt-get -o Acquire::AllowInsecureRepositories=true install -y --allow-unauthenticated --no-install-recommends \
  xvfb x11-utils imagemagick fonts-wqy-zenhei fonts-dejavu-core
dpkg -i /build/xdotool-gui_1.0.0-1_arm64.deb
dpkg -l xdotool-gui | tail -2

pkill -f 'Xvfb :99' || true
sleep 1
Xvfb :99 -screen 0 1260x860x24 -nolisten tcp >/build/xvfb.log 2>&1 &
sleep 4
export DISPLAY=:99
xdpyinfo -display :99 | grep -iE 'XTest|dimensions' || true

for t in 0 1 2 3; do
  pkill -f 'xdotool-gui --tab' || true
  sleep 1
  /usr/bin/xdotool-gui --tab $t >/build/app$t.log 2>&1 &
  sleep 12
  echo "--- tab=$t running=$(pgrep -c -f 'xdotool-gui --tab' || echo 0)"
  cat /build/app$t.log || true
  import -display :99 -window root /build/tab$t.png || true
done

echo "=== CLI / libxdo 功能验证 ==="
xdotool-gui-cli getmouselocation
xdotool-gui-cli mousemove 640 400
xdotool-gui-cli getmouselocation
xdotool-gui-cli search --onlyvisible --name '.' | wc -l
xdotool-gui-cli getactivewindow
xdotool-gui-cli getactivewindow getwindowname || true
xdotool-gui-cli getactivewindow getwindowgeometry || true

pkill -f 'xdotool-gui --tab' || true
sleep 1
pkill -f 'Xvfb :99' || true
ls -l /build/tab*.png
EOF

mkdir -p "$GUI_HOST/out/shots"
rm -f "$GUI_HOST/out/shots"/*.png "$GUI_HOST/out/shots"/*.log
cp -f "$R/build/xdotool-gui_1.0.0-1_arm64.deb" "$GUI_HOST/out/"
cp -f $R/build/tab*.png "$GUI_HOST/out/shots/" 2>/dev/null || true
cp -f $R/build/app*.log "$GUI_HOST/out/shots/" 2>/dev/null || true
step "完成"
ls -lh "$GUI_HOST/out/" "$GUI_HOST/out/shots/"
