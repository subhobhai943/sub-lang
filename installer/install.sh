#!/bin/sh
#
# SUB-LANG Universal Installer
# 
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/subhobhai943/sub-lang/main/installer/install.sh | sh
#

set -e

INSTALLER_VERSION="1.0.0"
GITHUB_REPO="subhobhai943/sub-lang"

# -----------------------------------------------------------------------------
# Utility Functions
# -----------------------------------------------------------------------------

# Setup colors if supported by terminal
if [ -t 1 ] && command -v tput >/dev/null 2>&1; then
    RED=$(tput setaf 1)
    GREEN=$(tput setaf 2)
    YELLOW=$(tput setaf 3)
    BLUE=$(tput setaf 4)
    BOLD=$(tput bold)
    RESET=$(tput sgr0)
else
    RED=""
    GREEN=""
    YELLOW=""
    BLUE=""
    BOLD=""
    RESET=""
fi

info() {
    echo "${GREEN}${BOLD}info${RESET}: $1"
}

warn() {
    echo "${YELLOW}${BOLD}warn${RESET}: $1" >&2
}

error() {
    echo "${RED}${BOLD}error${RESET}: $1" >&2
    exit 1
}

print_banner() {
    echo "${BLUE}${BOLD}"
    cat << 'EOF'
  ____  _   _ ____    _                       
 / ___|| | | | __ )  | |    __ _ _ __   __ _ 
 \___ \| | | |  _ \  | |   / _` | '_ \ / _` |
  ___) | |_| | |_) | | |__| (_| | | | | (_| |
 |____/ \___/|____/  |_____\__,_|_| |_|\__, |
                                        |___/ 
EOF
    echo "${RESET}"
}

# -----------------------------------------------------------------------------
# Argument Parsing
# -----------------------------------------------------------------------------

YES_FLAG=0
PREFIX=""

while [ $# -gt 0 ]; do
    case "$1" in
        --help)
            echo "Usage: install.sh [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --help           Show this help message"
            echo "  --version        Show installer version"
            echo "  --prefix=PATH    Install to custom path (e.g., ~/.local or /usr/local)"
            echo "  --yes            Skip interactive prompts (e.g., license acceptance)"
            exit 0
            ;;
        --version)
            echo "sub-lang installer version $INSTALLER_VERSION"
            exit 0
            ;;
        --yes)
            YES_FLAG=1
            ;;
        --prefix=*)
            PREFIX="${1#*=}"
            ;;
        *)
            error "Unknown argument: $1"
            ;;
    esac
    shift
done

# -----------------------------------------------------------------------------
# Platform Detection
# -----------------------------------------------------------------------------

OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"

case "$OS" in
    linux) OS="linux" ;;
    darwin) OS="macos" ;;
    *) error "Unsupported OS: $OS" ;;
esac

case "$ARCH" in
    x86_64|amd64) ARCH="x86_64" ;;
    arm64|aarch64) ARCH="arm64" ;;
    *) error "Unsupported architecture: $ARCH" ;;
esac

info "Detected platform: $OS-$ARCH"

# Determine prefix
if [ -z "$PREFIX" ]; then
    if [ "$(id -u)" = "0" ]; then
        PREFIX="/usr/local"
    else
        PREFIX="$HOME/.local"
    fi
fi
info "Install prefix: $PREFIX"

print_banner

# -----------------------------------------------------------------------------
# License Acceptance
# -----------------------------------------------------------------------------

if [ "$YES_FLAG" -eq 0 ]; then
    echo "SUB-LANG is distributed under the MIT License."
    echo ""
    echo "Copyright (c) $(date +%Y) SUB Language Project"
    echo ""
    echo "Permission is hereby granted, free of charge, to any person obtaining a copy"
    echo "of this software and associated documentation files (the \"Software\"), to deal"
    echo "in the Software without restriction, including without limitation the rights"
    echo "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell"
    echo "copies of the Software, and to permit persons to whom the Software is"
    echo "furnished to do so, subject to the following conditions:"
    echo ""
    echo "The above copyright notice and this permission notice shall be included in all"
    echo "copies or substantial portions of the Software."
    echo ""
    printf "Do you accept the license terms? [y/N] "
    read -r response
    case "$response" in
        [yY][eE][sS]|[yY]) 
            ;;
        *)
            error "Installation aborted by user."
            ;;
    esac
fi

# -----------------------------------------------------------------------------
# Fetch Release Information
# -----------------------------------------------------------------------------

info "Fetching latest release information..."
if command -v curl >/dev/null 2>&1; then
    FETCH="curl -fsSL"
elif command -v wget >/dev/null 2>&1; then
    FETCH="wget -qO-"
else
    error "Neither curl nor wget was found. Please install one to continue."
fi

# Try to get the latest tag from GitHub API
LATEST_TAG=$($FETCH "https://api.github.com/repos/$GITHUB_REPO/releases/latest" 2>/dev/null | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/')
if [ -z "$LATEST_TAG" ]; then
    warn "Failed to fetch latest tag from GitHub API. Defaulting to v1.0.9"
    LATEST_TAG="v1.0.9"
fi

info "Target version: $LATEST_TAG"

# Release assets are named sub-{os}-{arch}.tar.gz
FILENAME="sub-${OS}-${ARCH}.tar.gz"
DOWNLOAD_URL="https://github.com/$GITHUB_REPO/releases/download/${LATEST_TAG}/${FILENAME}"
CHECKSUM_URL="https://github.com/$GITHUB_REPO/releases/download/${LATEST_TAG}/checksums-sha256.txt"

# -----------------------------------------------------------------------------
# Download and Verify
# -----------------------------------------------------------------------------

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

info "Downloading $DOWNLOAD_URL ..."
if command -v curl >/dev/null 2>&1; then
    curl -fSL "$DOWNLOAD_URL" -o "$TMP_DIR/$FILENAME" || error "Failed to download $FILENAME"
    curl -fsSL "$CHECKSUM_URL" -o "$TMP_DIR/sha256sums.txt" || warn "No checksum file found at release. Skipping verification."
else
    wget -q "$DOWNLOAD_URL" -O "$TMP_DIR/$FILENAME" || error "Failed to download $FILENAME"
    wget -q "$CHECKSUM_URL" -O "$TMP_DIR/sha256sums.txt" || warn "No checksum file found at release. Skipping verification."
fi

if [ -f "$TMP_DIR/sha256sums.txt" ]; then
    info "Verifying checksum..."
    cd "$TMP_DIR"
    if command -v sha256sum >/dev/null 2>&1; then
        grep "$FILENAME" sha256sums.txt | sha256sum -c - || error "Checksum verification failed!"
    elif command -v shasum >/dev/null 2>&1; then
        grep "$FILENAME" sha256sums.txt | shasum -a 256 -c - || error "Checksum verification failed!"
    else
        warn "sha256sum or shasum not found. Skipping checksum verification."
    fi
    cd - >/dev/null
fi

# -----------------------------------------------------------------------------
# Extraction and Installation
# -----------------------------------------------------------------------------

info "Extracting and installing to $PREFIX..."

SUDO=""
if [ ! -w "$PREFIX" ] && [ "$(id -u)" != "0" ]; then
    if command -v sudo >/dev/null 2>&1; then
        info "Elevated permissions required to write to $PREFIX, using sudo."
        SUDO="sudo"
    else
        error "No write permission to $PREFIX and sudo is not available."
    fi
fi

$SUDO mkdir -p "$PREFIX/bin" "$PREFIX/lib/sub/stdlib"
EXTRACT_DIR="$TMP_DIR/extracted"
mkdir -p "$EXTRACT_DIR"

tar -xzf "$TMP_DIR/$FILENAME" -C "$EXTRACT_DIR" || error "Failed to extract archive"

# The tarball has a top-level directory like sub-linux-x86_64/ with
# binaries directly inside, plus a stdlib/ subdirectory.
INNER=$(find "$EXTRACT_DIR" -maxdepth 1 -mindepth 1 -type d | head -n 1)
if [ -z "$INNER" ]; then
    INNER="$EXTRACT_DIR"
fi

for tool in sub subc subi; do
    if [ -f "$INNER/$tool" ]; then
        $SUDO install -m 755 "$INNER/$tool" "$PREFIX/bin/$tool"
    fi
done

# Install the standard library where `import` finds it:
# ../lib/sub/stdlib relative to the binary.
if [ -d "$INNER/stdlib" ]; then
    $SUDO cp "$INNER"/stdlib/*.sb "$PREFIX/lib/sub/stdlib/" 2>/dev/null || true
fi

# -----------------------------------------------------------------------------
# Smoke Test
# -----------------------------------------------------------------------------

info "Running smoke test..."
if command -v "$PREFIX/bin/subi" >/dev/null 2>&1; then
    if "$PREFIX/bin/subi" --version >/dev/null 2>&1; then
        info "Smoke test passed successfully."
    else
        warn "Smoke test failed. The binary was installed but did not execute properly."
    fi
else
    warn "Could not locate 'subi' binary in $PREFIX/bin for smoke test."
fi

# -----------------------------------------------------------------------------
# Post-Installation Instructions
# -----------------------------------------------------------------------------

info "Installation complete!"
echo ""
echo "${GREEN}${BOLD}SUB-LANG has been successfully installed!${RESET}"
echo ""
echo "Make sure that ${BOLD}$PREFIX/bin${RESET} is in your PATH."

case $SHELL in
    */zsh) SHELL_RC="$HOME/.zshrc" ;;
    */bash) SHELL_RC="$HOME/.bashrc" ;;
    */fish) SHELL_RC="$HOME/.config/fish/config.fish" ;;
    *) SHELL_RC="your shell configuration file" ;;
esac

# Check if PATH already contains PREFIX/bin
if ! echo "$PATH" | grep -q "$PREFIX/bin"; then
    echo "You can add it to your PATH by running:"
    if echo "$SHELL" | grep -q "fish"; then
        echo "  echo 'set -gx PATH \"$PREFIX/bin\" \$PATH' >> $SHELL_RC"
    else
        echo "  echo 'export PATH=\"$PREFIX/bin:\$PATH\"' >> $SHELL_RC"
    fi
    echo "Then restart your shell or run:"
    echo "  source $SHELL_RC"
fi

echo ""
echo "Happy coding with SUB-LANG!"
