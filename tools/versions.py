#!/usr/bin/env python3
"""The versions of a build, and its release zip.

  versions.py OUT                 the table: the loader's version (SVM_LOADER_VERSION) and the
                                  id of every engine file (the CRC-32 in its footer), which is
                                  what the PAUSE menu shows on the console
  versions.py OUT --zip FILE      also writes the zip to unpack at the root of the SD card:
                                  cores/scummvm.bin, cores/scummvm/<engine>.svm and
                                  cores/scummvm/engines.txt (the loader's table of engines)

OUT is the build's out/ directory. The zip is the same for the same files (fixed dates, sorted).
"""
import glob, os, re, struct, sys, zipfile

def main():
    out = sys.argv[1]
    here = os.path.dirname(os.path.abspath(__file__))
    hdr = open(os.path.join(here, '..', 'src', 'svm_xip.h')).read()
    loader = int(re.search(r'#define\s+SVM_LOADER_VERSION\s+(\d+)', hdr).group(1))
    files = [(os.path.join(out, 'scummvm.bin'), 'cores/scummvm.bin')]
    rows = [('`/cores/scummvm.bin`', 'loader', 'L%d' % loader, os.path.getsize(files[0][0]))]
    for path in sorted(glob.glob(os.path.join(out, 'scummvm', '*.svm'))):
        data = open(path, 'rb').read()
        magic, image_id = struct.unpack('<I', data[-52:-48])[0], struct.unpack('<I', data[-4:])[0]
        if magic != 0x474E4553:
            sys.exit('%s: not an engine file' % path)
        name = os.path.basename(path)
        files.append((path, 'cores/scummvm/' + name))
        rows.append(('`/cores/scummvm/%s`' % name, name[:-4], '%08X' % image_id, len(data)))
    table = os.path.join(out, 'scummvm', 'engines.txt')
    files.append((table, 'cores/scummvm/engines.txt'))
    print('| File | Engine | Version / id | Size |')
    print('|---|---|---|---|')
    for f, e, i, n in rows:
        print('| %s | %s | `%s` | %d KB |' % (f, e, i, (n + 1023) // 1024))
    if '--zip' in sys.argv:
        dest = sys.argv[sys.argv.index('--zip') + 1]
        with zipfile.ZipFile(dest, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for path, arc in files:
                info = zipfile.ZipInfo(arc, (2026, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                z.writestr(info, open(path, 'rb').read())

main()
