# Generates assets/marble_cream.jpg: soft cream / ivory marble with faint warm veins (Honey Tune's roll, behind the
# notes: low contrast so the notes stand out). Same method as make_marble.py (the blue / white one).
import numpy as np
from PIL import Image, ImageFilter
from scipy.ndimage import map_coordinates, gaussian_filter
def noise(w,h,scale,seed):
    r=np.random.default_rng(seed)
    small=r.random((max(2,h//scale)+1,max(2,w//scale)+1))
    return np.asarray(Image.fromarray((small*255).astype(np.uint8)).resize((w,h),Image.BICUBIC),dtype=np.float32)/255
def fbm(w,h,seed,base=260):
    return sum(a*noise(w,h,max(2,int(base*s)),seed+i) for i,(s,a) in enumerate(((1,.55),(.5,.27),(.25,.13),(.12,.05))))
W,H=1600,1000; P=400; WW,HH=W+2*P,H+2*P
y,x=np.mgrid[0:HH,0:WW].astype(np.float32)
def sample(a,yy,xx): return map_coordinates(a,[yy,xx],order=1,mode='reflect')
q1=fbm(WW,HH,5)-.5; q2=fbm(WW,HH,9)-.5
d=(x*0.8+y*0.6)/620.0 + 1.4*sample(fbm(WW,HH,23,320),y+300*q1,x+300*q2)
d=d[P:P+H,P:P+W]
t=(np.sin(d*np.pi*2.0)+1)/2
f=(np.sin(d*np.pi*10+fbm(W,H,41,80)*4)+1)/2
c_vein=np.array([206,188,152]); c_mid=np.array([232,221,198]); c_base=np.array([243,236,220]); c_white=np.array([251,248,240])
def lerp(a,b,k): return a*(1-k[...,None])+b*k[...,None]
col=lerp(c_mid,c_base,np.clip((t-0.15)/0.45,0,1))
col=lerp(col,c_white,np.clip((t-0.7)/0.25,0,1))
col=lerp(col,c_vein,np.clip(0.12-f,0,1)*5*0.55)      # fine warm veins
col=lerp(col,c_white,np.clip(f-0.9,0,1)*4*0.5)
a=np.asarray(Image.fromarray(np.clip(col,0,255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.8)),dtype=np.float32)
# A little depth and polish (much gentler than the blue one).
h=gaussian_filter(t,4.0)*1.0+gaussian_filter(f,1.5)*0.08
gy,gx=np.gradient(h)
nx,ny,nz=-gx*20,-gy*20,np.ones_like(h)
nl=np.sqrt(nx*nx+ny*ny+nz*nz); nx,ny,nz=nx/nl,ny/nl,nz/nl
L=np.array([-0.55,-0.65,0.52]); L=L/np.linalg.norm(L)
diff=nx*L[0]+ny*L[1]+nz*L[2]
a=a*(0.95+0.07*diff)[...,None]
yy,xx=np.mgrid[0:H,0:W].astype(np.float32)
a=a+(np.clip(1-np.abs(((xx*0.55+yy)/H)-0.75)*2.2,0,1)**2*12)[...,None]
r=np.random.default_rng(97)
mask=(r.random((H,W))>0.9996)
spk=Image.fromarray((mask*255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6))
s=np.clip(np.asarray(spk,dtype=np.float32)[...,None]/255*2.0,0,1)
gold=np.array([222,180,90],np.float32)
a=a*(1-s*0.6)+gold*s*0.6
# Quiet: pulled 45 % toward the base colour, so it reads as stone without pulling the eye from the notes.
a=c_base+(a-c_base)*0.55
Image.fromarray(np.clip(a,0,255).astype(np.uint8)).save('marble_cream.jpg',quality=88)
