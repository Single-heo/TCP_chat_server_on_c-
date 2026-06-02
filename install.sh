#!/bin/bash

if command -v apt >/dev/null 2>&1; then
    PKG_MGR="apt"
elif command -v dnf >/dev/null 2>&1; then
    PKG_MGR="dnf"
elif command -v pacman >/dev/null 2>&1; then
    PKG_MGR="pacman"
else
    echo "Unsupported package manager"
    exit 1
fi

# Later in the script
case "$PKG_MGR" in
    apt)
        apt update
        apt install -y nginx
        ;;
    dnf)
        dnf update -y
        dnf install -y nginx
        ;;
    pacman)
        pacman -Sy
        pacman -S --noconfirm nginx
        ;;
esac