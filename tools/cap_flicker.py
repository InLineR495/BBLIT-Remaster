# Всплески яркости пола по записи [live] cap: python3 cap_flicker.py кадры.raw cap.txt номер_первого_кадра
# (кадры.raw — RGB24 564x353, уже перевёрнутые vflip, например ffmpeg ... -vf "vflip,select=between(n\,260\,719)")
import numpy as np, sys
W,H=564,353
raw=sys.argv[1]; meta=sys.argv[2]; first=int(sys.argv[3])
a=np.fromfile(raw,dtype=np.uint8).reshape(-1,H,W,3).astype(np.float32)
n=len(a)
lines=[l.split() for l in open(meta).read().splitlines()[1:]]
pairs={int(l[0]):int(l[3]) for l in lines}
bad={int(l[0]):' '.join(x.split('/')[0][-4:]+'/'+x.split('/')[1]+'/'+x.split('/')[3] for x in l[5:]) for l in lines if len(l)>5}
y0=int(H*0.62)
fl=a[:,y0:,:,:].mean(axis=3)       # пол: нижние 38 %
# всплеск: кадр отличается от обоих соседей в одну сторону
d1=fl[1:-1]-fl[:-2]; d2=fl[1:-1]-fl[2:]
same=np.sign(d1)==np.sign(d2)
spike=np.where(same,np.minimum(np.abs(d1),np.abs(d2)),0)
sp=spike.mean(axis=(1,2)); big=(spike>12).mean(axis=(1,2))*100
mean=fl.mean(axis=(1,2))
for i in range(1,n-1):
    f=first+i
    mark='*' if pairs.get(f)!=pairs.get(f-1) else ' '
    if sp[i-1]>0.25 or big[i-1]>0.3:
        print('%4d %s pair %3d mean %6.2f spike %.2f big%% %.2f  %s'%(f,mark,pairs.get(f,-1),mean[i],sp[i-1],big[i-1],bad.get(f,'')))
print('median spike %.3f'%np.median(sp))
