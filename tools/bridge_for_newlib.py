#!/usr/bin/env python3
"""Copy sdk/src/gw_core_bridge.c without the C library it provides for freestanding cores.

This core links newlib (malloc, string, stdio formatting) and libsupc++ itself; only the
firmware calls of boot.c / platform_core.c go through the bridge. Removed here: the
_impure_ptr alias, the _ctype_ table and the block of un-renamed libc exports (malloc, strlen,
fputs, strtoul...), which would otherwise replace newlib's for ScummVM.
usage: bridge_for_newlib.py <in> <out>"""
import re, sys
s = open(sys.argv[1]).read()

def cut(text, start, end, keep_end=True):
    a = text.index(start)
    b = text.index(end, a)
    return text[:a] + (text[b:] if keep_end else text[b + len(end):])

s = cut(s, 'struct _reent *_impure_ptr;', '/* libm')
a = s.index('const char _ctype_[1 + 256] = {')
b = s.index('};', a) + 2
s = s[:a] + s[b:]
s = cut(s, '/* ====================================================================\n * Un-renamed libc exports', '/* ---- ours:')
open(sys.argv[2], 'w').write(s)
