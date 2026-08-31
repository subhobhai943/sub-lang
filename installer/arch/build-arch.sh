#!/usr/bin/env bash
set -euo pipefail

# Helper script to build the Arch Linux package for sub-lang
# Usage: ./build-arch.sh <version>

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <version>"
    exit 1
fi

VERSION=$1

echo "Building sub-lang version ${VERSION}..."

# Move to the directory containing the PKGBUILD
cd "$(dirname "$0")"

# Update the version in PKGBUILD
sed -i "s/^pkgver=.*/pkgver=${VERSION}/" PKGBUILD

# Download source and update checksums
updpkgsums

# Build the package
makepkg -sf --noconfirm

echo "Build complete! .pkg.tar.zst should be available in $(pwd)"
