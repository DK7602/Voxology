# Generates assets/marble_blue_white.jpg: blue / white agate marble with gold flecks (Honey Tune).
import numpy as np
from PIL import Image, ImageFilter
from scipy.ndimage import map_coordinates
def noise(w,h,scale,seed):
    r=np.random.default_rng(seed)
    small=r.random((max(2,h//scale)+1,max(2,w//scale)+1))
    return np.asarray(Image.fromarray((small*255).astype(np.uint8)).resize((w,h),Image.BICUBIC),dtype=np.float32)/255
def fbm(w,h,seed,base=260):
    return sum(a*noise(w,h,max(2,int(base*s)),seed+i) for i,(s,a) in enumerate(((1,.55),(.5,.27),(.25,.13),(.12,.05))))
W,H=1600,1000; P=400; WW,HH=W+2*P,H+2*P
y,x=np.mgrid[0:HH,0:WW].astype(np.float32)
def sample(a,yy,xx): return map_coordinates(a,[yy,xx],order=1,mode='reflect')
q1=fbm(WW,HH,3)-.5; q2=fbm(WW,HH,8)-.5
d=(x*0.8+y*0.6)/520.0 + 1.6*sample(fbm(WW,HH,21,300),y+300*q1,x+300*q2)
d=d[P:P+H,P:P+W]
t=(np.sin(d*np.pi*2.2)+1)/2
f=(np.sin(d*np.pi*11+fbm(W,H,40,80)*4)+1)/2
c_deep=np.array([62,112,166]); c_mid=np.array([118,160,204]); c_pale=np.array([212,228,240]); c_white=np.array([247,249,251])
def lerp(a,b,k): return a*(1-k[...,None])+b*k[...,None]
col=lerp(c_deep,c_mid,np.clip((t-0.1)/0.5,0,1)**1.3)
col=lerp(col,c_pale,np.clip((t-0.5)/0.3,0,1))
col=lerp(col,c_white,np.clip((t-0.78)/0.14,0,1))
col=lerp(col,c_white,np.clip(f-0.85,0,1)*3*0.6)
col=lerp(col,c_deep*0.8,np.clip(0.1-f,0,1)*5*0.5)
a=np.asarray(Image.fromarray(np.clip(col,0,255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.8)),dtype=np.float32)
# 3D: a height field from the bands (pale stone raised, blue veins sunk), lit from the top left, glossy.
from scipy.ndimage import gaussian_filter
h=gaussian_filter(t,4.0)*1.0+gaussian_filter(f,1.5)*0.12
gy,gx=np.gradient(h)
nx,ny,nz=-gx*28,-gy*28,np.ones_like(h)
nl=np.sqrt(nx*nx+ny*ny+nz*nz); nx,ny,nz=nx/nl,ny/nl,nz/nl
L=np.array([-0.55,-0.65,0.52]); L=L/np.linalg.norm(L)
diff=nx*L[0]+ny*L[1]+nz*L[2]
shade=0.72+0.42*diff
Hh=np.array([0,0,1.0])+L; Hh=Hh/np.linalg.norm(Hh)
spec=np.clip(nx*Hh[0]+ny*Hh[1]+nz*Hh[2],0,1)**60
a=a*shade[...,None]+255*spec[...,None]*0.55
# broad polished sheen
yy,xx=np.mgrid[0:H,0:W].astype(np.float32)
sheen=np.clip(1-np.abs(((xx*0.55+yy)/H)-0.75)*2.2,0,1)**2*28
a=a+sheen[...,None]
r=np.random.default_rng(99)
mask=(r.random((H,W))>0.9994)
spk=Image.fromarray((mask*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6))
s=np.clip(np.asarray(spk,dtype=np.float32)[...,None]/255*2.5,0,1)
gold=np.array([236,198,104],np.float32)
a=a*(1-s*0.85)+gold*s*0.85
Image.fromarray(np.clip(a,0,255).astype(np.uint8)).save('marble_blue_white.jpg',quality=88)
