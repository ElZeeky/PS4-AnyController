#!/usr/bin/env python3
"""List defined global function/object symbols of an ELF shared object.

Used by ps4/build_payload.sh to synthesise *stub* shared libraries whose SONAME
is the Sony SPRX name (libScePad.sprx, ...). The ps4-payload-sdk CRT resolves a
payload's DT_NEEDED entries by those .sprx names, so a payload linked against
OpenOrbis's .so stubs never loads and never reaches main.
"""
import struct, sys

def main(path, asm=False):
    data = open(path, 'rb').read()
    if data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
        sys.stderr.write("not a 64-bit little-endian ELF: %s\n" % path)
        return 1
    e_shoff, = struct.unpack_from('<Q', data, 40)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', data, 58)
    shs = [struct.unpack_from('<IIQQQQIIQQ', data, e_shoff + i * e_shentsize)
           for i in range(e_shnum)]
    names = [s[0] for s in shs]
    def nameof(i):
        n = shs[i][0]; base = shs[e_shstrndx][4]
        end = data.index(b'\0', base + n); return data[base + n:end].decode()
    # section index -> name
    secname = {nameof(i): i for i in range(e_shnum)}
    out = set()
    for sec in ('.dynsym', '.symtab'):
        if sec not in secname:
            continue
        s = shs[secname[sec]]
        entsize = s[9] or 24
        strtab = shs[s[6]][4]
        for off in range(s[4], s[4] + s[5], entsize):
            st_name, st_info, st_other, st_shndx, st_value, st_size = \
                struct.unpack_from('<IBBHQQ', data, off)
            if st_shndx == 0 or st_name == 0:
                continue
            bind = st_info >> 4
            if bind not in (1, 2):  # GLOBAL, WEAK
                continue
            end = data.index(b'\0', strtab + st_name)
            nm = data[strtab + st_name:end].decode('latin1')
            # keep C identifiers only; those are what our asm stub can define
            if nm and all(c.isalnum() or c in '_.' for c in nm):
                out.add(nm)
    if asm:
        # One label per exported symbol: enough for a stub that exists only so
        # the linker records the right SONAME as DT_NEEDED.
        print("\t.text")
        for nm in sorted(out):
            print("\t.global %s" % nm)
            print("%s:" % nm)
    else:
        for nm in sorted(out):
            print(nm)
    return 0

if __name__ == '__main__':
    args = sys.argv[1:]
    asm = False
    if args and args[0] == '--asm':
        asm = True
        args = args[1:]
    if not args:
        sys.stderr.write("usage: elf_exports.py [--asm] FILE\n")
        sys.exit(2)
    sys.exit(main(args[0], asm))
