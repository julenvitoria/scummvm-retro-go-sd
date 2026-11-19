#!/usr/bin/env python3
"""nhfilms.py GAMEDIR -- The Neverhood's films made into GAMEDIR/neverhood.gwf, once, on a PC.

The Neverhood plays a Smacker film in most rooms (every walk between two places is one). The
console cannot decode Smacker: the Huffman trees of one film take up to 506 KB beside its frame
(their symbols are 16 bits wide), more than the game has to spare. neverhood.gwf holds every
film of the game's .BLB files re-coded in a form made for the console (.gws, below): the same
4x4 blocks Smacker draws, with Huffman tables of byte symbols -- 40 KB to decode instead of
half a megabyte, at about Smacker's own size on the card.

Needs ffmpeg (on the PATH) and numpy. Takes several minutes.

neverhood.gwf (little-endian):
  "NHFV", u32 version (1), u32 films,
  per film, sorted by hash: u32 hash, u32 offset of its film in the file, u32 its size,
  then the films one after another.

A film (.gws):
  "GWS1", u16 width, u16 height, u16 frames, u16 reserved, u32 microseconds per frame,
  u32 audio rate (0: none), u32 reserved,
  the Huffman code lengths of its 5 tables (canonical codes, at most 12 bits; 0: unused):
    TYPE 64 bytes, FILL 256, COL 256, MASK 256, FULL 256
  per frame: u32 bytes that follow, u8 flags (1: a palette follows), [768 bytes RGB],
    u16 audio samples, [s16 mono samples], then a bit stream (codes MSB first) over the
    frame's 4x4 blocks in rows from the top: a TYPE symbol t<<4 | (n-1) says the next n blocks
    are of type t: 0 unchanged, 1 one colour (FILL), 2 two colours (COL c0, COL c1, MASK
    low byte, MASK high byte: bit i set = pixel i of the block, row by row, is c1),
    3 sixteen pixels (FULL x16).
"""
import heapq, multiprocessing, os, shutil, struct, subprocess, sys, tempfile
import numpy as np

MAXLEN = 12
TYPE, FILL, COL, MASK, FULL = range(5)
SIZES = (64, 256, 256, 256, 256)


def blb_entries(path):
    """(hash, type, compression, timestamp, offset, size on disk) of each file in a .BLB"""
    with open(path, 'rb') as f:
        id1, id2, _ext, _size, n = struct.unpack('<IHHII', f.read(16))
        if id1 != 0x2004940 or id2 != 7:
            raise SystemExit('%s: not a Neverhood .BLB' % path)
        hashes = struct.unpack('<%dI' % n, f.read(4 * n))
        for h in hashes:
            t, c, _eo, ts, off, ds, _sz = struct.unpack('<BBHIIII', f.read(20))
            yield h, t, c, ts, off, ds


def films(gamedir):
    """hash -> (blb, offset, size): as the game picks them (the newest of a hash wins)"""
    found = {}
    for name in sorted(os.listdir(gamedir)):
        if not name.lower().endswith('.blb'):
            continue
        path = os.path.join(gamedir, name)
        for h, t, c, ts, off, ds in blb_entries(path):
            if t != 10:
                continue
            if c != 1:
                raise SystemExit('%s: film %08X is compressed (%d)' % (name, h, c))
            if h not in found or ts > found[h][0]:
                found[h] = (ts, path, off, ds)
    return {h: v[1:] for h, v in found.items()}


def probe(path):
    out = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                          'stream=width,height,r_frame_rate', '-of', 'csv=p=0', path],
                         capture_output=True, text=True, check=True).stdout.strip().split(',')
    num, den = (int(x) for x in out[2].split('/'))
    a = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries',
                        'stream=sample_rate', '-of', 'csv=p=0', path],
                       capture_output=True, text=True).stdout.strip()
    return int(out[0]), int(out[1]), num, den, int(a) if a else 0


def huffman_lengths(freq):
    """code lengths (at most MAXLEN) for the symbols' counts"""
    freq = np.asarray(freq, dtype=np.int64)
    used = np.flatnonzero(freq)
    lens = np.zeros(len(freq), dtype=np.int64)
    if len(used) == 0:
        return lens
    if len(used) == 1:
        lens[used[0]] = 1
        return lens
    f = freq.copy()
    while True:
        heap = [(int(f[s]), int(s), (int(s),)) for s in used]
        heapq.heapify(heap)
        depth = {int(s): 0 for s in used}
        k = len(freq)
        while len(heap) > 1:
            a, b = heapq.heappop(heap), heapq.heappop(heap)
            for s in a[2] + b[2]:
                depth[s] += 1
            heapq.heappush(heap, (a[0] + b[0], k, a[2] + b[2]))
            k += 1
        if max(depth.values()) <= MAXLEN:
            for s, d in depth.items():
                lens[s] = d
            return lens
        f = np.where(f > 0, np.maximum(1, f >> 1), 0)	# flatter counts, shorter codes


def canonical(lens):
    """the codes of canonical Huffman code lengths (as the console builds them)"""
    codes = np.zeros(len(lens), dtype=np.int64)
    code = 0
    for ln in range(1, MAXLEN + 1):
        for s in np.flatnonzero(lens == ln):
            codes[s] = code
            code += 1
        code <<= 1
    return codes


def frame_symbols(prev, cur, w, h):
    """(table, symbol) arrays of one frame, in stream order"""
    bw, bh = w // 4, h // 4
    B = cur.reshape(bh, 4, bw, 4).transpose(0, 2, 1, 3).reshape(-1, 16)
    P = prev.reshape(bh, 4, bw, 4).transpose(0, 2, 1, 3).reshape(-1, 16)
    nb = len(B)
    same = (B == P).all(1)
    srt = np.sort(B, 1)
    nun = 1 + (np.diff(srt.astype(np.int16), axis=1) != 0).sum(1)
    types = np.where(same, 0, np.where(nun == 1, 1, np.where(nun == 2, 2, 3)))
    # runs of one type, at most 16 blocks
    start = np.flatnonzero(np.concatenate(([True], types[1:] != types[:-1])))
    ends = np.concatenate((start[1:], [nb]))
    pieces = (ends - start + 15) // 16
    first = np.repeat(start, pieces)
    k = np.arange(len(first)) - np.repeat(np.cumsum(pieces) - pieces, pieces)
    rs = first + 16 * k
    rl = np.minimum(16, np.repeat(ends, pieces) - rs)
    keys, tabs, syms = [], [], []
    # the run symbols, before their first block's data
    keys.append(rs * 32)
    tabs.append(np.full(len(rs), TYPE))
    syms.append(types[rs] * 16 + rl - 1)
    idx = np.arange(nb)
    f = types == 1
    keys.append(idx[f] * 32 + 1)
    tabs.append(np.full(f.sum(), FILL))
    syms.append(B[f, 0])
    m = types == 2
    if m.any():
        lo, hi = srt[m, 0], srt[m, 15]
        mk = ((B[m] == hi[:, None]) * (1 << np.arange(16))).sum(1)
        i = idx[m] * 32
        keys += [i + 1, i + 2, i + 3, i + 4]
        tabs += [np.full(m.sum(), COL)] * 2 + [np.full(m.sum(), MASK)] * 2
        syms += [lo, hi, mk & 255, mk >> 8]
    u = types == 3
    if u.any():
        i = idx[u] * 32
        keys.append((i[:, None] + 1 + np.arange(16)).reshape(-1))
        tabs.append(np.full(u.sum() * 16, FULL))
        syms.append(B[u].reshape(-1))
    keys = np.concatenate(keys)
    order = np.argsort(keys, kind='stable')
    return np.concatenate(tabs)[order].astype(np.int64), np.concatenate(syms)[order].astype(np.int64)


def pack_bits(codes, lens):
    """the codes, MSB first, into bytes (the last byte padded with zeros)"""
    if len(codes) == 0:
        return b''
    shifts = np.arange(MAXLEN - 1, -1, -1)
    bits = (codes[:, None] << (MAXLEN - lens[:, None])) >> shifts & 1
    keep = np.arange(MAXLEN)[None, :] < lens[:, None]
    return np.packbits(bits[keep].astype(np.uint8)).tobytes()


def convert(job):
    h, blb, off, size, work = job
    smk = os.path.join(work, '%08X.smk' % h)
    with open(blb, 'rb') as f:
        f.seek(off)
        data = f.read(size)
    with open(smk, 'wb') as f:
        f.write(data)
    frames_hdr = struct.unpack('<I', data[12:16])[0]
    try:
        w, ht, num, den, rate = probe(smk)
        us = round(1e6 * den / num)
        vid = subprocess.run(['ffmpeg', '-v', 'error', '-i', smk, '-f', 'rawvideo', '-pix_fmt', 'pal8', '-'],
                             capture_output=True, check=True).stdout
        samples = np.zeros(0, dtype='<i2')
        if rate:
            samples = np.frombuffer(subprocess.run(
                ['ffmpeg', '-v', 'error', '-i', smk, '-vn', '-f', 's16le', '-ac', '1', '-'],
                capture_output=True, check=True).stdout, dtype='<i2')
    finally:
        os.remove(smk)
    if w % 4 or ht % 4:
        raise SystemExit('film %08X: %dx%d, not in 4x4 blocks' % (h, w, ht))
    fsz = w * ht + 1024
    n = len(vid) // fsz
    # first pass: each frame's symbols, and how often each comes
    prev = np.zeros(w * ht, dtype=np.uint8)
    frames = []
    freq = [np.zeros(s, dtype=np.int64) for s in SIZES]
    for k in range(n):
        cur = np.frombuffer(vid[k * fsz:k * fsz + w * ht], dtype=np.uint8)
        t, s = frame_symbols(prev, cur, w, ht)
        for tb in range(5):
            freq[tb] += np.bincount(s[t == tb], minlength=SIZES[tb])
        frames.append((t, s))
        prev = cur
    lens = [huffman_lengths(fr) for fr in freq]
    codes = [canonical(ln) for ln in lens]
    lens_all, codes_all = np.concatenate(lens), np.concatenate(codes)
    base = np.cumsum((0,) + SIZES[:-1])
    out = bytearray(b'GWS1' + struct.pack('<HHHHIII', w, ht, n, 0, us, rate, 0))
    out += bytes(np.concatenate(lens).astype(np.uint8))
    prevpal = None
    for k, (t, s) in enumerate(frames):
        pal = np.frombuffer(vid[k * fsz + w * ht:(k + 1) * fsz], dtype=np.uint8).reshape(256, 4)[:, [2, 1, 0]].tobytes()
        body = bytearray([1 if pal != prevpal else 0])
        if pal != prevpal:
            body += pal
        a0 = round(k * us * rate / 1e6) if rate else 0
        a1 = round((k + 1) * us * rate / 1e6) if rate else 0
        if k == n - 1:
            a1 = max(a1, len(samples))	# (the sound's tail with the last frame)
        chunk = samples[a0:a1]
        body += struct.pack('<H', len(chunk)) + chunk.astype('<i2').tobytes()
        g = base[t] + s
        body += pack_bits(codes_all[g], lens_all[g])
        out += struct.pack('<I', len(body)) + body
        prevpal = pal
    warn = '' if n == frames_hdr else ' (%d frames, the Smacker header says %d)' % (n, frames_hdr)
    return h, bytes(out), warn


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    gamedir = sys.argv[1]
    todo = films(gamedir)
    if not todo:
        raise SystemExit('%s: no films found (the .BLB files go in this folder)' % gamedir)
    dst = os.path.join(gamedir, 'neverhood.gwf')
    work = tempfile.mkdtemp(prefix='nhfilms-', dir=gamedir)
    hashes = sorted(todo)
    try:
        with open(dst + '.tmp', 'wb') as f:
            f.write(b'NHFV' + struct.pack('<II', 1, len(hashes)) + b'\0' * (12 * len(hashes)))
            index = {}
            jobs = [(h,) + todo[h] + (work,) for h in hashes]
            with multiprocessing.Pool() as pool:
                for k, (h, film, warn) in enumerate(pool.imap_unordered(convert, jobs)):
                    index[h] = (f.tell(), len(film))
                    f.write(film)
                    print('\r%d/%d films' % (k + 1, len(jobs)), end='', flush=True)
                    if warn:
                        print('\n%08X%s' % (h, warn))
            f.seek(12)
            for h in hashes:
                f.write(struct.pack('<III', h, *index[h]))
        os.replace(dst + '.tmp', dst)
        print('\n%s: %d films, %d MB' % (dst, len(hashes), os.path.getsize(dst) >> 20))
    finally:
        shutil.rmtree(work, ignore_errors=True)
        if os.path.exists(dst + '.tmp'):
            os.remove(dst + '.tmp')


if __name__ == '__main__':
    main()
