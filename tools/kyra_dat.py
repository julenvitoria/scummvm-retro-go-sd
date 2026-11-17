#!/usr/bin/env python3
"""kyra.dat for the Game & Watch: only what The Legend of Kyrandia 1 (DOS) reads.

ScummVM's kyra.dat (dists/engine-data) is 2 MB with 5,442 entries for six games; the engine keeps
an index of every entry in RAM and checks the whole file's MD5 at each start. This writes a copy
with the INDEX, the id maps of Kyrandia 1 for DOS (every language and edition: floppy, CD, demo)
and the entries those maps name -- the same PAK format, the MD5 appended as the engine expects.

    kyra_dat.py IN OUT
"""
import hashlib, struct, sys

GAME, PLATFORM = 0, 0          # staticres.cpp: iGameTable (kyra1), iPlatformTable (DOS)


def read_pak(d):
    off = struct.unpack('<I', d[:4])[0]
    p, start, ents = 4, off, {}
    while p < off:
        e = d.index(b'\0', p)
        name = d[p:e].decode()
        p = e + 1
        if not name:
            break
        end = struct.unpack('<I', d[p:p + 4])[0]
        p += 4
        ents[name] = d[start:end]
        start = end
    return ents


def write_pak(ents):
    names = list(ents)
    head = sum(4 + len(n) + 1 for n in names) + 4 + 1
    head = (head + 3) & ~3
    out, data, pos = bytearray(), bytearray(), head
    for n in names:
        out += struct.pack('<I', pos) + n.encode() + b'\0'
        data += ents[n]
        pos += len(ents[n])
    out += struct.pack('<I', pos) + b'\0'
    out += b'\0' * (head - len(out))
    out += data
    return bytes(out) + hashlib.md5(out).digest()


def main():
    src, dst = sys.argv[1], sys.argv[2]
    d = open(src, 'rb').read()
    assert hashlib.md5(d[:-16]).digest() == d[-16:], 'not a kyra.dat (MD5)'
    ents = read_pak(d)
    idx = ents['INDEX']
    version, n = struct.unpack('>II', idx[:8])
    defs = [struct.unpack('>H', idx[8 + 2 * i:10 + 2 * i])[0] for i in range(n)]
    keep_defs = [g for g in defs if g >> 12 == GAME and (g >> 8) & 15 == PLATFORM]
    out = {}
    for g in keep_defs:
        name = '0%01X%01X%01X000%01X' % (g >> 12, (g >> 8) & 15, (g >> 4) & 15, g & 15)
        m = ents[name]
        out[name] = m
        count = struct.unpack('>H', m[:2])[0]
        for k in range(count):
            _id, _type, fn = struct.unpack('>HBI', m[2 + 7 * k:9 + 7 * k])
            fname = '%08X' % fn
            out[fname] = ents[fname]
    out['INDEX'] = struct.pack('>II', version, len(keep_defs)) + b''.join(struct.pack('>H', g) for g in keep_defs)
    blob = write_pak(out)
    assert read_pak(blob).keys() == out.keys()
    open(dst, 'wb').write(blob)
    print('kyra.dat: %d of %d entries, %d KB (Kyrandia 1, DOS: %d editions)' %
          (len(out), len(ents), len(blob) // 1024, len(keep_defs)))


if __name__ == '__main__':
    main()
