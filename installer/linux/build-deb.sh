#!/bin/sh
# Build the Debian/Ubuntu package.
#
#   installer/linux/build-deb.sh <version> <deb-arch> [outdir]
#   installer/linux/build-deb.sh 1.0.9 amd64
#
# The file layout is not defined here: `make install PREFIX=/usr` is, and
# this script stages into a DESTDIR and then adds the things that are
# specific to a Debian package -- the control file, the machine-readable
# copyright, the changelog, and the maintainer scripts that refresh the
# icon, MIME and desktop caches.
#
# It is a script rather than steps in a workflow so that it can be run and
# checked on a laptop. The .deb used to be assembled inline in release.yml,
# which meant the only way to find out whether a change to it worked was to
# tag a release.
set -eu

VERSION=${1:?usage: build-deb.sh <version> <deb-arch> [outdir]}
ARCH=${2:?usage: build-deb.sh <version> <deb-arch> [outdir]}
OUTDIR=${3:-.}

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
PKG="sub-lang_${VERSION}_${ARCH}"
STAGE="${ROOT}/build/deb/${PKG}"

rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN"

# /usr, not /usr/local: Debian policy reserves /usr/local for the local
# administrator and forbids a package to write there. The module resolver
# looks for the standard library at ../lib/sub/stdlib relative to the
# binary, so /usr/bin/subi finds /usr/lib/sub/stdlib.
make -C "$ROOT" install DESTDIR="$STAGE" PREFIX=/usr >/dev/null

# Manual pages are compressed in a Debian package, and `gzip -n` keeps the
# timestamp out of the output so the same input builds byte-identically.
for m in "$STAGE"/usr/share/man/man1/*.1; do
    gzip -9n "$m"
done

# Debian requires shipped binaries to be stripped, and the package was
# carrying the full symbol table of three C programs for no one's benefit.
# rpmbuild does this itself; dpkg-deb does not.
strip --strip-unneeded "$STAGE"/usr/bin/sub "$STAGE"/usr/bin/subc \
                       "$STAGE"/usr/bin/subi

DOC="$STAGE/usr/share/doc/sub-lang"
# The licence belongs in `copyright`, in the machine-readable format, and
# a second copy under another name is a lintian error.
rm -f "$DOC/LICENSE"
cp "$ROOT/installer/linux/copyright" "$DOC/copyright"
chmod 644 "$DOC/copyright"

# changelog.gz, not changelog.Debian.gz: the version carries no Debian
# revision (1.0.9, not 1.0.9-1), which makes this a native package, and a
# native package's changelog goes under the plain name.
printf '%s\n' \
  "sub-lang (${VERSION}) stable; urgency=low" \
  "" \
  "  * SUB Language ${VERSION}. See" \
  "    https://github.com/subhobhai943/sub-lang/releases/tag/v${VERSION}" \
  "" \
  " -- SUB Language Project <noreply@github.com>  $(date -R)" \
  > "$DOC/changelog"
gzip -9n "$DOC/changelog"

INSTALLED_KB=$(du -ks "$STAGE" | cut -f1)

cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: sub-lang
Version: ${VERSION}
Section: devel
Priority: optional
Architecture: ${ARCH}
Maintainer: SUB Language Project <noreply@github.com>
Installed-Size: ${INSTALLED_KB}
Depends: libc6
Recommends: gcc
Homepage: https://github.com/subhobhai943/sub-lang
Description: SUB programming language toolchain
 SUB is a small language with one specification and many backends: the
 interpreter defines what a program prints and what status it exits with,
 and every compiler and transpiler is tested against it.
 .
 This package installs three tools:
 .
  * subi - the interpreter and REPL, and the language specification
  * subc - the native compiler; on x86-64 it emits machine code and writes
    the ELF itself, with no C toolchain involved
  * sub  - the transpiler, to thirteen other languages
 .
 gcc is recommended, not required: subc only needs a C compiler for the
 programs its machine-code backend cannot build yet.
CONTROL

# md5sums lets `dpkg --verify` and debsums detect a file that changed after
# installation. dpkg-deb does not generate it.
( cd "$STAGE" && find usr -type f -print0 \
    | sort -z \
    | xargs -0 md5sum > DEBIAN/md5sums )

# The caches a desktop needs refreshed before a .sb file gets its icon and
# the interpreter appears as an application. Each is guarded: none of these
# programs is a dependency, and a package must not fail to install on a
# machine that has no desktop at all.
cat > "$STAGE/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications || true
    fi
    if command -v update-mime-database >/dev/null 2>&1; then
        update-mime-database /usr/share/mime || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -qtf /usr/share/icons/hicolor || true
    fi
fi
exit 0
POSTINST

cat > "$STAGE/DEBIAN/postrm" <<'POSTRM'
#!/bin/sh
set -e
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications || true
    fi
    if command -v update-mime-database >/dev/null 2>&1; then
        update-mime-database /usr/share/mime || true
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -qtf /usr/share/icons/hicolor || true
    fi
fi
exit 0
POSTRM

chmod 755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"

mkdir -p "$OUTDIR"
# --root-owner-group so the package does not record whichever uid built it;
# without it every file is owned by the build account rather than root.
dpkg-deb --root-owner-group --build "$STAGE" "$OUTDIR/${PKG}.deb" >/dev/null
echo "$OUTDIR/${PKG}.deb"
