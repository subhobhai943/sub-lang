#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <version> [output_dir]"
    exit 1
fi

VERSION=$1
OUTPUT_DIR=${2:-.}
PROJECT_ROOT="$(realpath "$(dirname "$0")/../..")"
APPDIR="$OUTPUT_DIR/sub-lang.AppDir"
ARCH=$(uname -m)

echo "Preparing AppDir for sub-lang version $VERSION..."

# Clean up any previous AppDir
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/lib/sub-lang/stdlib"

# Copy binaries
# Assuming the binaries are already built and located in the project root or build directory
# Update the source paths if your build system places them elsewhere
for bin in sub subc subi; do
    if [ -f "$PROJECT_ROOT/$bin" ]; then
        cp "$PROJECT_ROOT/$bin" "$APPDIR/usr/bin/"
    elif [ -f "$PROJECT_ROOT/build/$bin" ]; then
        cp "$PROJECT_ROOT/build/$bin" "$APPDIR/usr/bin/"
    elif [ -f "$PROJECT_ROOT/target/release/$bin" ]; then
        cp "$PROJECT_ROOT/target/release/$bin" "$APPDIR/usr/bin/"
    else
        echo "Error: Could not find binary '$bin' in expected locations. Please build the project first."
        exit 1
    fi
    chmod +x "$APPDIR/usr/bin/$bin"
done

# Copy stdlib
cp -r "$PROJECT_ROOT/stdlib/"*.sb "$APPDIR/usr/lib/sub-lang/stdlib/"

# Copy desktop entry and icon
cp "$PROJECT_ROOT/installer/linux/sub-lang.desktop" "$APPDIR/"
cp "$PROJECT_ROOT/installer/linux/icons/sub-lang-256.png" "$APPDIR/sub-lang.png"

# Copy AppRun
cp "$PROJECT_ROOT/installer/appimage/AppRun" "$APPDIR/"
chmod +x "$APPDIR/AppRun"

# Download appimagetool if not present
APPIMAGETOOL="$OUTPUT_DIR/appimagetool"
if [ ! -f "$APPIMAGETOOL" ]; then
    echo "Downloading appimagetool..."
    wget -q -O "$APPIMAGETOOL" "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-$ARCH.AppImage"
    chmod +x "$APPIMAGETOOL"
fi

# Build the AppImage
echo "Building AppImage..."
export ARCH
"$APPIMAGETOOL" "$APPDIR" "$OUTPUT_DIR/sub-lang-$VERSION-$ARCH.AppImage"

echo "Success! AppImage created at $OUTPUT_DIR/sub-lang-$VERSION-$ARCH.AppImage"
