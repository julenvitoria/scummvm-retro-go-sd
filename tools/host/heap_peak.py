#!/usr/bin/env python3
"""What was live at the heap's peak, summed by caller (GW_HEAP_PEAK, tools/host/gwheap.c).
usage: heap_peak.py EXE PEAKFILE [depth]  -- depth: how many frames of caller to group by"""
import sys, subprocess, collections
exe, path = sys.argv[1], sys.argv[2]
depth = int(sys.argv[3]) if len(sys.argv) > 3 else 1
rows = []
head = ''
for l in open(path):
    if l.startswith('#'): head = l.strip(); continue
    f = l.split(); rows.append((int(f[0]), f[1:]))
addrs = sorted({a for _, pcs in rows for a in pcs if a != '0x0'})
fn = {}
for i in range(0, len(addrs), 2000):
    chunk = addrs[i:i + 2000]
    out = subprocess.run(['addr2line', '-Cfe', exe] + chunk, capture_output=True, text=True).stdout.splitlines()
    for k, a in enumerate(chunk): fn[a] = out[2 * k]
skip = ('heap_trace', 'alloc', 'malloc', 'operator new', 'calloc', 'realloc', 'Surface::create', 'createResource',
        'allocCapacity', 'HashMap', 'String::', 'Array<', 'memalign', '??', 'BaseString', 'Common::List', 'ensureCapacity',
        'makeRoom', 'U32String')
agg = collections.Counter(); cnt = collections.Counter()
for n, pcs in rows:
    names = [fn.get(a, '?') for a in pcs]
    useful = [x for x in names if not any(s in x for s in skip)]
    key = ' < '.join(x[:60] for x in useful[:depth]) or '?'
    agg[key] += n; cnt[key] += 1
print(head, ' total', sum(agg.values()) // 1024, 'KB in', len(rows), 'blocks')
for k, v in agg.most_common(40): print('%6d KB %5dx  %s' % (v // 1024, cnt[k], k))
