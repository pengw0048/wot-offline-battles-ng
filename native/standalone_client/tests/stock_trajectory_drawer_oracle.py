"""Execute #1513 TrajectoryDrawer compute/tick/colour/bounds instructions.

006f7130 and its real 00f5d660 -> 008a9290 adaptive subdivision execute
unchanged. D3DX look-at/inverse/transform and camera-space queries are the
explicit environment boundary, with a caller supplied deterministic scene.
006f6cc0 (packing colours) and 006f6700 (map bounds) execute unchanged too.
"""
import math
import struct
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP
import unicorn
import stock_ballistics_oracle as base

STATE=base.DATA+0x50000
TARGET=STATE+0x200
START=TARGET+12
VELOCITY=START+12
BOUNDS=STATE+0x300
SPACE=STATE+0x400


def f(x): return base.f32(x)
def normalise(a):
    length=f(math.sqrt(f(f(f(a[0]*a[0])+f(a[1]*a[1]))+f(a[2]*a[2]))))
    return [f(x/length) for x in a] if length else [0.,0.,0.]
def cross(a,b): return [f(f(a[1]*b[2])-f(a[2]*b[1])),f(f(a[2]*b[0])-f(a[0]*b[2])),f(f(a[0]*b[1])-f(a[1]*b[0]))]


class Original(base.Original):
    def __init__(self,executable):
        super(Original,self).__init__(executable)
        self.scene=None
        self.basis=None
        extra={0x11c3be8:self.lookat,0x11c3bb8:self.inverse,
            0xac2f10:self.camera_space,0xf5c9a0:self.scene_query,
            0x13dabf9:lambda:self.ret(0,0),0xac2b70:self.bounds,
            0x13d809e:lambda:self.ret(None,0)}
        for address,hook in extra.items():
            self.hooks[address]=hook
            self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.hooks[a](),begin=address,end=address)
        self.put(STATE,'<40I',*([0]*40))
        self.put(STATE+0x94,'<3I',0,0,0)
        self.put(0x1a0e968,'<B',0) # Dynamic colliders are separately covered by the binding consumer.

    def lookat(self):
        eye,target,up=(self.floats(self.arg(i),3) for i in (2,3,4))
        z=normalise([f(target[i]-eye[i]) for i in range(3)])
        x=normalise(cross(up,z));y=cross(z,x)
        self.basis=(x,y,z,eye)
        self.ret(self.arg(1),16)

    def inverse(self):
        out=self.arg(1);x,y,z,eye=self.basis
        self.put(out,'<16f',*(x+[0.]+y+[0.]+z+[0.]+eye+[1.]))
        self.ret(out,12)

    def camera_space(self):
        self.put(self.arg(1),'<I',0)
        self.put(BOUNDS,'<I',SPACE)
        self.ret(BOUNDS,0)

    def bounds(self):
        self.put(self.arg(1),'<6f',*self.space_bounds)
        self.ret(self.arg(1),4)

    def scene_query(self):
        start,end=self.floats(self.arg(3),3),self.floats(self.arg(4),3)
        hit=self.scene(start,end) if self.scene else None
        if hit is not None:self.put(self.arg(1),'<3f',*hit)
        self.ret(1 if hit is not None else 0,0)

    def calculate(self,target,start,velocity,gravity,maximum,scene,bounds):
        self.scene=scene;self.space_bounds=bounds
        self.put(TARGET,'<9f',*(target+start+velocity))
        self.put(STATE+0x10,'<f',maximum)
        self.put(STATE+0x1c,'<3f',*gravity)
        self.call(0x6f7130,STATE,[TARGET,START,VELOCITY])
        return self.floats(STATE+0x40,4)

    def colour(self,rgba):
        self.put(TARGET,'<4f',*rgba)
        return self.call(0x6f6cc0,0,[TARGET])

    def leave_bounds(self,minimum,maximum,start,end):
        self.space_bounds=minimum+maximum
        self.put(TARGET,'<6f',*(start+end))
        self.put(TARGET+24,'<3f',0.,0.,0.)
        yes=self.call(0x6f6700,0,[TARGET+24,TARGET,TARGET+12])&255
        return yes,self.floats(TARGET+24,3)

    def advance(self,values):
        self.put(STATE,'<40I',*([0]*40))
        self.put(STATE+0xc,'<3B',*map(int,values[:3]))
        self.put(STATE+0x14,'<2f',*values[3:5])
        self.put(STATE+0x30,'<8f',*values[5:13])
        self.put(STATE+0x50,'<6f',*values[13:19])
        self.put(STATE+0x7c,'<I',STATE+0x100)
        self.put(STATE+0x100,'<24I',*([0]*24))
        def get_camera():self.ret(STATE+0x500,0)
        def near_point():
            self.put(self.arg(1),'<3f',0.,-1.,1.)
            self.ret(self.arg(1),12)
        def transform_normal():
            self.put(self.arg(1),'<3f',0.,-1.,1.);self.ret(self.arg(1),12)
        def transform_coord():
            self.put(self.arg(1),'<3f',0.,100.,0.);self.ret(self.arg(1),12)
        def camera_hit():
            self.put(self.arg(1),'<3f',*values[19:22]);self.ret(int(values[23]),0)
        for address,hook in {0x9288d0:get_camera,0x9288e0:get_camera,0x965e40:near_point,
                0x11c3c06:transform_normal,0x11c3bdc:transform_coord,0xf5d1c0:camera_hit}.items():
            if address not in self.hooks:
                self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.hooks[a](),begin=address,end=address)
            self.hooks[address]=hook
        self.call(0x6f6d90,STATE,[base.bits(values[22])])
        vertices=[]
        for i in range(6):vertices+=self.floats(STATE+0x100+16*i,3)
        return self.floats(STATE+0x14,1)+vertices

    def simulate(self,start,velocity,gravity,step,epsilon,skip=0x80,required=0,ground=0):
        # f5db50/f5bf40 run unchanged; only the external scene and Python
        # allocation/conversion boundary are supplied by the host environment.
        self.put(TARGET,'<9f',*(start+velocity+gravity))
        self.put(0x1a5c798,'<I',100)
        self.results={}
        self.mask_calls=[]
        def allocate(value):
            address=self.heap;self.heap+=0x100
            self.put(address,'<I',100)
            self.results[address]=value
            return address
        def tuple_new():self.ret(allocate('tuple'),0)
        def get_float():self.ret(allocate(self.farg(1)),0)
        def get_vector():self.ret(allocate(self.floats(self.arg(1),3)),0)
        def scene():
            a,b=self.floats(self.arg(1),3),self.floats(self.arg(2),3)
            callback=self.arg(3)
            mask=struct.unpack('<2H',self.u.mem_read(callback+8,4));self.mask_calls.append(mask)
            if (a[1]>ground and b[1]<=ground) or (a[1]<ground and b[1]>=ground):
                # Fixture surfaces have flag 0: honor the required mask.
                if not mask[1]:
                    delta=[f(b[i]-a[i]) for i in range(3)]
                    length=f(math.sqrt(f(f(f(delta[0]*delta[0])+f(delta[1]*delta[1]))+f(delta[2]*delta[2]))))
                    fraction=f(f(a[1]-ground)/f(a[1]-b[1]))
                    self.put(callback+4,'<f',f(length*fraction))
            self.ret(0,12)
        for address,hook in {0xfb3420:tuple_new,0x68ed00:get_float,0x68ebd0:get_vector,0xac2c90:scene}.items():
            if address not in self.hooks:
                self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.hooks[a](),begin=address,end=address)
            self.hooks[address]=hook
        result=self.call(0xf5db50,0,[TARGET,START,VELOCITY,base.bits(step),base.bits(epsilon),skip&0xffffffff,required&0xffffffff])
        if result==0x1a5c798:return None
        pointers=struct.unpack('<3I',self.u.mem_read(result+12,12))
        return [self.results[p] for p in pointers]
