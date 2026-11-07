import collections,subprocess,sys
lines=open(sys.argv[1]).read().splitlines()
base=int(lines[0].split()[1].split('-')[0],16)
live={}
for l in lines[1:]:
    f=l.split()
    if not f: continue
    if f[0]=='A' and len(f)>=3: live[f[1]]=(int(f[2]),f[3:])
    elif f[0]=='F': live.pop(f[1],None)
addrs=sorted({a for sz,fr in live.values() for a in fr if base<=int(a,16)<base+0x20000000})
out=subprocess.run(['addr2line','-f','-C','-e',sys.argv[2]]+['%x'%(int(a,16)-base) for a in addrs],capture_output=True,text=True).stdout.splitlines()
fn={a:out[2*i] for i,a in enumerate(addrs)}
def cat(fr):
    names=[fn.get(a,'') for a in fr]
    s=' | '.join(names)
    if 'OPL::' in s: return 'audio: AdLib OPL emulator'
    if 'IMuse' in s or 'MidiDriver' in s or 'Player_' in s or 'Sound::' in s: return 'audio: iMuse/sound'
    if 'Audio::' in s or 'Mixer' in s: return 'audio: mixer'
    if 'createInstance' in s: return 'engine: ScummEngine object'
    if 'ResourceManager' in s:
        if 'initVirtScreen' in s or 'initBGBuffers' in s: return 'engine: virtual screens'
        return 'engine: game resources'
    if 'Scumm::' in s: return 'engine: other (surfaces, tables, actors...)'
    if 'GUI::' in s or 'Theme' in s: return 'ScummVM GUI'
    if 'HeadlessGraphics' in s: return 'backend screen copy'
    if 'Translation' in s or 'ConfigManager' in s or 'registerDefaults' in s or 'commandLine' in s or 'Keymap' in s: return 'config/translations/keymaps'
    return 'other ScummVM framework'
agg=collections.Counter(); cnt=collections.Counter()
for sz,fr in live.values(): k=cat(fr); agg[k]+=sz; cnt[k]+=1
print('total live',sum(agg.values()))
for k,v in agg.most_common(): print('%9d  %5d allocs  %s'%(v,cnt[k],k))
