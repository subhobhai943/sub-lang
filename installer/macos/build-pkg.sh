#!/bin/sh
# Build the macOS installer package.
#
#   installer/macos/build-pkg.sh <version> [outdir]
#   installer/macos/build-pkg.sh 1.0.9
#
# Two steps, which is how a macOS installer with a licence page is made:
# pkgbuild turns a staged file tree into a component package, and
# productbuild wraps that in the Installer UI described by
# distribution.xml -- welcome, licence, background artwork, summary.
#
# Needs the Xcode command line tools, so it only runs on macOS.
set -eu

VERSION=${1:?usage: build-pkg.sh <version> [outdir]}
OUTDIR=${2:-.}

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK="${ROOT}/build/macos"
STAGE="${WORK}/root"
RES="${WORK}/resources"
IDENT=com.sublang.toolchain

rm -rf "$WORK"
mkdir -p "$STAGE" "$RES"

# /usr/local, not /usr: /usr is on the read-only system volume and sealed
# by SIP. /usr/local/bin is on the default PATH, and the module resolver
# finds the standard library at ../lib/sub/stdlib from there. The Makefile
# leaves out the freedesktop icon and MIME files on a non-Linux host, so
# this gets the programs, the standard library, the manual and the licence
# and nothing that would be meaningless here.
make -C "$ROOT" install DESTDIR="$STAGE" PREFIX=/usr/local >/dev/null

cp "$ROOT/installer/macos/resources/welcome.html"    "$RES/"
cp "$ROOT/installer/macos/resources/conclusion.html" "$RES/"
cp "$ROOT/installer/macos/background.png"            "$RES/"
# Installer will only show a licence it can read as text; LICENSE has no
# extension, and without one the pane comes up empty.
cp "$ROOT/LICENSE" "$RES/LICENSE.txt"

sed "s/@VERSION@/${VERSION}/g" \
    "$ROOT/installer/macos/distribution.xml" > "$WORK/distribution.xml"

pkgbuild \
    --root "$STAGE" \
    --identifier "$IDENT" \
    --version "$VERSION" \
    --install-location / \
    "$WORK/sub-lang-toolchain.pkg"

mkdir -p "$OUTDIR"
PKG="${OUTDIR}/sub-lang-${VERSION}-macos-arm64.pkg"
productbuild \
    --distribution "$WORK/distribution.xml" \
    --resources "$RES" \
    --package-path "$WORK" \
    "$PKG"

# A package that installs nothing still builds, so look inside.
echo "--- payload ---"
pkgutil --payload-files "$PKG" | sed -n '1,40p'
for want in ./usr/local/bin/subi ./usr/local/bin/subc ./usr/local/bin/sub \
            ./usr/local/lib/sub/stdlib/math.sb; do
    pkgutil --payload-files "$PKG" | grep -qx "$want" \
        || { echo "missing from the package: $want" >&2; exit 1; }
done

echo "$PKG"
