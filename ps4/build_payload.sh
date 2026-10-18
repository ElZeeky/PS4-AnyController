#!/usr/bin/env bash
#
# ps4/build_payload.sh - cross compile the GoldHEN payload ELF.
#
# Normally invoked as `./build.sh payload`; `./build.sh payload --check` only
# compiles the sources and stops.
#
# No Linux, no make, no installed LLVM
# ------------------------------------
# The payload needs a CRT: the `_start` that calls main(), and the runtime that
# resolves the Sony imports by name in the hijacked system process. That CRT only
# exists in ps4-payload-sdk (GPL-3.0), but its build system is not needed - we
# compile its seven source files ourselves with the Zig that this project already
# uses, and link with Zig's ld.lld and the SDK's own linker script.
#
# Its *system* libraries are not used as-is either. The real Sony symbol names
# come from the OpenOrbis stubs we already have (libScePad.so exports all four
# scePadVirtualDevice* entry points; libkernel.so exports the dl* and
# sceKernelOpen/Read/Close/Write we call), but those stubs carry SONAME
# "libX.so". The CRT resolves DT_NEEDED by Sony SPRX name only, so a payload
# linked straight against them never loads. Each needed library is therefore
# re-stubbed below with SONAME "libX.sprx" (same symbols, link-time only).
#
# printf, memset, memcpy, snprintf... resolve through the libSceLibcInternal.sprx
# stub, so the payload links no libc of its own.
#
# Environment:
#   OO_PS4_TOOLCHAIN  OpenOrbis root       (default .tools/oo/OpenOrbis/PS4Toolchain)
#   PS4_PAYLOAD_SDK   ps4-payload-sdk root (default .tools/ps4-payload-sdk; cloned
#                                          on first use, pinned by third_party/REFS)
#   ZIG               zig binary           (default .tools/zig/zig.exe)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CHECK=0
HELLO=0
DUMP=0
case "${1:-}" in
  --check) CHECK=1 ;;
  hello)   HELLO=1 ;;
  dump)    DUMP=1 ;;
esac

OO="${OO_PS4_TOOLCHAIN:-$ROOT/.tools/oo/OpenOrbis/PS4Toolchain}"
SDK="${PS4_PAYLOAD_SDK:-$ROOT/.tools/ps4-payload-sdk}"
OUT="$ROOT/build/ps4"
OBJ="$OUT/obj-payload"
NAME="AnyController"
if [ "$HELLO" = "1" ]; then
  # Separate object dir: the link globs $OBJ/*.o, so mixing would link two mains.
  OBJ="$OUT/obj-hello"
  NAME="xcp_hello"
fi
if [ "$DUMP" = "1" ]; then
  OBJ="$OUT/obj-dump"
  NAME="xcp_dump"
fi

say() { printf '\033[1m== %s\033[0m\n' "$*"; }
die() { printf '\033[31m%s\033[0m\n' "$*" >&2; exit 1; }

[ -d "$OO/include/orbis" ] || die "OpenOrbis not found at $OO (set OO_PS4_TOOLCHAIN)"

# --- compiler --------------------------------------------------------------

ZIG="${ZIG:-}"
if [ -z "$ZIG" ]; then
  if   [ -x "$ROOT/.tools/zig/zig.exe" ]; then ZIG="$ROOT/.tools/zig/zig.exe"
  elif [ -x "$ROOT/.tools/zig/zig" ];     then ZIG="$ROOT/.tools/zig/zig"
  elif command -v zig >/dev/null 2>&1;    then ZIG="$(command -v zig)"
  fi
fi
[ -n "$ZIG" ] || die "no compiler: install zig or set ZIG"

# --- python (turns the OpenOrbis stub libs into SPRX-named stubs) ----------

try_py() { "$1" -c "import sys" >/dev/null 2>&1; }
PYTHON="${PYTHON:-}"
if [ -z "$PYTHON" ]; then
  for cand in python3 python py; do
    if command -v "$cand" >/dev/null 2>&1 && try_py "$cand"; then
      PYTHON="$cand"; break
    fi
  done
fi
[ -n "$PYTHON" ] || die "no working python found (needed to build the SPRX stubs)"

# --- sources ---------------------------------------------------------------
#
# The payload reuses the whole decoder and the console USB backend unchanged.
# router/ has no meaning here: the router is the PC inspector's player-1 policy,
# for a device the console now owns outright.

if [ "$HELLO" = "1" ]; then
  # The loader self-test: one file, no core, no backend, nothing of ours.
  PAYLOAD_SRC=("$ROOT/ps4/hello/hello.c")
  LIBS=(libkernel)
elif [ "$DUMP" = "1" ]; then
  # The module dumper: loads the system modules and copies them to /data.
  PAYLOAD_SRC=("$ROOT/ps4/dumper/dump.c")
  LIBS=(libkernel libSceLibcInternal libScePad libSceMbus)
else
  PAYLOAD_SRC=(
    "$ROOT"/core/src/*.c
    "$ROOT/usb/src/usb_common.c"
    "$ROOT/ps4/src/usb_sceusbd.c"
    "$ROOT/ps4/src/orbis_glue.c"
    "$ROOT"/ps4/payload/*.c
  )
  LIBS=(libkernel libSceLibcInternal libScePad libSceUsbd libSceMbus)
fi

# The SDK's CRT, compiled from source. GPL-3.0: linked in, never copied here.
CRT_SRC=(crt.c klog.c kernel.c rtld.c patch.c mdbg.c nid.c)

INCLUDES=(
  -I"$ROOT/core/include"
  -I"$ROOT/usb/include"
  -I"$ROOT/router/include"
  -I"$ROOT/ps4/include"
)

ORBIS_DEFS=(
  -DORBIS=1 -D__ORBIS__=1 -DPS4=1 -DOO=1
  -D__PS4__=1 -DOOPS4=1 -D__OOPS4__=1
  -D__BSD_VISIBLE=1 -D_BSD_SOURCE=1
)

# Without the GoldHEN SDK, xcp_log.h maps klog to printf.
EXTRA_DEFS=(-DXCP_PAYLOAD=1)

COMMON_CFLAGS=(
  --target=x86_64-freestanding
  -march=btver2 -mtune=btver2 -fshort-wchar -fno-sanitize=all -fPIC -fno-plt
  -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Wno-unused-parameter
  -Wno-macro-redefined
  -isysroot "$OO" -isystem "$OO/include" -isystem "$OO/include/orbis/_types"
)

mkdir -p "$OBJ"

# --- --check: compile the payload sources and stop -------------------------

if [ "$CHECK" = "1" ]; then
  say "compiling payload sources only (no link): $ZIG cc"
  for src in "${PAYLOAD_SRC[@]}"; do
    printf '  CC %s\n' "$(basename "$src")"
    "$ZIG" cc "${COMMON_CFLAGS[@]}" -std=c11 "${INCLUDES[@]}" \
      "${ORBIS_DEFS[@]}" "${EXTRA_DEFS[@]}" -c "$src" -o "$OBJ/$(basename "${src%.c}").o"
  done
  say "payload sources compile clean"
  exit 0
fi

# --- the SDK's CRT (sources only) ------------------------------------------

if [ ! -d "$SDK/crt" ]; then
  say "cloning ps4-payload-sdk (CRT sources only, GPL-3.0)"
  git clone --depth 1 https://github.com/ps4-payload-dev/sdk "$SDK" \
    || die "could not clone the SDK into $SDK"
fi
[ -f "$SDK/crt/crt.c" ]        || die "no CRT sources under $SDK/crt"
[ -f "$SDK/host/elf_x86_64.x" ] || die "no linker script at $SDK/host/elf_x86_64.x"

say "compiling the payload sources and the SDK CRT with: $ZIG cc"

for src in "${PAYLOAD_SRC[@]}"; do
  printf '  CC %s\n' "$(basename "$src")"
  "$ZIG" cc "${COMMON_CFLAGS[@]}" -std=c11 "${INCLUDES[@]}" \
    "${ORBIS_DEFS[@]}" "${EXTRA_DEFS[@]}" -c "$src" -o "$OBJ/$(basename "${src%.c}").o"
done

# In their own directory: the link below globs $OBJ/*.o for the payload objects,
# and crt1.o must not be picked up by that glob as well.
CRT_DIR="$OBJ/crt"
mkdir -p "$CRT_DIR"
CRT_OBJ="$CRT_DIR/crt1.o"
CRT_PARTS=()
# -std=gnu11: syscall.h uses `register ... asm("r10")`, a GNU extension.
for src in "${CRT_SRC[@]}"; do
  printf '  CC %s (crt)\n' "$src"
  "$ZIG" cc --target=x86_64-freestanding -ffreestanding -fno-builtin -nostdlib \
    -fPIC -fno-plt -fno-stack-protector -O1 -std=gnu11 -fshort-wchar \
    -I "$SDK/crt" -I "$SDK/include" -I "$SDK/include/freebsd-9.3" \
    -c "$SDK/crt/$src" -o "$CRT_DIR/${src%.c}.o"
  CRT_PARTS+=("$CRT_DIR/${src%.c}.o")
done
"$ZIG" ld.lld -m elf_x86_64 -r -o "$CRT_OBJ" "${CRT_PARTS[@]}"

# --- SPRX-named stub libraries ---------------------------------------------
#
# This is the difference between a payload that runs and one that does nothing.
# The CRT resolves the payload's DT_NEEDED list by SONAME and only understands
# the Sony SPRX names (libScePad.sprx, libkernel.sprx, ...). Linking straight
# against OpenOrbis's libScePad.so records DT_NEEDED "libScePad.so", which
# sprx_open() cannot find; rtld_dt_needed() then fails, payload_init aborts and
# main is never called - which is exactly "payload ran, nothing happened".
#
# So each needed library gets a stub shared object whose SONAME is the real SPRX
# name and which exports the same symbols OpenOrbis exports. The stub is used
# only by the linker; nothing of it executes on the console.

STUB_DIR="$OUT/sprx-stubs"
mkdir -p "$STUB_DIR"
STUB_ARGS=()
for lib in "${LIBS[@]}"; do
  soname="$lib.sprx"
  src="$OO/lib/$lib.so"
  [ -f "$src" ] || die "OpenOrbis stub missing: $src"
  printf '  STUB %s\n' "$soname"
  "$PYTHON" "$ROOT/tools/elf_exports.py" --asm "$src" > "$STUB_DIR/$soname.s" \
    || die "symbol dump failed for $lib"
  "$ZIG" cc --target=x86_64-freestanding -fno-builtin -fPIC \
    -c "$STUB_DIR/$soname.s" -o "$STUB_DIR/$soname.o" \
    || die "stub assemble failed for $lib"
  "$ZIG" ld.lld -m elf_x86_64 -shared -soname "$soname" \
    -o "$STUB_DIR/$soname" "$STUB_DIR/$soname.o" \
    || die "stub link failed for $lib"
  STUB_ARGS+=("$STUB_DIR/$soname")
done

# --- link ------------------------------------------------------------------
#
# The flags are the SDK's own (its bin/orbis-ld), with the SPRX-named stubs in
# place of its sysroot: -e _start is the CRT's entry point, and max-page-size
# 0x4000 is what the console's loader expects.

say "linking $NAME.elf"
"$ZIG" ld.lld -m elf_x86_64 -pie \
  -T "$SDK/host/elf_x86_64.x" \
  -e _start --eh-frame-hdr -z max-page-size=0x4000 \
  --no-as-needed \
  -o "$OUT/$NAME.elf" \
  "$CRT_OBJ" "$OBJ"/*.o "${STUB_ARGS[@]}"

# --- verify the artifact ---------------------------------------------------
#
# A payload that is not a PIE, or that lost main, is not loadable - and that is
# worth failing the build over rather than discovering on the console.

if command -v node >/dev/null 2>&1 && [ -f "$ROOT/tools/elf_symbols.js" ]; then
  readelf_out="$(node "$ROOT/tools/elf_symbols.js" "$OUT/$NAME.elf")"
  printf '%s\n' "$readelf_out" | grep -q "e_type    : 3" \
    || die "$NAME.elf is not a PIE (e_type != 3)"
  printf '%s\n' "$readelf_out" | grep -q "  DEF  GLOBAL FUNC .*  main$" \
    || die "$NAME.elf does not define main"
  printf '%s\n' "$readelf_out" | grep -q "  DEF  GLOBAL FUNC .*  _start$" \
    || die "$NAME.elf does not define _start"
  say "artifact checks passed (PIE, _start, main)"
fi

# The defect that cost a console round trip: DT_NEEDED must use the SPRX names
# the CRT resolves, not the .so names of the OpenOrbis stubs.
grep -aq "libkernel.sprx" "$OUT/$NAME.elf" \
  || die "$NAME.elf has no SPRX DT_NEEDED; the CRT cannot load it"

ls -l "$OUT/$NAME.elf" | awk '{print "  "$5" bytes"}'
printf '  copy it to the console as /data/payloads/%s.elf and start it from\n' "$NAME"
printf '  GoldHEN -> Utilities -> Payloader LaunchPad, or send it to port 9090.\n'
