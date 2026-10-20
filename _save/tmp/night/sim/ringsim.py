# Simulates the PCSS filter of shadow.hlsli on a 'leaf canopy' shadow map with small sunlit gaps: per pixel a random
# rotation of the 12 taps (as the IGN noise angle does, then averaged by TAA ~ many rotations), radius R texels.
import numpy as np, math
from PIL import Image
old=[(-0.326,-0.406),(-0.840,-0.074),(-0.696,0.457),(-0.203,0.621),(0.962,-0.195),(0.473,-0.480),(0.519,0.767),(0.185,-0.893),(0.507,0.064),(0.896,0.412),(-0.322,-0.933),(-0.792,-0.598)]
new=[(math.sqrt((i+0.5)/12)*math.cos(i*2.39996323), math.sqrt((i+0.5)/12)*math.sin(i*2.39996323)) for i in range(12)]
N=160
lit=np.zeros((N,N))
rng=np.random.default_rng(3)
for k in range(9):   # gaps of 1..4 texels radius
    cx,cy=rng.integers(15,N-15,2); r=[1,1.5,2,2.5,3,3,4,4,5][k]
    yy,xx=np.mgrid[0:N,0:N]; lit[(xx-cx)**2+(yy-cy)**2<=r*r]=1
def filt(taps,R=8.0,rots=24):
    out=np.zeros_like(lit)
    for a in np.linspace(0,2*math.pi,rots,endpoint=False):
        ca,sa=math.cos(a),math.sin(a)
        acc=np.zeros_like(lit)
        for (x,y) in taps:
            dx=int(round((ca*x-sa*y)*R)); dy=int(round((sa*x+ca*y)*R))
            acc+=np.roll(np.roll(lit,dy,0),dx,1)
        out+=acc/len(taps)
    return out/rots
a=filt(old); b=filt(new)
def img(m): return Image.fromarray((np.clip(m/m.max(),0,1)**(1/2.2)*255).astype(np.uint8)).resize((N*3,N*3),Image.NEAREST)
w=Image.new('L',(N*9+20,N*3)); w.paste(img(lit),(0,0)); w.paste(img(a),(N*3+10,0)); w.paste(img(b),(N*6+20,0))
w.save('/tmp/night/sim/ringsim.png'); print('saved')
