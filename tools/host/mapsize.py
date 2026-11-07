import re,collections,sys
txt=open(sys.argv[1]).read(); txt=txt[txt.index('Linker script and memory map'):]
agg=collections.Counter(); sub=collections.Counter(); pend=None
for l in txt.splitlines():
    m=re.match(r'^ (\.text|\.rodata|\.data)(\S*)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)',l)
    if m: addr,sz,f=int(m.group(3),16),int(m.group(4),16),m.group(5)
    else:
        m2=re.match(r'^ (\.text|\.rodata|\.data)\S*$',l)
        if m2: pend=1; continue
        m3=re.match(r'^\s{16}0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)',l)
        if not (m3 and pend): pend=None; continue
        addr,sz,f=int(m3.group(1),16),int(m3.group(2),16),m3.group(3); pend=None
    if addr==0: continue
    f=f.lstrip('./')
    if '/lib/' in f or 'libc' in f: top='libc+libgcc+libstdc++'
    else:
        p=f.split('/'); top=p[0] if p[0]!='engines' else '/'.join(p[:2])
        sub['/'.join(p[:3] if p[0]=='engines' else p[:2])]+=sz
    agg[top]+=sz
print('TOTAL',sum(agg.values()))
for k,v in agg.most_common(): print('%9d %s'%(v,k))
print('---')
for k,v in sub.most_common(int(sys.argv[2]) if len(sys.argv)>2 else 35): print('%9d %s'%(v,k))
