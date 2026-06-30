#!/usr/bin/env sh
set -eu

RED="$(printf '\033[1;31m')"
GREEN="$(printf '\033[1;32m')"
YELLOW="$(printf '\033[1;33m')"
BOLD="$(printf '\033[1m')"
NC="$(printf '\033[0m')"

log_info()  { printf "%s%s%s\n" "$BOLD$GREEN" "$1" "$NC"; }
log_warn()  { printf "%s%s%s\n" "$BOLD$YELLOW" "$1" "$NC"; }
log_error() { printf "%s%s%s\n" "$BOLD$RED" "$1" "$NC" >&2; }

# ============================================================
# Root check
# ============================================================
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        exec sudo "$0" "$@"
    fi
    log_error "Root required (sudo not found)."
    exit 1
fi

# ============================================================
# Flags
#   --purge  : also remove config, data (DB), logs and user
# ============================================================
PURGE=0
[ "${1:-}" = "--purge" ] && PURGE=1

# ============================================================
# Paths (must match install.sh)
# ============================================================
CONFIG_DIR="/etc/tcpserver"
DATA_DIR="/var/lib/tcpserver"
LOG_DIR="/var/log/tcpserver"
BIN_DIR="/usr/bin/tcpserver"
SERVICE_FILE="/etc/systemd/system/tcpserver.service"

# ============================================================
# Stop & disable service
# ============================================================
if command -v systemctl >/dev/null 2>&1; then
    if systemctl list-unit-files | grep -q '^tcpserver\.service'; then
        log_info "Stopping service..."
        systemctl disable --now tcpserver.service 2>/dev/null || true
    fi
    if [ -f "$SERVICE_FILE" ]; then
        rm -f "$SERVICE_FILE"
        systemctl daemon-reload
        systemctl reset-failed tcpserver.service 2>/dev/null || true
        log_info "Service unit removed."
    fi
fi

# ============================================================
# Binaries (always removed)
# ============================================================
if [ -d "$BIN_DIR" ]; then
    log_info "Removing binaries..."
    rm -rf "$BIN_DIR"
fi

# ============================================================
# Data / config / logs / user
# ============================================================
if [ "$PURGE" -eq 1 ]; then
    log_warn "PURGE: removing config, data, logs and user."
    rm -rf "$CONFIG_DIR" "$DATA_DIR" "$LOG_DIR"

    if getent passwd tcpserver >/dev/null 2>&1; then
        userdel tcpserver 2>/dev/null || true
        log_info "User 'tcpserver' removed."
    fi
else
    log_warn "Kept (use --purge to remove):"
    printf "  %s\n  %s\n  %s\n  user: tcpserver\n" \
        "$CONFIG_DIR" "$DATA_DIR" "$LOG_DIR"
fi

log_info "Uninstall completed."
