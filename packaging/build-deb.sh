#!/bin/sh
# Builds a .deb from the working tree. Needs dpkg-deb, meson, ninja and the
# build dependencies installed (gtk4, libadwaita-1, bluez-libs headers).
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

name=momentum-anc
version=$(sed -n "s/^  version: '\(.*\)',/\1/p" meson.build)
arch=$(dpkg --print-architecture)
stage=$root/build-deb/pkg

rm -rf "$root/build-deb"
meson setup "$root/build-deb/build" --prefix=/usr --buildtype=release --strip
meson compile -C "$root/build-deb/build"
DESTDIR="$stage" meson install -C "$root/build-deb/build" --no-rebuild

mkdir -p "$stage/DEBIAN"
size=$(du -sk "$stage" | cut -f1)
cat > "$stage/DEBIAN/control" <<EOF
Package: $name
Version: $version
Architecture: $arch
Maintainer: skurtz97 <skurtz97@users.noreply.github.com>
Section: sound
Priority: optional
Installed-Size: $size
Homepage: https://github.com/skurtz97/momentum-anc
Depends: libgtk-4-1 (>= 4.16), libadwaita-1-0 (>= 1.6), libbluetooth3
Description: Control noise cancellation on the Sennheiser MOMENTUM 4
 GTK 4 application that talks to a paired MOMENTUM 4 over Bluetooth Classic
 (GAIA v3 over RFCOMM) and switches noise cancellation, transparency and
 adaptive mode, and shows the battery level.
EOF

out=$root/${name}_${version}_${arch}.deb
dpkg-deb --root-owner-group --build "$stage" "$out"
echo "$out"
