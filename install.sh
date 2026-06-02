#!/usr/bin/env bash

set -euo pipefail

TARGET="${1:-both}"

BUILD_CLIENT=OFF
BUILD_SERVER=OFF

case "$TARGET" in
    client)
        BUILD_CLIENT=ON
        ;;
    server)
        BUILD_SERVER=ON
        ;;
    both)
        BUILD_CLIENT=ON
        BUILD_SERVER=ON
        ;;
    *)
        echo "Usage: $0 {client|server|both}"
        exit 1
        ;;
esac

# Detect package manager
if command -v apt >/dev/null 2>&1; then
    PKG_MGR="apt"
elif command -v dnf >/dev/null 2>&1; then
    PKG_MGR="dnf"
elif command -v pacman >/dev/null 2>&1; then
    PKG_MGR="pacman"
elif command -v apk >/dev/null 2>&1; then
    PKG_MGR="apk"
else
    echo "Unsupported package manager"
    exit 1
fi

install_packages() {
    case "$PKG_MGR" in
        apt)
            sudo apt update
            sudo apt install -y "$@"
            ;;
        dnf)
            sudo dnf install -y "$@"
            ;;
        pacman)
            sudo pacman -Sy --noconfirm "$@"
            ;;
        apk)
            sudo apk add "$@"
            ;;
    esac
}

# Package names per distro
case "$PKG_MGR" in
    apt)
        COMMON_PKGS=(cmake g++ pkg-config)
        SERVER_PKGS=(libsodium-dev libargon2-dev)
        ;;
    dnf)
        COMMON_PKGS=(cmake gcc-c++ pkgconf-pkg-config)
        SERVER_PKGS=(libsodium-devel argon2-devel)
        ;;
    pacman)
        COMMON_PKGS=(cmake gcc pkgconf)
        SERVER_PKGS=(libsodium argon2)
        ;;
    apk)
        COMMON_PKGS=(cmake g++ pkgconf)
        SERVER_PKGS=(libsodium-dev argon2-dev)
        ;;
esac

echo "[+] Installing common dependencies..."
install_packages "${COMMON_PKGS[@]}"

if [ "$BUILD_SERVER" = "ON" ]; then
    echo "[+] Installing server dependencies..."
    install_packages "${SERVER_PKGS[@]}"
fi

echo "[+] Configuring CMake..."

cmake -B build \
    -DBUILD_CLIENT="$BUILD_CLIENT" \
    -DBUILD_SERVER="$BUILD_SERVER"

echo "[+] Building..."

cmake --build build -j"$(nproc)"

echo "[+] Build completed"