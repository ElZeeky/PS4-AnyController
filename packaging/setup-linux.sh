#!/bin/sh
# One-time setup: let the logged-in user read /dev/hidraw*.
# This is what lets AnyController-Inspector capture a controller without sudo.
set -e

if [ "$(uname -s)" != "Linux" ]; then
    echo "This script is for Linux." >&2
    exit 1
fi

if [ ! -d /etc/udev/rules.d ]; then
    echo "No /etc/udev/rules.d here; run the inspector with sudo instead." >&2
    exit 1
fi

RULE='KERNEL=="hidraw*", SUBSYSTEM=="hidraw", TAG+="uaccess"'

if [ "$(id -u)" -eq 0 ]; then
    printf '%s\n' "$RULE" > /etc/udev/rules.d/99-anycontroller.rules
    command -v udevadm >/dev/null 2>&1 && udevadm control --reload-rules || true
    command -v udevadm >/dev/null 2>&1 && udevadm trigger || true
else
    if ! command -v sudo >/dev/null 2>&1; then
        echo "sudo is not installed. Run this script as root, or run the" >&2
        echo "inspector itself with sudo." >&2
        exit 1
    fi
    printf '%s\n' "$RULE" | sudo tee /etc/udev/rules.d/99-anycontroller.rules >/dev/null
    sudo udevadm control --reload-rules || true
    sudo udevadm trigger || true
fi

echo
echo "Installed /etc/udev/rules.d/99-anycontroller.rules"
echo "Unplug and replug the controller, then run:"
echo "  ./run-linux.sh"
echo
echo "If /dev/hidraw* is still not readable, run the inspector with sudo."
