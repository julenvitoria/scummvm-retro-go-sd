#!/usr/bin/env python3
"""Play Operation Stealth past its colour-card tests on the PC build, the way a player does:
look at the region the game marks, find its colour on an image of the card, click that colour.
Then let the game run and keep a Retro-Go save state from after the card.

usage: stealth_card.py EXE GAMEDIR CARD.gif OUTDIR [run_seconds [intro_seconds]]
The card image is the player's own; it is not in the repository. Scratch goes in OUTDIR.
Drives the game through GW_LIVE (src/platform_host.c)."""
import sys, os, subprocess, glob, collections, time, shutil
from PIL import Image
exe, game, cardf, out = sys.argv[1:5]
run_s = int(sys.argv[5]) if len(sys.argv) > 5 else 300
intro_s = int(sys.argv[6]) if len(sys.argv) > 6 else 150
card = Image.open(cardf).convert('RGB')
NAMES = ['BLACK', 'WHITE', 'YELLOW', 'ORANGE', 'RED', 'BROWN', 'GREY', 'PINK', 'PURPLE', 'LIGHT BLUE', 'DARK BLUE',
         'LIGHT GREEN', 'DARK GREEN']
COL = {(66, 66, 66): 0, (43, 43, 43): 0, (254, 254, 254): 1, (255, 255, 255): 1, (255, 255, 0): 2, (255, 128, 0): 3,
       (255, 0, 0): 4, (128, 64, 0): 5, (129, 129, 129): 6, (206, 206, 206): 6, (255, 128, 192): 7, (128, 0, 128): 8,
       (128, 192, 255): 9, (0, 0, 255): 10, (0, 255, 0): 11, (0, 128, 0): 12}
shutil.rmtree(out, ignore_errors=True)
os.makedirs(out + '/shots'); os.makedirs(out + '/saves'); os.makedirs(out + '/keep')
live = out + '/live.txt'; open(live, 'w').close()
def send(line):
    with open(live, 'a') as f: f.write(line + '\n')
env = dict(os.environ, GW_LIVE=live, GW_WAV='/dev/null', GW_SAVES=out + '/saves', GW_SHOTS=out + '/shots', GW_EVERY='300',
           GW_QUIT=str((run_s + 600) * 1000), GW_HEAP_PEAK=out + '/peak.txt')
log = open(out + '/log.txt', 'w')
p = subprocess.Popen([exe, game], env=env, stdout=log, stderr=subprocess.STDOUT)
def frames(since):
    fs = sorted(f for f in glob.glob(out + '/shots/*.ppm') if os.path.getmtime(f) > since)
    ims = []
    for f in fs[-8:]:
        try: ims.append(Image.open(f).convert('RGB')); ims[-1].load()
        except Exception: pass
    for f in sorted(glob.glob(out + '/shots/*.ppm'))[:-40]: os.remove(f)
    return ims
def is_start(im):       # the green "OK"
    return sum(1 for y in range(180, 197) for x in range(137, 169) if im.getpixel((x, y))[1] > 200 and im.getpixel((x, y))[0] < 80) > 40
def is_test(im):        # the red frame or line around the marked region
    return sum(1 for y in range(0, 200, 1) for x in range(0, 320, 1) if im.getpixel((x, y)) [0] > 200 and im.getpixel((x, y))[1] < 60 and im.getpixel((x, y))[2] < 60) > 15
last, answers, passed_at, saved, t0 = time.time(), 0, 0, False, time.time()
try:
    while p.poll() is None and time.time() - t0 < run_s + 500:
        time.sleep(0.5)
        ims = frames(last + 1.5)
        if len(ims) < 4: continue
        im = ims[-1]
        if passed_at:
            saved = saved or len(os.listdir(out + '/saves')) > 1      # more than scummvm.ini
            # the game refuses a save during the intro: once it has run, click the talk on and try
            if not saved and time.time() - passed_at > intro_s and time.time() - last > 5:
                send('k b 120'); time.sleep(2); send('s'); last = time.time()
            if time.time() - passed_at > run_s: break
            n = int(time.time() - passed_at)
            if n % 6 == 0 and not os.path.exists(f'{out}/keep/t{n:04d}.png'): im.save(f'{out}/keep/t{n:04d}.png')
            continue
        if is_start(im):
            if answers: print('back at the start after', answers, 'answers: wrong, again', flush=True)
            answers = 0
            send('m 159 190'); time.sleep(0.4); send('k b 120'); last = time.time(); continue
        if is_test(im):
            cnt = collections.Counter(); xs = []
            for a in ims:
                for b in ims:
                    if a is b: continue
                    pa, pb = a.load(), b.load()
                    for y in range(200):
                        for x in range(320):
                            if pa[x, y] == (0, 0, 0) and pb[x, y] != (0, 0, 0):
                                c = card.getpixel((x + 8, y + 8))
                                if c in COL: cnt[COL[c]] += 1; xs.append(x)
                if cnt: break
            if not cnt: continue
            top = cnt.most_common(2)
            if len(top) > 1 and top[1][1] * 3 > top[0][1]: continue      # frames from two screens: look again
            idx, cx = top[0][0], sum(xs) / len(xs)
            # where is the menu? the green box is on the side away from the region
            mx = 275 if cx < 160 else 55
            print('test', answers + 1, NAMES[idx], dict((NAMES[k], v) for k, v in top), flush=True)
            send(f'm {mx} {43 + 10 * idx}'); time.sleep(0.4); send('k b 120'); last = time.time(); answers += 1
            continue
        if answers >= 2 and time.time() - last > 8:
            passed_at = time.time(); im.save(out + '/keep/passed.png')
            print('past the card after', answers, 'answers', flush=True)
finally:
    send('q')
    try: p.wait(10)
    except Exception: p.kill()
    log.close()
print('save state:', sorted(os.listdir(out + '/saves')), open(out + '/peak.txt').readline().strip() if os.path.exists(out + '/peak.txt') else '')
