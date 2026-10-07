import numpy as np, glob
hop=128/48000.
def zlp(x,fc):
    c=1-np.exp(-2*np.pi*fc*hop); y=x.copy()
    for _ in range(2):
        a=y[0]
        for k in range(len(y)): a+=(y[k]-a)*c; y[k]=a
        a=y[-1]
        for k in range(len(y)-1,-1,-1): a+=(y[k]-a)*c; y[k]=a
    return y
files=['Gallas_2026_Note26_Raw','Don_Birthday_2024_Vox_only','Gallas_2026_Clip_Acapella','Schaf_2026_Clip_Acapella','Don_Lysette_Clip_Acapella']
print('%-4s %-4s | %-30s | %-26s | %-10s | %s'%('ver','set','centre error (c): med  p90','shape change (c): med p90','jumps/min','wrong-note time %'))
for tag in ('n25','n60','c10'):
  for v in ('old','fix'):
    C=[];S=[];J=0;T=0;W=[]
    for f in files:
        N=np.loadtxt(f+'.notes',ndmin=2); d=np.loadtxt('%s_%s_%s.d'%(f,v,tag))
        t=d[:,0]; vo=d[:,1]>0; s=d[:,2]; ap=d[:,3]
        for st,en,p in N:
            if en-st<0.3: continue
            m=(t>=st+0.1)&(t<=en-0.05)&vo
            if m.sum()<30: continue
            tg=round(p); o=s[m]+ap[m]; ss=s[m]
            C.append(abs(np.median(o)-tg)*100)
            S.append(np.sqrt(np.mean(((o-zlp(o,1))-(ss-zlp(ss,1)))**2))*100)
            J+=np.sum(np.abs(np.diff(ap[m]))>0.06); T+=m.sum()*hop
            W.append(np.mean(np.abs(zlp(o,3)-tg)>0.5)*100)
    print('%-4s %-4s | %6.1f %6.1f                  | %6.1f %6.1f              | %6.1f     | %5.1f'%(v,tag,np.median(C),np.percentile(C,90),np.median(S),np.percentile(S,90),J/T*60,np.mean(W)))
  print()
