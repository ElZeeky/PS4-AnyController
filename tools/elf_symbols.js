#!/usr/bin/env node
/*
 * tools/elf_symbols.js - list an ELF's exported and imported symbols.
 *
 * Needed because this machine has no readelf/nm. `file` cannot tell an ELF PRX
 * apart from random data either, so this is how we confirm the plugin actually
 * exports what GoldHEN's loader looks for and still imports what it calls.
 *
 * usage: node tools/elf_symbols.js <file.elf> [--exports] [--imports] [--filter TEXT]
 */
"use strict";

const fs = require("fs");

const SHT_SYMTAB = 2;
const SHT_DYNSYM = 11;
const SHF_ALLOC = 0x2;

/* st_info: bind = high nibble, type = low nibble */
const BINDS = ["LOCAL", "GLOBAL", "WEAK", "3", "4", "5", "6", "7",
               "8", "9", "GNU_UNIQUE"];
const TYPES = ["NOTYPE", "OBJECT", "FUNC", "SECTION", "FILE", "COMMON", "TLS"];

function readSymbols(buf) {
  if (buf.length < 64 || buf[0] !== 0x7f || buf[1] !== 0x45) {
    throw new Error("not an ELF file");
  }
  const is64 = buf[4] === 2;
  const le = buf[5] === 1;
  if (!is64) throw new Error("only 64-bit ELF is supported");
  if (!le) throw new Error("only little-endian ELF is supported");

  const u16 = (o) => buf.readUInt16LE(o);
  const u32 = (o) => buf.readUInt32LE(o);
  const u64 = (o) => Number(buf.readBigUInt64LE(o));

  const eShoff = u64(0x28);
  const eShentsize = u16(0x3a);
  const eShnum = u16(0x3c);
  const eShstrndx = u16(0x3e);

  const sections = [];
  for (let i = 0; i < eShnum; i++) {
    const o = eShoff + i * eShentsize;
    sections.push({
      nameOff: u32(o),
      type: u32(o + 4),
      flags: u64(o + 8),
      offset: u64(o + 0x18),
      size: u64(o + 0x20),
      link: u32(o + 0x28),
      entsize: u64(o + 0x38),
    });
  }

  const strtabOf = (sh) => {
    const s = sections[sh.link];
    return { base: s.offset, size: s.size };
  };
  const readStr = (sec, off) => {
    const start = sec.base + off;
    let end = start;
    while (end < sec.base + sec.size && buf[end] !== 0) end++;
    return buf.toString("utf8", start, end);
  };

  const out = [];
  const shstrtab = sections[eShstrndx];
  const shstr = { base: shstrtab.offset, size: shstrtab.size };

  for (const sh of sections) {
    if (sh.type !== SHT_SYMTAB && sh.type !== SHT_DYNSYM) continue;
    const strsec = strtabOf(sh);
    const count = sh.entsize ? Math.floor(sh.size / sh.entsize) : 0;

    for (let i = 0; i < count; i++) {
      const o = sh.offset + i * sh.entsize;
      const nameOff = u32(o);
      const info = buf[o + 4];
      const shndx = u16(o + 6);
      const value = u64(o + 8);
      const name = readStr(strsec, nameOff);
      if (!name) continue;

      out.push({
        name,
        bind: BINDS[info >> 4] || "?",
        type: TYPES[info & 0xf] || "?",
        section: shndx,
        defined: shndx !== 0,
        value,
        table: sh.type === SHT_DYNSYM ? "dynsym" : "symtab",
      });
    }
  }

  return { symbols: out, machine: u16(0x12), osabi: buf[7], type: u16(0x10) };
}

const args = process.argv.slice(2);
if (args.length === 0) {
  console.error("usage: elf_symbols.js <file> [--exports|--imports] [--filter TEXT]");
  process.exit(2);
}
const path = args[0];
const wantExports = args.includes("--exports");
const wantImports = args.includes("--imports");
const filterIdx = args.indexOf("--filter");
const filter = filterIdx >= 0 ? args[filterIdx + 1] : null;

const { symbols, machine, osabi, type } = readSymbols(fs.readFileSync(path));
console.log(`file      : ${path}`);
console.log(`e_machine : ${machine} (62 = x86-64)`);
console.log(`e_osabi   : ${osabi} (9 = FreeBSD)`);
console.log(`e_type    : ${type} (2 = EXEC, 3 = DYN)`);

const shown = symbols.filter((s) => {
  if (filter && !s.name.includes(filter)) return false;
  if (wantExports) return s.defined;
  if (wantImports) return !s.defined;
  return true;
});

/* Collapse duplicate entries from symtab + dynsym. */
const seen = new Map();
for (const s of shown) {
  const key = `${s.name}|${s.defined}|${s.bind}|${s.type}`;
  if (!seen.has(key)) seen.set(key, s);
}
const list = [...seen.values()];

console.log(`symbols   : ${symbols.length} total, ${list.length} shown`);
console.log("");
for (const s of list) {
  console.log(
    `  ${s.defined ? "DEF" : "UND"}  ${s.bind.padEnd(6)} ${s.type.padEnd(6)} ` +
    `${s.defined ? "0x" + s.value.toString(16).padStart(8, "0") : "        "}  ${s.name}`
  );
}
