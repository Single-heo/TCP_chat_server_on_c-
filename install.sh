#!/usr/bin/env sh
set -eu

umask 022

# ============================================================
# Colors / logging
# ============================================================
RED="$(printf '\033[1;31m')"
GREEN="$(printf '\033[1;32m')"
YELLOW="$(printf '\033[1;33m')"
BOLD="$(printf '\033[1m')"
NC="$(printf '\033[0m')"

log_info()  { printf "%s%s%s\n" "$BOLD$GREEN" "$1" "$NC"; }
log_warn()  { printf "%s%s%s\n" "$BOLD$YELLOW" "$1" "$NC"; }
log_error() { printf "%s%s%s\n" "$BOLD$RED" "$1" "$NC" >&2; }

# ============================================================
# Root check (re-exec via sudo only if available)
# ============================================================
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        exec sudo "$0" "$@"
    fi
    log_error "Root required (sudo not found)."
    exit 1
fi
# From here on we are ROOT -> no internal sudo needed.

# ============================================================
# Build target
# ============================================================
TARGET="${1:-both}"
BUILD_CLIENT=OFF
BUILD_SERVER=OFF

case "$TARGET" in
    client) BUILD_CLIENT=ON ;;
    server) BUILD_SERVER=ON ;;
    both)   BUILD_CLIENT=ON; BUILD_SERVER=ON ;;
    *)
        echo "Usage: $0 {client|server|both}" >&2
        exit 1
        ;;
esac

# ============================================================
# systemd requirement (this installer is systemd-only)
# ============================================================
if ! command -v systemctl >/dev/null 2>&1; then
    log_error "systemctl not found. This installer targets systemd systems only."
    exit 1
fi

# ============================================================
# Paths
# ============================================================
CONFIG_DIR="/etc/tcpserver"
DATA_DIR="/var/lib/tcpserver"
LOG_DIR="/var/log/tcpserver"
BIN_DIR="/usr/bin/tcpserver"

CONFIG_FILE="$CONFIG_DIR/Config_file.ini"
SERVICE_FILE="/etc/systemd/system/tcpserver.service"
DB_FILE="$DATA_DIR/credentials.json"
LOG_FILE="$LOG_DIR/log.txt"

# ============================================================
# Package manager detection
# ============================================================
if command -v apt-get >/dev/null 2>&1; then PKG=apt
elif command -v dnf   >/dev/null 2>&1; then PKG=dnf
elif command -v yum   >/dev/null 2>&1; then PKG=yum
elif command -v pacman >/dev/null 2>&1; then PKG=pacman
else
    log_error "No supported package manager (apt/dnf/yum/pacman)."
    exit 1
fi
log_info "Package manager: $PKG"

# =====================================================================
# Dependencies
# SimpleIni: NOT installed via pkg manager (only in Debian sid).
# CMake FetchContent resolves it everywhere -> consistent.
# autoconf/automake/libtool: needed for libsodium source fallback.
# =====================================================================
log_info "Installing dependencies..."
case "$PKG" in
  apt)
    apt-get update
    apt-get install -y \
        build-essential cmake git pkg-config \
        autoconf automake libtool \
        libsodium-dev \
        nlohmann-json3-dev \
        acl
    ;;
  dnf|yum)
    "$PKG" groupinstall -y "Development Tools"
    "$PKG" install -y \
        cmake git pkgconf-pkg-config \
        autoconf automake libtool \
        libsodium-devel \
        json-devel \
        acl
    ;;
  pacman)
    pacman -Sy --needed --noconfirm \
        base-devel cmake git pkgconf \
        autoconf automake libtool \
        libsodium \
        nlohmann-json \
        acl
    ;;
esac

# ============================================================
# Service user (idempotent)
# ============================================================
if ! getent passwd tcpserver >/dev/null 2>&1; then
    log_info "Creating service user 'tcpserver'..."
    useradd --system \
        --home-dir /nonexistent \
        --no-create-home \
        --shell /usr/sbin/nologin \
        tcpserver
fi

# ============================================================
# Directories
# ============================================================
log_info "Creating directories..."
mkdir -p "$CONFIG_DIR" "$DATA_DIR" "$LOG_DIR" "$BIN_DIR"

chown root:tcpserver "$CONFIG_DIR"; chmod 750 "$CONFIG_DIR"
chown tcpserver:tcpserver "$DATA_DIR"; chmod 750 "$DATA_DIR"
chown tcpserver:tcpserver "$LOG_DIR"; chmod 750 "$LOG_DIR"


# Log file
[ -f "$LOG_FILE" ] || touch "$LOG_FILE"
chown tcpserver:tcpserver "$LOG_FILE"
chmod 640 "$LOG_FILE"

# ============================================================
# Build
# ============================================================
log_info "Configuring build..."
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_CLIENT="$BUILD_CLIENT" \
    -DBUILD_SERVER="$BUILD_SERVER"

log_info "Building..."
JOBS="$( (command -v nproc >/dev/null 2>&1 && nproc) || echo 1 )"
cmake --build build --parallel "$JOBS"

# ============================================================
# Install binaries (root-owned)
# ============================================================
log_info "Installing binaries..."
if [ "$BUILD_SERVER" = "ON" ]; then
    [ -f build/server ] || { log_error "Missing build/server"; exit 1; }
    install -m 755 -o root -g root build/server "$BIN_DIR/server"
fi
if [ "$BUILD_CLIENT" = "ON" ]; then
    [ -f build/client ] || { log_error "Missing build/client"; exit 1; }
    install -m 755 -o root -g root build/client "$BIN_DIR/client"
fi

# ============================================================
# Config (root-owned, read-only to service)  [server only]
# ============================================================
if [ "$BUILD_SERVER" = "ON" ]; then
    log_info "Installing config..."
    if [ -f "./common/Config_file.ini" ]; then
        if [ ! -f "$CONFIG_FILE" ]; then
            install -m 640 -o root -g tcpserver ./common/Config_file.ini "$CONFIG_FILE"
        else
            log_warn "Config exists, not overwritten."
        fi
    else
        log_warn "Missing Config_file.ini"
    fi

    # Database (writable by service)
    log_info "Installing database..."
    if [ -f "./DataBase/credentials.json" ]; then
        if [ ! -f "$DB_FILE" ]; then
            install -m 640 -o tcpserver -g tcpserver ./DataBase/credentials.json "$DB_FILE"
        else
            log_warn "Database exists, not overwritten."
        fi
    else
        log_warn "Missing credentials.json"
    fi

    # ACL fallback (best-effort)
    setfacl -m u:tcpserver:rx  "$CONFIG_DIR" 2>/dev/null || true
    setfacl -m u:tcpserver:rwx "$DATA_DIR"   2>/dev/null || true
fi

# ============================================================
# Service (server only)
# ============================================================
if [ "$BUILD_SERVER" = "ON" ]; then
    log_info "Installing service..."
    [ -f "./common/service/tcpserver.service" ] || { log_error "Missing tcpserver.service"; exit 1; }
    install -m 644 -o root -g root ./common/service/tcpserver.service "$SERVICE_FILE"

    systemctl daemon-reload
    systemctl enable --now tcpserver.service

    if systemctl is-active --quiet tcpserver.service; then
        log_info "Service running."
    else
        log_error "Service failed to start. Last logs:"
        journalctl -u tcpserver.service -n 20 --no-pager || true
        exit 1
    fi
else
    log_warn "Client-only build: service not installed."
fi

# ============================================================
# Done
# ============================================================
log_info "Installation completed."
printf "Artifacts:\n"
[ "$BUILD_SERVER" = "ON" ] && printf "  %s/server\n" "$BIN_DIR"
[ "$BUILD_CLIENT" = "ON" ] && printf "  %s/client\n" "$BIN_DIR"
