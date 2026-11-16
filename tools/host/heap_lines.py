#!/usr/bin/env python3
"""What was live at the heap's peak (GW_HEAP_PEAK), summed by the first frame inside a path
(default engines/): heap_lines.py EXE PEAKFILE [path-part]. Finer than heap_peak.py: file:line."""
import subprocess, collections, sys
exe, peak = sys.argv[1], sys.argv[2]
part = sys.argv[3] if len(sys.argv) > 3 else '/engines/'
rows = []
for l in open(peak):
    if l.startswith('#'): continue
    f = l.split(); rows.append((int(f[0]), f[1:]))
addrs = sorted({a for _, p in rows for a in p if a != '0x0'})
out = subprocess.run(['addr2line', '-Cfe', exe] + addrs, capture_output=True, text=True).stdout.splitlines()
fn = {a: (out[2 * k], out[2 * k + 1]) for k, a in enumerate(addrs)}
agg = collections.Counter(); cnt = collections.Counter()
for n, p in rows:
    key = 'other'
    for a in p:
        f, l = fn.get(a, ('?', '?'))
        if part in l and '/common/' not in l:
            key = f[:40] + ' ' + l.split('/')[-1]; break
    agg[key] += n; cnt[key] += 1
for k, v in agg.most_common(30): print('%6d KB %5dx %s' % (v // 1024, cnt[k], k))
