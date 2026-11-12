#!/bin/sh
# Build a FreeBSD pkg (.txz) for the SUB toolchain.
#   Usage: installer/bsd/build-txz.sh <version> [output-dir]
# Run where the binaries were built (normally a FreeBSD runner, so the
# ELF and libc match). Produces sub-lang-<version>-<arch>.pkg — install
# with: sudo pkg install ./sub-lang-<version>-amd64.pkg
set -eu

VERSION="${1:?usage: build-txz.sh <version> [output-dir]}"
OUT="${2:-dist}"
MACHINE="$(uname -m)"
case "$MACHINE" in
    x86_64|amd64) PKG_ARCH=amd64 ;;
    arm64|aarch64) PKG_ARCH=aarch64 ;;
    *) PKG_ARCH="$MACHINE" ;;
esac
PREFIX="${PREFIX:-/usr/local}"

for t in sub subc subi; do
    [ -x "./$t" ] || { echo "error: ./$t missing (run make all first)" >&2; exit 1; }
done

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
ROOT="$STAGE$PREFIX"
mkdir -p "$ROOT/bin" "$ROOT/share/man/man1" "$ROOT/lib/sub"

cp sub subc subi "$ROOT/bin/"
# module.c looks for the stdlib at ../lib/sub/stdlib from the binary, so
# /usr/local/bin/subi finds /usr/local/lib/sub/stdlib — same rule the .deb
# and the macOS .pkg honour.
cp -r stdlib "$ROOT/lib/sub/stdlib"
if [ -d installer/man ] && [ -n "$(ls installer/man/man1 2>/dev/null)" ]; then
    cp installer/man/man1/* "$ROOT/share/man/man1/"
fi

sha_of() {
    if command -v sha256 >/dev/null 2>&1; then sha256 -q "$1"
    else sha256sum "$1" | awk '{print $1}'; fi
}

FLATSIZE=$(find "$ROOT" -type f -exec wc -c {} + | tail -1 | awk '{print $1}')

# files map: "/usr/local/bin/sub": { "size": N, "sha256": "..." }
FILES=$(cd "$STAGE" && find . -type f | sed 's|^\./||' | sort | awk -v root="$STAGE" '
    { lines[NR] = $0 }
    END { for (i = 1; i <= NR; i++) print lines[i] }')

i=0; TOTAL=$(printf '%s\n' "$FILES" | wc -l | tr -d ' ')
BODY=""
for f in $FILES; do
    i=$((i + 1))
    size=$(wc -c < "$STAGE/$f" | tr -d ' ')
    digest=$(sha_of "$STAGE/$f")
    BODY="$BODY    \"/$f\": { \"size\": $size, \"sha256\": \"$digest\" }"
    [ "$i" -lt "$TOTAL" ] && BODY="$BODY,"
    BODY="$BODY
"
done

cat > "$STAGE/+MANIFEST" <<EOF
{
  "arch": "$PKG_ARCH",
  "origin": "devel/sub-lang",
  "version": "$VERSION",
  "name": "sub-lang",
  "comment": "SUB language toolchain: transpiler, native compiler, interpreter",
  "desc": "One .sb source: transpiled to 13 languages, compiled to native code, or interpreted live. Ships sub, subc, subi and the standard library.",
  "maintainer": "Subhadip Sarkar <sarkarsubhadip604@gmail.com>",
  "www": "https://subhobhai943.github.io/sub-lang/",
  "prefix": "$PREFIX",
  "licenses": ["MIT"],
  "flatsize": $FLATSIZE,
  "deps": {},
  "files": {
$BODY  }
}
EOF

mkdir -p "$OUT"
case "$OUT" in /*) OUT_ABS="$OUT" ;; *) OUT_ABS="$(pwd)/$OUT" ;; esac
PKG_NAME="sub-lang-$VERSION-$PKG_ARCH.pkg"
# pkg create is the official packer; its output is just a tar with +MANIFEST
# at the root, so tar is a faithful fallback when the pkg tooling is absent.
if command -v pkg >/dev/null 2>&1; then
    ( cd "$STAGE" && pkg create -M +MANIFEST -m . >/dev/null && mv "sub-lang-$VERSION.pkg" "$OUT_ABS/$PKG_NAME" ) \
    || ( cd "$STAGE" && tar cJf "$OUT_ABS/$PKG_NAME" +MANIFEST . )
else
    ( cd "$STAGE" && tar cJf "$OUT_ABS/$PKG_NAME" +MANIFEST . )
fi
echo "built: $OUT/$PKG_NAME"
