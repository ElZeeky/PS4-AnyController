#!/bin/sh
# Run the Linux inspector from the folder that contains this script.
set -e

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BIN="$DIR/AnyController-Inspector-linux-x86_64"

if [ ! -f "$BIN" ]; then
    echo "AnyController-Inspector-linux-x86_64 is not next to this script." >&2
    exit 1
fi
if [ ! -x "$BIN" ]; then
    chmod +x "$BIN"
fi

exec "$BIN" "$@"
