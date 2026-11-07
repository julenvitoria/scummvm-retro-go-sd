#!/usr/bin/env python3
"""Append the XIP image and its relocation bitmaps to a packed core .bin.

    xip_pack.py --elf core.elf --core core_packed.bin --build-id 0x... --out final.bin

The ELF is linked with --emit-relocs (ld/scummvm_core.ld): .core_entry/.data are the RAM
image, .xip the image that runs from external flash, linked at XIP_VMA. Every word that holds
an address inside .xip -- in .xip itself or in the RAM image -- must move by
(where the firmware cached the image - XIP_VMA); this tool finds them and writes one bit per
word into two bitmaps, read by src/boot.c:

  * R_ARM_ABS32 / R_ARM_TARGET1 relocations whose value (as linked) lies inside .xip,
  * the target words of the linker's long-branch veneers (RAM <-> flash calls).

Anything else crossing between the two images (MOVW/MOVT pairs, PC-relative data) cannot be
relocated this way and stops the build. Layout appended after the packed core:
pad to 4 KB | XIP image | XIP bitmap | RAM bitmap | footer (src/svm_xip.h).
"""
import argparse, struct, sys

R_ARM_ABS32, R_ARM_REL32, R_ARM_TARGET1, R_ARM_PREL31 = 2, 3, 38, 42
MOVW_MOVT = {43, 44, 45, 46, 47, 48, 49, 50}
MAGIC = 0x50495853


class Elf:
    def __init__(self, data):
        self.d = data
        assert data[:4] == b'\x7fELF' and data[4] == 1 and data[5] == 1, 'need ELF32 little endian'
        (self.shoff,) = struct.unpack_from('<I', data, 0x20)
        self.shentsize, self.shnum, self.shstrndx = struct.unpack_from('<HHH', data, 0x2E)
        self.sh = []
        for i in range(self.shnum):
            f = struct.unpack_from('<IIIIIIIIII', data, self.shoff + i * self.shentsize)
            self.sh.append(dict(name_off=f[0], type=f[1], flags=f[2], addr=f[3], off=f[4], size=f[5],
                                link=f[6], info=f[7], entsize=f[9]))
        strtab = self.sh[self.shstrndx]
        for s in self.sh:
            s['name'] = self.cstr(strtab['off'] + s['name_off'])

    def cstr(self, off):
        return self.d[off:self.d.index(b'\0', off)].decode()

    def section(self, name):
        for i, s in enumerate(self.sh):
            if s['name'] == name:
                return i, s
        return None, None

    def symbols(self):
        for s in self.sh:
            if s['type'] == 2:  # SHT_SYMTAB
                strs = self.sh[s['link']]
                for k in range(s['size'] // 16):
                    name, value, size, info, other, shndx = struct.unpack_from('<IIIBBH', self.d, s['off'] + k * 16)
                    yield self.cstr(strs['off'] + name), value, shndx

    def rels(self, target_index):
        for s in self.sh:
            if s['type'] == 9 and s['info'] == target_index:  # SHT_REL
                for k in range(s['size'] // 8):
                    off, info = struct.unpack_from('<II', self.d, s['off'] + k * 8)
                    yield off, info & 0xFF


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--elf', required=True)
    ap.add_argument('--core', required=True)
    ap.add_argument('--build-id', required=True, type=lambda s: int(s, 0))
    ap.add_argument('--out', required=True)
    a = ap.parse_args()

    elf = Elf(open(a.elf, 'rb').read())
    syms = {n: v for n, v, _ in elf.symbols()}
    xip_lo, xip_hi = syms['__xip_start'], syms['__xip_end']
    ram_lo, ram_hi = syms['__RAM_EMU_START__'], syms['__CORE_CODE_END__']
    xi, xs = elf.section('.xip')
    image = bytearray(elf.d[xs['off']:xs['off'] + xs['size']])
    assert xs['addr'] == xip_lo and len(image) == xip_hi - xip_lo, 'unexpected .xip layout'

    # the RAM image as it will sit in memory, to read linked values out of it
    ram = bytearray(ram_hi - ram_lo)
    ram_sections = []
    for name in ('.core_entry', '.data'):
        i, s = elf.section(name)
        ram[s['addr'] - ram_lo:s['addr'] - ram_lo + s['size']] = elf.d[s['off']:s['off'] + s['size']]
        ram_sections.append((i, s))

    def word_at(addr):
        if xip_lo <= addr < xip_hi:
            return struct.unpack_from('<I', image, addr - xip_lo)[0]
        if ram_lo <= addr < ram_hi:
            return struct.unpack_from('<I', ram, addr - ram_lo)[0]
        return None

    in_xip = lambda v: v is not None and xip_lo <= (v & ~1) < xip_hi
    xip_bits = bytearray((len(image) // 4 + 7) // 8)
    ram_bits = bytearray(((ram_hi - ram_lo) // 4 + 7) // 8)
    counts = {'xip': 0, 'ram': 0, 'veneer': 0}
    errors = []

    def mark(addr, kind):
        assert addr % 4 == 0, 'unaligned address word at 0x%08x' % addr
        if xip_lo <= addr < xip_hi:
            w = (addr - xip_lo) // 4
            if not xip_bits[w >> 3] & (1 << (w & 7)):
                xip_bits[w >> 3] |= 1 << (w & 7); counts['xip'] += 1
        else:
            w = (addr - ram_lo) // 4
            if not ram_bits[w >> 3] & (1 << (w & 7)):
                ram_bits[w >> 3] |= 1 << (w & 7); counts['ram'] += 1
        if kind == 'veneer':
            counts['veneer'] += 1

    for idx, sec in ram_sections + [(xi, xs)]:
        lo, hi = sec['addr'], sec['addr'] + sec['size']
        for off, typ in elf.rels(idx):
            if not lo <= off < hi:
                continue
            if typ in (R_ARM_ABS32, R_ARM_TARGET1):
                if in_xip(word_at(off)):
                    mark(off, 'abs')
            elif typ in MOVW_MOVT:
                errors.append('MOVW/MOVT at 0x%08x (type %d): compile without -mpure-code/-mslow-flash-data' % (off, typ))
            elif typ in (R_ARM_REL32, R_ARM_PREL31):
                v = word_at(off)
                tgt = (off + v) & 0xFFFFFFFF if typ == R_ARM_REL32 else None
                if tgt is not None and in_xip(tgt) != (xip_lo <= off < xip_hi):
                    errors.append('PC-relative data across images at 0x%08x' % off)

    # long-branch veneers: "ldr.w pc, [pc, #-0]" + address word
    for name, value, _ in elf.symbols():
        if not name.endswith('_veneer'):
            continue
        addr = value & ~1
        head = word_at(addr)
        if head == 0xF000F85F:
            if in_xip(word_at(addr + 4)):
                mark(addr + 4, 'veneer')
        elif head is not None:
            errors.append('unknown veneer form %s at 0x%08x: %08x' % (name, addr, head))

    if errors:
        for e in errors[:20]:
            print('xip_pack: ' + e, file=sys.stderr)
        sys.exit(1)

    core = bytearray(open(a.core, 'rb').read())
    core += b'\0' * (-len(core) % 4096)
    xip_off = len(core)
    core += image
    core += b'\0' * (-len(core) % 4)
    xbits_off = len(core)
    core += xip_bits
    core += b'\0' * (-len(core) % 4)
    rbits_off = len(core)
    core += ram_bits
    core += b'\0' * (-len(core) % 4)
    core += struct.pack('<8I', MAGIC, a.build_id, xip_lo, xip_off, len(image), xbits_off, rbits_off, len(ram_bits))
    open(a.out, 'wb').write(core)
    print('xip_pack: RAM image %d KB, XIP image %d KB, %d + %d relocations (%d veneers) -> %s (%d KB)' % (
        (ram_hi - ram_lo) // 1024, len(image) // 1024, counts['xip'], counts['ram'], counts['veneer'], a.out,
        len(core) // 1024))


if __name__ == '__main__':
    main()
