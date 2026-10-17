import sys, re, collections
rows=[]
for line in open(sys.argv[1]):
    if line.startswith('#'): print(line.strip()); continue
    L,A,B,J=[x.strip() for x in re.split(r'\|',line)][:4] if line.count('|')>=3 else (None,)*4
    l=L.split(); a=A.split(); b=B.split()
    r=dict(prim=int(l[0]),k=int(l[1]),tag=l[2],obj=l[3],local=int(l[4]),tex=int(l[5][4:]),abr=int(l[7][4:]),
           x=int(a[0])/100,y=int(a[1])/100,z=int(a[2])/100,u=int(a[3])/1e4,v=int(a[4])/1e4,c=a[5],
           xa=int(b[0])/100,ya=int(b[1])/100,za=int(b[2])/100,ua=int(b[3])/1e4,va=int(b[4])/1e4,ca=b[5])
    m=re.search(r'j=(-?\d+) jump=(-?\d+) bad=(-?\d+)',J); r['j'],r['jump'],r['bad']=map(int,m.groups())
    rows.append(r)
print('verts',len(rows))
# по текстурам: число вершин, с изменением uv, медиана |du|,|dv|
by=collections.defaultdict(list)
for r in rows:
    if r['j']>=0: by[(r['tag'],r['tex'],r['abr'])].append(r)
print('tag tex abr | n  uvchg  du(min/med/max) dv(min/med/max)  objs')
for key,rs in sorted(by.items(), key=lambda kv:-len(kv[1])):
    ch=[r for r in rs if r['u']!=r['ua'] or r['v']!=r['va']]
    if not ch: continue
    du=sorted(r['u']-r['ua'] for r in ch); dv=sorted(r['v']-r['va'] for r in ch)
    objs=len(set(r['obj'] for r in rs))
    print(key,'|',len(rs),len(ch),' du %.3f/%.3f/%.3f'%(du[0],du[len(du)//2],du[-1]),' dv %.3f/%.3f/%.3f'%(dv[0],dv[len(dv)//2],dv[-1]),' objs',objs)
print('\nскачки:')
for r in rows:
    if r['jump']==1: print(r['prim'],r['tag'],r['obj'],r['local'],'tex',r['tex'],'B(%.0f,%.0f,%.0f) A(%.0f,%.0f,%.0f)'%(r['x'],r['y'],r['z'],r['xa'],r['ya'],r['za']))
bad=collections.Counter((r['tag'],r['obj'],r['tex']) for r in rows if r['bad']==1)
print('\nплохие объекты (вершин):',bad.most_common(12))
