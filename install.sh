#!/bin/sh

set -eu

# Re-execute as root if necessary
if [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null 2>&1 || {
        printf '\033[1;31mError: Please, run this script as root!\033[0m\n'
        exit 1
    }

    exec sudo "$0" "$@"
fi

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
    PKG_MGR=apt
elif command -v dnf >/dev/null 2>&1; then
    PKG_MGR=dnf
elif command -v pacman >/dev/null 2>&1; then
    PKG_MGR=pacman
elif command -v apk >/dev/null 2>&1; then
    PKG_MGR=apk
else
    echo "Unsupported package manager"
    exit 1
fi

# Install dependencies
case "$PKG_MGR" in
    apt)
        apt update
        apt install -y cmake g++ pkg-config build-essential

        if [ "$BUILD_SERVER" = "ON" ]; then
            apt install -y libsodium-dev libargon2-dev
        fi
        ;;
    dnf)
        dnf install -y cmake gcc-c++ pkgconf-pkg-config
        dnf groupinstall "Development Tools"

        if [ "$BUILD_SERVER" = "ON" ]; then
            dnf install -y libsodium-devel argon2-devel
        fi
        ;;
    pacman)
        pacman -Sy --noconfirm cmake gcc pkgconf base-devel

        if [ "$BUILD_SERVER" = "ON" ]; then
            pacman -Sy --noconfirm libsodium argon2
        fi
        ;;
    apk)
        apk add cmake g++ pkgconf build-base

        if [ "$BUILD_SERVER" = "ON" ]; then
            apk add libsodium-dev argon2-dev
        fi
        ;;
esac

echo "Configuring project..."

cmake -B build \
    -DBUILD_CLIENT="$BUILD_CLIENT" \
    -DBUILD_SERVER="$BUILD_SERVER"

echo "Building..."

if command -v nproc >/dev/null 2>&1; then
    cmake --build build -j"$(nproc)"
else
    cmake --build build
fi

echo "Build completed."
