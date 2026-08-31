#!/bin/sh
set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <version>"
    exit 1
fi

VERSION="$1"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "Building sub-lang Alpine package version $VERSION..."

# Update version in APKBUILD
sed -i "s/^pkgver=.*/pkgver=$VERSION/" APKBUILD
sed -i "s/^pkgrel=.*/pkgrel=0/" APKBUILD

# Update checksums
abuild checksum

# Build the package
if abuild -r; then
    echo "Successfully built sub-lang-$VERSION Alpine package"
else
    echo "Failed to build Alpine package"
    exit 1
fi
