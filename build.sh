#!/usr/bin/env bash
#
# build.sh - the single entry point for every build on every platform.
#
# There is no `make` on the Windows/Git-Bash machine this project is developed
# on, so this script is authoritative.
#
# Usage:
#   ./build.sh test        build and run the unit tests        (default)
#   ./build.sh cli         build the PC inspector tool
#   ./build.sh all         test + cli
#   ./build.sh clean       remove build output
#   ./build.sh payload     cross compile the PS4 payload ELF
#                          `payload --check` compiles only, so it works without
#                          the ps4-payload-sdk installed
#   ./build.sh package      assemble release/ ready to drag to the console via FTP
#
# Environment:
#   CC                 C compiler command    (default: .tools/zig/zig.exe cc)
#   XCP_HAVE_HIDAPI=1  link hidapi into the CLI (real hardware on the PC)
#   OO_PS4_TOOLCHAIN   OpenOrbis root        (payload target)
set -euo pipefail

shopt -s nullglob

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

BUILD="$ROOT/build"

# --- pick a compiler ------------------------------------------------------
if [ -n "${CC:-}" ]; then
  # shellcheck disable=SC2206
  CC_CMD=($CC)
elif [ -x "$ROOT/.tools/zig/zig.exe" ]; then
  CC_CMD=("$ROOT/.tools/zig/zig.exe" cc)
elif [ -x "$ROOT/.tools/zig/zig" ]; then
  CC_CMD=("$ROOT/.tools/zig/zig" cc)
elif command -v zig >/dev/null 2>&1; then
  CC_CMD=(zig cc)
elif command -v clang >/dev/null 2>&1; then
  CC_CMD=(clang)
else
  CC_CMD=(cc)
fi

WARN=(-Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)
STD=(-std=c99)

INCLUDES=(
  "-I$ROOT/core/include"
  "-I$ROOT/usb/include"
  "-I$ROOT/router/include"
  "-I$ROOT/mocks/include"
  "-I$ROOT/tests"
)

say() { printf '\033[1m== %s\033[0m\n' "$*"; }
die() { printf '\033[31m%s\033[0m\n' "$*" >&2; exit 1; }

# Sources that never touch a platform: the same files the payload links, plus the
# scripted backend and the scePad mock, which only exist for tests/PC.
portable_src() {
  local files=(
    "$ROOT"/core/src/*.c
    "$ROOT"/usb/src/usb_common.c
    "$ROOT"/usb/src/usb_mock.c
    "$ROOT"/router/src/*.c
    "$ROOT"/mocks/src/*.c
  )
  [ ${#files[@]} -gt 0 ] || die "no portable sources found"
  printf '%s\n' "${files[@]}"
}

cmd_test() {
  local src
  say "compiling tests with: ${CC_CMD[*]}"
  mkdir -p "$BUILD"
  mapfile -t src < <(portable_src)
  "${CC_CMD[@]}" "${STD[@]}" "${WARN[@]}" "${INCLUDES[@]}" \
    -o "$BUILD/xcp_tests" \
    "${src[@]}" \
    "$ROOT"/tests/*.c
  say "running tests"
  "$BUILD/xcp_tests"
}

cmd_cli() {
  local src extra=()
  local cli_src=("$ROOT"/host/*.c)
  # hidapi glue is optional and globbed, so a checkout without it still builds.
  local hid_src=("$ROOT"/usb/src/usb_hidapi*.c)
  [ ${#cli_src[@]} -gt 0 ] || die "no host CLI sources yet (host/*.c)"

  say "compiling PC inspector with: ${CC_CMD[*]}"
  mkdir -p "$BUILD"
  # Windows device enumeration goes through SetupAPI; nowhere else needs it.
  case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) extra+=("-lsetupapi") ;;
  esac
  # Without -DXCP_HAVE_HIDAPI the CLI still runs its replay/script modes.
  if [ "${XCP_HAVE_HIDAPI:-0}" = "1" ]; then
    extra+=("-DXCP_HAVE_HIDAPI=1" "-lhidapi")
  fi
  mapfile -t src < <(portable_src)
  "${CC_CMD[@]}" "${STD[@]}" "${WARN[@]}" "${INCLUDES[@]}" "${extra[@]}" \
    -o "$BUILD/xcp_inspector" \
    "${src[@]}" "${hid_src[@]}" "${cli_src[@]}"
}

cmd_payload() {
  local script="$ROOT/ps4/build_payload.sh"
  [ -x "$script" ] || die "ps4/build_payload.sh not present yet (Phase PS4-3)"
  say "PS4 payload cross build"
  exec "$script" "${@:2}"
}

cmd_clean() {
  say "cleaning"
  rm -rf "$BUILD"
}

# Assemble release/: the payload the user actually uploads, plus the installer
# text and an AutoRun example. This is the thing attached to a GitHub Release -
# it is generated, so it lives outside version control (see .gitignore).
cmd_package() {
  local elf="$BUILD/ps4/xcp_payload.elf"
  [ -f "$elf" ] || die "no payload yet - run ./build.sh payload first"
  [ -f "$ROOT/packaging/INSTALL.txt" ] || die "packaging/INSTALL.txt missing"

  say "assembling release/"
  rm -rf "$ROOT/release"
  mkdir -p "$ROOT/release"
  cp "$elf" "$ROOT/release/xcp_payload.elf"
  cp "$ROOT/packaging/INSTALL.txt"  "$ROOT/release/INSTALL.txt"
  cp "$ROOT/packaging/payloads.ini" "$ROOT/release/payloads.ini"

  printf '\n'
  ls -l "$ROOT/release"
  printf '\n  release/ is ready. Drag its contents to the console over FTP;\n'
  printf '  read release/INSTALL.txt for the steps.\n'
}

case "${1:-test}" in
  test)  cmd_test ;;
  cli)   cmd_cli ;;
  all)   cmd_test; cmd_cli ;;
  payload) cmd_payload "$@" ;;
  package) cmd_package ;;
  clean) cmd_clean ;;
  *)     die "unknown target: $1 (try: test, cli, all, payload, package, clean)" ;;
esac
