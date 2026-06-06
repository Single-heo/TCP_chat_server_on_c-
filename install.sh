#!/bin/sh

set -eu

# Colors
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
NC='\033[0m'
BOLD='\033[1m'

log_info() {
    printf "${BOLD}${GREEN}%s${NC}\n" "$1"
}

log_error() {
    printf "${BOLD}${RED}%s${NC}\n" "$1" >&2
}

# Re-execute as root if necessary
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        exec sudo "$0" "$@"
    fi

    log_error "Please run this script as root or install sudo."
    exit 1
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
        printf 'Usage: %s {client|server|both}\n' "$0" >&2
        exit 1
        ;;
esac

log_info "Installing configuration file..."

mkdir -p /etc/TcpServer
mkdir -p /var/lib/tcpserver

if [ -f common/Config_file.ini ]; then
    install -m 644 common/Config_file.ini \
        /etc/TcpServer/Config_file.ini
fi


if [ -f DataBase/credentials.json ] &&
   [ ! -f /var/lib/tcpserver/credentials.json ]; then
    install -m 644 DataBase/credentials.json \
        /var/lib/tcpserver/credentials.json
fi

log_info "Config file location: /etc/TcpServer/Config_file.ini"
sleep 0.4
log_info "Database location:  /var/lib/tcpserver/credentials.json"

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
    log_error "Unsupported package manager."
    exit 1
fi

log_info "Installing dependencies..."

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
        dnf groupinstall -y "Development Tools"

        if [ "$BUILD_SERVER" = "ON" ]; then
            dnf install -y libsodium-devel argon2-devel
        fi
        ;;
    pacman)
        pacman -Syu --noconfirm

        pacman -S --noconfirm \
            cmake \
            gcc \
            pkgconf \
            base-devel

        if [ "$BUILD_SERVER" = "ON" ]; then
            pacman -S --noconfirm \
                libsodium \
                argon2
        fi
        ;;
    apk)
        apk add \
            cmake \
            g++ \
            pkgconf \
            build-base

        if [ "$BUILD_SERVER" = "ON" ]; then
            apk add \
                libsodium-dev \
                argon2-dev
        fi
        ;;
esac

log_info "Configuring project..."

cmake -B build \
    -DBUILD_CLIENT="$BUILD_CLIENT" \
    -DBUILD_SERVER="$BUILD_SERVER"

log_info "Building project..."

if command -v nproc >/dev/null 2>&1; then
    cmake --build build -j"$(nproc)"
else
    cmake --build build
fi

if command -v clear >/dev/null 2>&1; then
    clear
fi

printf "\n"
log_info "Build completed."

printf "${BOLD}Executables are located in:${NC}\n"
printf "  build/\n\n"

if [ "$BUILD_SERVER" = "ON" ]; then
    printf "  ${YELLOW}./server${NC}\n"
fi

if [ "$BUILD_CLIENT" = "ON" ]; then
    printf "  ${YELLOW}./client${NC}\n"
fi