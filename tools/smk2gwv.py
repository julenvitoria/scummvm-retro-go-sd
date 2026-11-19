#!/usr/bin/env python3
"""smk2gwv.py IN.SMK [OUT.GWV] -- a Smacker film made into the Game & Watch's own film format.

A Smacker film cannot be decoded on the console: its Huffman trees alone take up to 838 KB
(Broken Sword's INTRO.SMK) beside a 256 KB frame, and walking them bit by bit for every 4x4
block is more than the CPU has at 12 frames a second. A .gwv is the same film already decoded,
at the size the panel shows (every other pixel: 640x400 -> 320x200), as the changes from one
frame to the next -- played by a few dozen lines of code (engines/sword1/animation.cpp) that read it from the
card as it goes, into the screen itself, with no memory of their own but one frame's chunk.

Needs ffmpeg and ffprobe (on the PATH) and numpy; it says how to install what is missing.

Format (little-endian):
  header, 32 bytes: "GWV1", u16 width, u16 height, u16 frames, u16 reserved,
                    u32 microseconds per frame, u32 audio rate (0: none), u32 reserved x3
  per frame:        u32 bytes that follow, u8 flags (1: a palette follows),
                    [768 bytes: the palette, RGB],
                    u16 audio samples, [s16 mono samples],
                    the picture: ops over the frame's pixels, row after row from the top:
                      op byte: kind = op >> 6, n = op & 63 (63: n = 63 + the u16 that follows),
                      count = n + 1; kind 0 skips count pixels (unchanged), 1 copies count
                      bytes that follow, 2 repeats the byte that follows count times
"""
import os, shutil, struct, subprocess, sys

HELP = '''
Install it, then open a NEW terminal window (PowerShell, cmd...) so it is found, and try again:
  Windows: winget install Gyan.FFmpeg     (or https://www.gyan.dev/ffmpeg/builds/, with its
           "bin" folder added to the PATH)
  macOS:   brew install ffmpeg
  Linux:   sudo apt install ffmpeg        (or your distribution's package)
  numpy:   python -m pip install numpy
'''
try:
    import numpy as np
except ImportError:
    raise SystemExit('smk2gwv.py needs numpy, which this Python does not have.' + HELP)
for tool in ('ffmpeg', 'ffprobe'):
    if not shutil.which(tool):
        raise SystemExit('smk2gwv.py needs ffmpeg: "%s" was not found on the PATH.' % tool + HELP)


def probe(path):
    out = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                          'stream=width,height,r_frame_rate,nb_frames', '-of', 'csv=p=0', path],
                         capture_output=True, text=True, check=True).stdout.strip().split(',')
    w, h = int(out[0]), int(out[1])
    num, den = (int(x) for x in out[2].split('/'))
    a = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries',
                        'stream=sample_rate', '-of', 'csv=p=0', path],
                       capture_output=True, text=True).stdout.strip()
    return w, h, num, den, int(a) if a else 0


def op(kind, count):
    n = count - 1
    if n < 63:
        return bytes([kind << 6 | n])
    return bytes([kind << 6 | 63]) + struct.pack('<H', n - 63)


def encode(prev, cur):
    """the ops that turn prev into cur (flat uint8 arrays)"""
    out = bytearray()
    changed = prev != cur
    n = len(cur)
    # segments of changed / unchanged pixels; unchanged stretches under 4 are copied over
    edges = np.flatnonzero(np.diff(changed.astype(np.int8))) + 1
    bounds = np.concatenate(([0], edges, [n]))
    segs = [[int(a), int(b), bool(changed[a])] for a, b in zip(bounds[:-1], bounds[1:]) if b > a]
    # an unchanged stretch under 4 pixels between changes is cheaper copied than skipped
    for k in range(1, len(segs) - 1):
        if not segs[k][2] and segs[k][1] - segs[k][0] < 4:
            segs[k][2] = True
    merged = []
    for s_ in segs:
        if merged and merged[-1][2] == s_[2]:
            merged[-1][1] = s_[1]
        else:
            merged.append(s_)
    segs = merged
    for a, b, c in segs:
        if not c:
            while b - a > 0:
                k = min(b - a, 63 + 65536)
                out += op(0, k)
                a += k
            continue
        seg = cur[a:b]
        # runs of 4 or more of one colour inside it; the rest copied
        d = np.flatnonzero(np.diff(seg)) + 1
        rb = np.concatenate(([0], d, [len(seg)]))
        lit = None
        for x, y in zip(rb[:-1], rb[1:]):
            if y - x >= 4:
                if lit is not None:
                    out += emit_lit(seg[lit:x])
                    lit = None
                while y - x > 0:
                    k = min(y - x, 63 + 65536)
                    out += op(2, k) + bytes([seg[x]])
                    x += k
            elif lit is None:
                lit = x
        if lit is not None:
            out += emit_lit(seg[lit:])
    return bytes(out)


def emit_lit(px):
    out = bytearray()
    i = 0
    while i < len(px):
        k = min(len(px) - i, 63 + 65536)
        out += op(1, k) + px[i:i + k].tobytes()
        i += k
    return out


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(src)[0] + '.gwv'
    w, h, num, den, rate = probe(src)
    # half size: every other column; every other row too, unless the film is stored at half
    # height already (Smacker's line-doubled films: 640x200 shown as 640x400)
    ow, oh = (w + 1) // 2, (h + 1) // 2 if h > 240 else h
    rowstep = 2 if h > 240 else 1
    us = round(1e6 * den / num)
    vid = subprocess.Popen(['ffmpeg', '-v', 'error', '-i', src, '-f', 'rawvideo', '-pix_fmt', 'pal8', '-'],
                           stdout=subprocess.PIPE)
    audio = b''
    if rate:
        audio = subprocess.run(['ffmpeg', '-v', 'error', '-i', src, '-vn', '-f', 's16le', '-ac', '1', '-'],
                               capture_output=True, check=True).stdout
    samples = np.frombuffer(audio, dtype='<i2')
    fsz = w * h + 1024
    frames = []
    prev = np.zeros(ow * oh, dtype=np.uint8)
    prevpal = None
    k = 0
    total = 0
    with open(dst + '.tmp', 'wb') as f:
        f.write(b'\0' * 32)
        while True:
            raw = vid.stdout.read(fsz)
            if len(raw) < fsz:
                break
            img = np.frombuffer(raw[:w * h], dtype=np.uint8).reshape(h, w)[::rowstep, ::2]
            cur = np.ascontiguousarray(img).reshape(-1)
            bgra = np.frombuffer(raw[w * h:], dtype=np.uint8).reshape(256, 4)
            pal = bgra[:, [2, 1, 0]].tobytes()
            body = bytearray()
            flags = 0
            if pal != prevpal:
                flags |= 1
            body.append(flags)
            if flags & 1:
                body += pal
            # this frame's share of the sound, by the clock (no drift)
            a0 = round(k * us * rate / 1e6) if rate else 0
            a1 = round((k + 1) * us * rate / 1e6) if rate else 0
            chunk = samples[a0:a1]
            body += struct.pack('<H', len(chunk)) + chunk.astype('<i2').tobytes()
            body += encode(prev, cur)
            f.write(struct.pack('<I', len(body)) + body)
            total += len(body) + 4
            prev, prevpal = cur, pal
            k += 1
        vid.wait()
        f.seek(0)
        f.write(b'GWV1' + struct.pack('<HHHHIIIII', ow, oh, k, 0, us, rate, 0, 0, 0))
    os.replace(dst + '.tmp', dst)
    print('%s: %dx%d -> %dx%d, %d frames, %d KB (%d KB a second)' %
          (os.path.basename(src), w, h, ow, oh, k, total // 1024, total * 1e6 / us / max(k, 1) / 1024))


if __name__ == '__main__':
    main()
