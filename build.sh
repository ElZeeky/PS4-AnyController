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
#   ./build.sh cli-linux   cross-compile the static Linux inspector
#   ./build.sh all         test + cli
#   ./build.sh clean       remove build output
#   ./build.sh payload     cross compile the PS4 payload ELF
#                          `payload --check` compiles only, so it works without
#                          the ps4-payload-sdk installed
#                          `payload dump` builds the firmware dumper instead
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
    MINGW*|MSYS*|CYGWIN*) extra+=("-lsetupapi" "-lhid") ;;
    Linux*) extra+=("-D_POSIX_C_SOURCE=200809L") ;;
  esac
  # Without -DXCP_HAVE_HIDAPI the CLI still runs its replay/script modes.
  if [ "${XCP_HAVE_HIDAPI:-0}" = "1" ]; then
    extra+=("-DXCP_HAVE_HIDAPI=1" "-lhidapi")
  fi
  mapfile -t src < <(portable_src)
  "${CC_CMD[@]}" "${STD[@]}" "${WARN[@]}" "${INCLUDES[@]}" "${extra[@]}" \
    -o "$BUILD/AnyController-Inspector" \
    "${src[@]}" "${hid_src[@]}" "${cli_src[@]}"
}

# Cross-compiled, static Linux build of the same inspector. Zig is the only
# compiler here that can target Linux from this Windows checkout.
cmd_cli_linux() {
  local src
  local out="$BUILD/AnyController-Inspector-linux-x86_64"
  local zig

  if [ -x "$ROOT/.tools/zig/zig.exe" ]; then
    zig="$ROOT/.tools/zig/zig.exe"
  elif [ -x "$ROOT/.tools/zig/zig" ]; then
    zig="$ROOT/.tools/zig/zig"
  elif command -v zig >/dev/null 2>&1; then
    zig="$(command -v zig)"
  else
    die "zig is required to build the Linux inspector"
  fi

  say "cross-compiling Linux inspector with: $zig cc"
  mkdir -p "$BUILD"
  mapfile -t src < <(portable_src)
  "$zig" cc "${STD[@]}" "${WARN[@]}" "${INCLUDES[@]}" \
    -D_POSIX_C_SOURCE=200809L -target x86_64-linux-musl -static \
    -o "$out" \
    "${src[@]}" "$ROOT"/usb/src/usb_hidapi*.c "$ROOT"/host/*.c
  [ -f "$out" ] || die "Linux inspector was not produced"
  say "Linux inspector: $out"
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
  local elf="$BUILD/ps4/AnyController.elf"
  local dumper="$BUILD/ps4/AnyController-dumper.elf"
  local cli="$BUILD/AnyController-Inspector"
  local lin="$BUILD/AnyController-Inspector-linux-x86_64"
  local tool_doc="$ROOT/packaging/INSPECTOR.txt"
  local compat="$ROOT/docs/COMPATIBILITY.md"
  local report="$ROOT/RELEASE-REPORT.md"
  local cli_name="AnyController-Inspector"
  case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) cli_name="AnyController-Inspector.exe" ;;
  esac
  [ -f "$cli" ] || cli="$BUILD/AnyController-Inspector.exe"
  [ -f "$tool_doc" ] || tool_doc="$ROOT/packaging/PC-TOOL.txt"
  [ -f "$elf" ] || die "no payload yet - run ./build.sh payload first"
  [ -f "$ROOT/packaging/INSTALL.txt" ] || die "packaging/INSTALL.txt missing"
  [ -f "$ROOT/packaging/DUMPER.txt" ] || die "packaging/DUMPER.txt missing"
  [ -f "$ROOT/packaging/INSPECTOR-LINUX.txt" ] || die "packaging/INSPECTOR-LINUX.txt missing"
  [ -f "$ROOT/packaging/setup-linux.sh" ] || die "packaging/setup-linux.sh missing"
  [ -f "$ROOT/packaging/run-linux.sh" ] || die "packaging/run-linux.sh missing"
  [ -f "$tool_doc" ] || die "packaging/INSPECTOR.txt (or PC-TOOL.txt) missing"
  [ -f "$compat" ] || die "docs/COMPATIBILITY.md missing"
  [ -f "$report" ] || die "RELEASE-REPORT.md missing"

  if [ ! -f "$dumper" ]; then
    say "dumper not built yet; building it"
    "$ROOT/ps4/build_payload.sh" dump
  fi
  if [ ! -f "$cli" ]; then
    say "PC inspector not built yet; building it"
    cmd_cli
    cli="$BUILD/AnyController-Inspector"
    [ -f "$cli" ] || cli="$BUILD/AnyController-Inspector.exe"
  fi
  [ -f "$cli" ] || die "PC inspector binary not found after build"
  if [ -f "$BUILD/AnyController-Inspector.exe" ]; then
    cli="$BUILD/AnyController-Inspector.exe"
    cli_name="AnyController-Inspector.exe"
  fi

  if [ ! -f "$lin" ]; then
    say "Linux inspector not built yet; building it"
    cmd_cli_linux
  fi

  say "assembling release/"
  rm -rf "$ROOT/release"
  mkdir -p "$ROOT/release"
  cp "$elf"    "$ROOT/release/AnyController.elf"
  cp "$dumper" "$ROOT/release/AnyController-dumper.elf"
  cp "$cli"    "$ROOT/release/$cli_name"
  cp "$lin"    "$ROOT/release/AnyController-Inspector-linux-x86_64"
  cp "$ROOT/packaging/INSTALL.txt" "$ROOT/release/INSTALL.txt"
  cp "$ROOT/packaging/DUMPER.txt"  "$ROOT/release/DUMPER.txt"
  cp "$tool_doc"                   "$ROOT/release/$(basename "$tool_doc")"
  cp "$ROOT/packaging/INSPECTOR-LINUX.txt" "$ROOT/release/INSPECTOR-LINUX.txt"
  cp "$compat"                     "$ROOT/release/COMPATIBILITY.md"
  cp "$report"                     "$ROOT/release/RELEASE-REPORT.md"
  # Keep the Linux helpers LF-only even from a CRLF checkout.
  tr -d '\r' < "$ROOT/packaging/setup-linux.sh" > "$ROOT/release/setup-linux.sh"
  tr -d '\r' < "$ROOT/packaging/run-linux.sh" > "$ROOT/release/run-linux.sh"
  chmod +x "$ROOT/release/AnyController-Inspector-linux-x86_64" \
           "$ROOT/release/setup-linux.sh" "$ROOT/release/run-linux.sh" 2>/dev/null || true

  printf '\n'
  ls -l "$ROOT/release"
  printf '\n  release/ is ready. Drag its contents to the console over FTP;\n'
  printf '  read release/INSTALL.txt for the steps (DUMPER.txt and %s\n' \
         "$(basename "$tool_doc")"
  printf '  are for contributors).\n'
}

case "${1:-test}" in
  test)      cmd_test ;;
  cli)       cmd_cli ;;
  cli-linux) cmd_cli_linux ;;
  all)       cmd_test; cmd_cli ;;
  payload)   cmd_payload "$@" ;;
  package)   cmd_package ;;
  clean)     cmd_clean ;;
  *)         die "unknown target: $1 (try: test, cli, cli-linux, all, payload, package, clean)" ;;
esac
