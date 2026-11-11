#!/bin/sh
# Builds an .rpm from the working tree. Needs rpmbuild plus the build
# dependencies (gtk4-devel, libadwaita-devel, bluez-libs-devel, meson, gcc).
# On a non-RPM host, run it in a container:
#   podman run --rm -v "$PWD:/src:z" -w /src fedora:latest sh -c \
#     'dnf -y install rpm-build meson gcc pkgconf-pkg-config gtk4-devel \
#      libadwaita-devel bluez-libs-devel && ./packaging/build-rpm.sh'
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

name=momentum-anc
version=$(sed -n "s/^  version: '\(.*\)',/\1/p" meson.build)
top=$root/build-rpm

rm -rf "$top"
mkdir -p "$top/rpmbuild/SOURCES" "$top/rpmbuild/SPECS"

tar -C "$root" -czf "$top/rpmbuild/SOURCES/$name-$version.tar.gz" \
	--exclude=./build --exclude=./build-deb --exclude=./build-rpm \
	--exclude=./.git --exclude='*.deb' --exclude='*.rpm' \
	--transform "s,^\./,$name-$version/," .

sed "s/^Version:.*/Version:        $version/" "packaging/$name.spec" \
	> "$top/rpmbuild/SPECS/$name.spec"

rpmbuild --define "_topdir $top/rpmbuild" -bb "$top/rpmbuild/SPECS/$name.spec" \
	> "$top/rpmbuild.log" 2>&1

find "$top/rpmbuild/RPMS" -name '*.rpm'
