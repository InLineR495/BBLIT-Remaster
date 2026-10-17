# Сопоставление граней A и B по последовательности ключей (obj, local): что меняется у грани — uv, цвет, текстура, позиция.
# python3 dump_faces.py путь/pair_dump [obj или tex ...]  (нужны pair_dump.txt и pair_dump_a.txt из [live] dump=1)
import sys, collections
def load(fn):
    faces=collections.OrderedDict()
    for line in open(fn):
        if line.startswith('#'): continue
        L,A=[x.strip() for x in line.split('|')][:2]
        l=L.split(); a=A.split()
        prim=int(l[0]); obj=l[3]; loc=int(l[4]); tex=int(l[5][4:]); abr=int(l[7][4:])
        v=dict(key=(obj,loc),x=int(a[0])/100,y=int(a[1])/100,z=int(a[2])/100,u=int(a[3])/1e4,v=int(a[4])/1e4,c=a[5])
        faces.setdefault(prim,dict(tex=tex,abr=abr,obj=obj,v=[]))['v'].append(v)
    return list(faces.values())
A=load(sys.argv[1]+'_a.txt'); B=load(sys.argv[1]+'.txt')
def fkey(f):
    ks=[v['key'] for v in f['v']]; i=ks.index(min(ks)); return tuple(ks[i:]+ks[:i])
ai=collections.defaultdict(list)
for f in A: ai[fkey(f)].append(f)
objs=sys.argv[2:] 
st=collections.defaultdict(lambda: collections.Counter())
for f in B:
    if f['obj']=='00000000': continue
    if objs and f['obj'] not in objs and str(f['tex']) not in objs: continue
    k=fkey(f); m=ai.get(k)
    g=(f['obj'],f['tex'])
    if not m: st[g]['noface']+=1; continue
    fa=m[0]
    st[g]['faces']+=1
    if fa['tex']!=f['tex']: st[g]['tex '+str(fa['tex'])+'->'+str(f['tex'])]+=1
    # сопоставить вершины по ключу
    va={v['key']:v for v in fa['v']}
    du=max(abs(v['u']-va[v['key']]['u'])+abs(v['v']-va[v['key']]['v']) for v in f['v'])
    dc=any(v['c']!=va[v['key']]['c'] for v in f['v'])
    dp=max(abs(v['x']-va[v['key']]['x'])+abs(v['y']-va[v['key']]['y'])+abs(v['z']-va[v['key']]['z']) for v in f['v'])
    if du>1e-4: st[g]['uv']+=1; st[g]['uvmax%.3f'%du if du>0.2 else 'uv<0.2']+=0
    if dc: st[g]['col']+=1
    if dp>0.05: st[g]['pos']+=1
for g,c in sorted(st.items()): print(g,dict(c))
