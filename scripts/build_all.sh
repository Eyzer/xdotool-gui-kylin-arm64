#!/bin/bash
# Build xdotool-gui for Kylin OS V10 (arm64) inside an Ubuntu 20.04 arm64 chroot
# using qemu-user-static. Run as root on the Debian host.
set -euo pipefail

HOSTDIR=/mnt/c/Users/Administrator/WorkBuddy/2026-10-12-17-09-29
SRC_HOST="$HOSTDIR/xdotool_src"
GUI_HOST="$HOSTDIR/kylin-build"
R=/opt/kylin-arm64
LOG=/tmp/kylin-build.log
: > "$LOG"

step() { echo "===== $* ====="; }

# ---------------------------------------------------------------- 1. host tools
step "1/6 安装宿主构建工具"
export DEBIAN_FRONTEND=noninteractive
printf 'deb http://deb.debian.org/debian bullseye main\n' > /tmp/extra.list
APT_OPTS="-o Dir::Etc::sourcelist=/tmp/extra.list -o Dir::Etc::sourceparts=/dev/null -o APT::Get::List-Cleanup=0 -o Dir::State::lists=/tmp/aptlists -o Dir::Cache=/tmp/aptcache"
mkdir -p /tmp/aptlists/partial /tmp/aptcache/archives/partial
if ! command -v debootstrap >/dev/null 2>&1 || ! command -v qemu-aarch64-static >/dev/null 2>&1; then
  apt-get $APT_OPTS update
  apt-get $APT_OPTS install -y debootstrap qemu-user-static binfmt-support
fi
command -v debootstrap
ls -l /usr/bin/qemu-aarch64-static

# ---------------------------------------------------------------- 2. debootstrap
step "2/6 创建 Ubuntu 20.04 arm64 根文件系统"
if [ ! -x "$R/usr/bin/apt-get" ]; then
  rm -rf "$R"
  mkdir -p "$R"
  if [ ! -e /usr/share/debootstrap/scripts/focal ]; then
    ln -sf gutsy /usr/share/debootstrap/scripts/focal
  fi
  debootstrap --arch=arm64 --variant=minbase --no-check-gpg \
              --include=ca-certificates focal "$R" \
              http://ports.ubuntu.com/ubuntu-ports
fi
cp -f /usr/bin/qemu-aarch64-static "$R/usr/bin/" 2>/dev/null || true

# ---------------------------------------------------------------- 3. stage sources
step "3/6 复制源码到构建目录"
rm -rf "$R/build"
mkdir -p "$R/build"
cp -a "$SRC_HOST" "$R/build/xdotool_src"
cp -a "$GUI_HOST/src" "$R/build/gui"
cp -a "$GUI_HOST/assets" "$R/build/assets"
cp -a "$GUI_HOST/deb" "$R/build/deb"
ls "$R/build"
ls "$R/build/xdotool_src" | head -5

mount | grep -q " $R/proc " || mount -t proc proc "$R/proc" || true
mount | grep -q " $R/sys "  || mount -t sysfs sysfs "$R/sys" || true
mount | grep -q " $R/dev "  || mount --bind /dev "$R/dev" || true
cp -f /etc/resolv.conf "$R/etc/resolv.conf" || true

# ---------------------------------------------------------------- 4. deps
step "4/6 chroot 内安装编译依赖"
chroot "$R" /bin/bash -eux <<'EOF'
export DEBIAN_FRONTEND=noninteractive
export LC_ALL=C.UTF-8
echo "deb http://ports.ubuntu.com/ubuntu-ports focal main universe"      > /etc/apt/sources.list
echo "deb http://ports.ubuntu.com/ubuntu-ports focal-updates main universe" >> /etc/apt/sources.list
echo "deb http://ports.ubuntu.com/ubuntu-ports focal-security main universe" >> /etc/apt/sources.list
apt-get -o Acquire::AllowInsecureRepositories=true update -y
apt-get install -y --allow-unauthenticated --no-install-recommends \
  ubuntu-keyring ca-certificates 2>/dev/null || true
apt-get -o Acquire::AllowInsecureRepositories=true update -y
apt-get install -y --allow-unauthenticated --no-install-recommends \
  build-essential pkg-config ca-certificates \
  libx11-dev libxtst-dev libxinerama-dev libxkbcommon-dev \
  qtbase5-dev qtbase5-dev-tools qt5-qmake
g++ --version | head -1
qmake -v
pkg-config --modversion x11 xkbcommon
EOF

# ---------------------------------------------------------------- 5. build
step "5/6 编译 libxdo.a 与 Qt5 图形界面"
chroot "$R" /bin/bash -eux <<'EOF'
export LC_ALL=C.UTF-8
cd /build/xdotool_src
rm -f *.o *.a
make xdo.o xdo_search.o
ar rcs libxdo.a xdo.o xdo_search.o
ls -l libxdo.a

cd /build/gui
rm -f Makefile .qmake.stash main.o
qmake xdotool-gui.pro
make -j"$(nproc)"
ls -l xdotool-gui
file xdotool-gui || true

cd /build/xdotool_src
make clean >/dev/null 2>&1 || true
make xdotool
ls -l xdotool
EOF

# ---------------------------------------------------------------- 6. deb
step "6/6 打包 deb"
chroot "$R" /bin/bash -eux <<'EOF'
export LC_ALL=C.UTF-8
P=/build/pkg
rm -rf "$P"
mkdir -p "$P/DEBIAN" \
         "$P/usr/bin" \
         "$P/usr/share/applications" \
         "$P/usr/share/icons/hicolor/scalable/apps" \
         "$P/usr/share/doc/xdotool-gui"

cp /build/deb/control  "$P/DEBIAN/control"
cp /build/deb/postinst "$P/DEBIAN/postinst"
chmod 755 "$P/DEBIAN/postinst"

install -m 755 /build/gui/xdotool-gui     "$P/usr/bin/xdotool-gui"
install -m 755 /build/xdotool_src/xdotool "$P/usr/bin/xdotool-gui-cli"
install -m 644 /build/assets/xdotool-gui.desktop "$P/usr/share/applications/xdotool-gui.desktop"
install -m 644 /build/assets/xdotool-gui.svg     "$P/usr/share/icons/hicolor/scalable/apps/xdotool-gui.svg"

cp /build/xdotool_src/COPYRIGHT "$P/usr/share/doc/xdotool-gui/copyright"
gzip -9 -n -c /build/xdotool_src/CHANGELIST > "$P/usr/share/doc/xdotool-gui/changelist.gz" || true

# 最低 glibc 需求校验
if grep -q 'GLIBC_2\.3[2-9]\|GLIBC_2\.4\|GLIBC_2\.5' <(objdump -T "$P/usr/bin/xdotool-gui" | grep GLIBC | sed 's/.*(//;s/).*//' | sort -u); then
  echo "!!! 警告：依赖 glibc 版本高于 2.31"
  objdump -T "$P/usr/bin/xdotool-gui" | grep GLIBC | sed 's/.*(//;s/).*//' | sort -u
else
  echo "--- glibc 符号版本 ---"
  objdump -T "$P/usr/bin/xdotool-gui" | grep GLIBC | sed 's/.*(//;s/).*//' | sort -u
fi

dpkg-deb --root-owner-group -Zxz -b "$P" /build/xdotool-gui_1.0.0-1_arm64.deb
dpkg-deb -I /build/xdotool-gui_1.0.0-1_arm64.deb
dpkg-deb -c /build/xdotool-gui_1.0.0-1_arm64.deb
EOF

mkdir -p "$GUI_HOST/out"
cp -f "$R/build/xdotool-gui_1.0.0-1_arm64.deb" "$GUI_HOST/out/"
step "完成"
ls -lh "$GUI_HOST/out/"
