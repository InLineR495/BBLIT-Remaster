import sys, re, collections
rows=[]
for line in open(sys.argv[1]):
    if line.startswith('#'): print(line.strip()); continue
    L,A,B,J=[x.strip() for x in line.split('|')][:4]
    l=L.split(); a=A.split(); b=B.split()
    r=dict(prim=int(l[0]),tag=l[2],obj=l[3],local=int(l[4]),tex=int(l[5][4:]),abr=int(l[7][4:]),
           x=int(a[0])/100,y=int(a[1])/100,z=int(a[2])/100,u=int(a[3])/1e4,v=int(a[4])/1e4,c=a[5],
           xa=int(b[0])/100,ya=int(b[1])/100,za=int(b[2])/100,ua=int(b[3])/1e4,va=int(b[4])/1e4,ca=b[5])
    m=re.search(r'j=(-?\d+) jump=(-?\d+) bad=(-?\d+)',J); r['j'],r['jump'],r['bad']=map(int,m.groups())
    rows.append(r)
by=collections.defaultdict(list)
for r in rows: by[(r['tag'],r['obj'])].append(r)
print('tag obj | nverts prims | tex | abr | x y z range | alpha set | prim order')
for k,rs in sorted(by.items(), key=lambda kv:-len(kv[1])):
    tex=collections.Counter(r['tex'] for r in rs); abr=collections.Counter(r['abr'] for r in rs)
    al=set(r['c'][:2] for r in rs)
    pr=sorted(set(r['prim'] for r in rs))
    print(k,len(rs),len(pr),dict(tex.most_common(4)),dict(abr),
      'x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f'%(min(r['x'] for r in rs),max(r['x'] for r in rs),min(r['y'] for r in rs),max(r['y'] for r in rs),min(r['z'] for r in rs),max(r['z'] for r in rs)),
      sorted(al)[:4], pr[0],'..',pr[-1])
