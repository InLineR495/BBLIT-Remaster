# Проверка восстановления вершин без пары по дампу пары ([live] dump=1 -> pair_dump.txt): скрыть 20 %, сравнить аффинное и 4 соседей
import sys, re, collections, numpy as np
H=400.0
def dv(z): return np.where(z>=0, z+1, -z)
rows=[]
for line in open(sys.argv[1]):
    if line.startswith('#'): continue
    L,A,B,J=[x.strip() for x in line.split('|')][:4]
    l=L.split(); a=A.split(); b=B.split()
    m=re.search(r'j=(-?\d+) jump=(-?\d+)',J)
    if int(m.group(1))<0: continue
    rows.append((l[3],int(l[4]),int(l[5][4:]),[int(a[i])/100 for i in range(3)],[int(b[i])/100 for i in range(3)]))
by=collections.defaultdict(dict)
for obj,loc,tex,B_,A_ in rows: by[obj][loc]=(B_,A_)
def view(p):
    p=np.array(p); d=dv(p[:,2])/H; return np.stack([p[:,0]*d,p[:,1]*d,p[:,2]],1)
rng=np.random.default_rng(1)
for key,vs in sorted(by.items(), key=lambda kv:-len(kv[1]))[:14]:
    locs=list(vs); n=len(locs)
    if n<8: continue
    Bv=view([vs[l][0] for l in locs]); Av=view([vs[l][1] for l in locs])
    hide=rng.random(n)<0.2; hide[0]=True
    mb=Bv[~hide].mean(0); ma=Av[~hide].mean(0)
    Bc=Bv[~hide]-mb; Ac=Av[~hide]-ma
    C=Bc.T@Bc; D=Bc.T@Ac
    w,V=np.linalg.eigh(C); inv=np.where(w>1e-9*w.max(),1/np.maximum(w,1e-300),0)
    Lm=(D.T)@(V@np.diag(inv)@V.T)
    res=np.sqrt(((Bc@Lm.T-Ac)**2).sum(1).mean()); spread=np.sqrt((Bc**2).sum(1).mean())
    aff=(Bv[hide]-mb)@Lm.T+ma
    # kNN
    knn=[]
    for p in Bv[hide]:
        dd=((Bv[~hide]-p)**2).sum(1); idx=np.argsort(dd)[:4]; wt=1/(dd[idx]+1)
        knn.append(p+((Av[~hide][idx]-Bv[~hide][idx])*wt[:,None]).sum(0)/wt.sum())
    knn=np.array(knn)
    mv=np.sqrt(((Av[hide]-Bv[hide])**2).sum(1)).mean()
    ea=np.sqrt(((aff-Av[hide])**2).sum(1)); ek=np.sqrt(((knn-Av[hide])**2).sum(1))
    mode='aff' if (int((w>1e-9*w.max()).sum())>=2 and res<=1+0.002*spread) else 'knn'
    print(key,n,'hide',hide.sum(),'move %.2f'%mv,'res %.3f tol %.3f'%(res,1+0.002*spread),mode,'err aff %.3f/%.3f knn %.3f/%.3f'%(ea.mean(),ea.max(),ek.mean(),ek.max()))
