#!/bin/sh

set -eu

# ============================
# Safety / environment
# ============================
umask 022

# ============================
# Colors (safe POSIX)
# ============================
RED="$(printf '\033[1;31m')"
GREEN="$(printf '\033[1;32m')"
YELLOW="$(printf '\033[1;33m')"
BOLD="$(printf '\033[1m')"
NC="$(printf '\033[0m')"

log_info()  { printf "%s%s%s\n" "$BOLD$GREEN" "$1" "$NC"; }
log_warn()  { printf "%s%s%s\n" "$BOLD$YELLOW" "$1" "$NC"; }
log_error() { printf "%s%s%s\n" "$BOLD$RED" "$1" "$NC" >&2; }

# ============================
# Root escalation
# ============================
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SCRIPT_PATH="$(readlink -f "$0" 2>/dev/null || printf "%s" "$0")"
        exec sudo "$SCRIPT_PATH" "$@"
    fi

    log_error "Root required (no sudo available)."
    exit 1
fi

# ============================
# Build target
# ============================
TARGET="${1:-both}"

BUILD_CLIENT=OFF
BUILD_SERVER=OFF

case "$TARGET" in
    client) BUILD_CLIENT=ON ;;
    server) BUILD_SERVER=ON ;;
    both)   BUILD_CLIENT=ON; BUILD_SERVER=ON ;;
    *)
        printf "Usage: %s {client|server|both}\n" "$0" >&2
        exit 1
        ;;
esac

# ============================
# Paths (consistent lowercase)
# ============================
CONFIG_DIR="/etc/tcpserver"
DATA_DIR="/var/lib/tcpserver"
LOG_DIR="/var/log/tcpserver"

CONFIG_FILE="$CONFIG_DIR/Config_file.ini"
DB_FILE="$DATA_DIR/credentials.json"
LOG_FILE="$LOG_DIR/log.txt"

# ============================
# Setup directories
# ============================
log_info "Creating system directories..."

mkdir -p "$CONFIG_DIR" "$DATA_DIR" "$LOG_DIR"

if [ ! -f "$LOG_FILE" ]; then
    : > "$LOG_FILE"
else
    log_info "Existing log file detected."
fi

# ============================
# Install config files
# ============================
log_info "Installing configuration files..."

if [ -f "./common/Config_file.ini" ]; then
    # FIX: Only install if the target file does not exist yet
    if [ ! -f "$CONFIG_FILE" ]; then
        install -m 644 "./common/Config_file.ini" "$CONFIG_FILE"
    else
        log_info "Existing configuration file detected. Skipping overwrite."
    fi
else
    log_warn "Missing: common/Config_file.ini"
fi

if [ -f "./DataBase/credentials.json" ]; then
    if [ ! -f "$DB_FILE" ]; then
        install -m 644 "./DataBase/credentials.json" "$DB_FILE"
    else
        log_info "Database already installed."
    fi
else
    log_warn "Missing: DataBase/credentials.json"
fi

printf "\n"
log_info "Config: $CONFIG_FILE"
log_info "DB:     $DB_FILE"
log_info "Log:    $LOG_FILE"
printf "\n"

# ============================
# Package manager detection
# ============================
PKG_MGR=""

if command -v apt-get >/dev/null 2>&1; then
    PKG_MGR="apt"
elif command -v dnf >/dev/null 2>&1; then
    PKG_MGR="dnf"
elif command -v pacman >/dev/null 2>&1; then
    PKG_MGR="pacman"
elif command -v apk >/dev/null 2>&1; then
    PKG_MGR="apk"
else
    log_error "Unsupported package manager."
    exit 1
fi

log_info "Package manager: $PKG_MGR"

# ============================
# Dependencies
# ============================
log_info "Installing dependencies..."

case "$PKG_MGR" in
    apt)
        apt-get update
        apt-get install -y \
            cmake \
            g++ \
            pkg-config \
            build-essential

        if [ "$BUILD_SERVER" = "ON" ]; then
            apt-get install -y \
                libsodium-dev \
                libargon2-dev \
                libsimpleini-dev
        fi
        ;;
    dnf)
        dnf install -y \
            cmake \
            gcc-c++ \
            pkgconf-pkg-config

        dnf groupinstall -y "Development Tools"

        if [ "$BUILD_SERVER" = "ON" ]; then
            dnf install -y \
                libsodium-devel \
                argon2-devel \
                simpleini-devel
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
                argon2 \
                simpleini
        fi
        ;;
    apk)
        apk add \
            cmake \
            g++ \
            pkgconf \
            build-base \
            git

        if [ "$BUILD_SERVER" = "ON" ]; then
            apk add \
                libsodium-dev \
                argon2-dev
            sudo mkdir -p /usr/include/simpleini \
            git clone https://github.com/brofield/simpleini.git \
            cd simpleini \
            cp SimpleIni.h /usr/include/simpleini/
        fi
        ;;
esac

# ============================
# Configure build
# ============================
log_info "Configuring CMake project..."

cmake -S . -B build \
    -DBUILD_CLIENT="$BUILD_CLIENT" \
    -DBUILD_SERVER="$BUILD_SERVER"

# ============================
# Build
# ============================
log_info "Building..."

JOBS=""
if command -v nproc >/dev/null 2>&1; then
    JOBS="-j$(nproc)"
fi

cmake --build build -- $JOBS

# ============================
# Done
# ============================
printf "\n"
log_info "Build completed."

printf "%sArtifacts:%s\n" "$BOLD" "$NC"

[ "$BUILD_SERVER" = "ON" ] && printf "  build/server\n"
[ "$BUILD_CLIENT" = "ON" ] && printf "  build/client\n"

printf "\n"