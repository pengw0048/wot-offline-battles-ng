"""Original #1513 BoidData avoidance and angle consumers, with D3DX/CRT boundaries."""
import struct


class OriginalBoids:
    def __init__(self, executable):
        from stock_tree_lod_oracle import OriginalTreeInstructionOracle
        from unicorn import UC_HOOK_CODE
        from unicorn import x86_const as r
        self.p = OriginalTreeInstructionOracle(executable)
        self.u = self.p.emulator
        self.r = r
        self.boid = self.p.allocate(44)
        self.filter = self.p.allocate(0x3a0)
        self.goal = self.p.allocate(12)
        self.output = self.p.allocate(12)
        self.mode = ''
        self.u.hook_add(UC_HOOK_CODE,self.hook)

    def floats(self,address,values):
        self.u.mem_write(address,struct.pack('<'+'f'*len(values),*values))

    def read(self,address,n):
        return struct.unpack('<'+'f'*n,self.u.mem_read(address,n*4))

    def returned(self,value=0,cleanup=0):
        stack=self.u.reg_read(self.r.UC_X86_REG_ESP)
        self.u.reg_write(self.r.UC_X86_REG_ESP,stack+4+cleanup)
        self.u.reg_write(self.r.UC_X86_REG_EAX,value)
        self.u.reg_write(self.r.UC_X86_REG_EIP,self.p.word(stack))

    def hook(self,u,address,size,unused):
        stack=u.reg_read(self.r.UC_X86_REG_ESP)
        if self.mode=='outer' and self.outer_hook(u,address,stack):return
        if self.mode=='model' and self.matrix_hook(u,address,stack):return
        if address==0x11c3bbe:
            import math
            output,source=self.p.word(stack+4),self.p.word(stack+8)
            x,y,z=self.read(source,3)
            f=lambda v:struct.unpack('<f',struct.pack('<f',v))[0]
            length=f(math.sqrt(f(f(f(x*x)+f(y*y))+f(z*z))))
            self.floats(output,(f(x/length),f(y/length),f(z/length)) if length else (0,0,0))
            self.returned(output,8)
        elif self.mode=='avoid' and address==0x65f021:
            self.before=self.read(self.output,3)
        elif self.mode=='angles' and address==0x65ec90:
            destination=self.p.word(stack+4)
            self.floats(destination,self.delta)
            self.returned(cleanup=12)
        elif self.mode in ('angles','model') and address==0x13dad3d:
            import math
            value=struct.unpack('<f',struct.pack('<I',u.reg_read(self.r.UC_X86_REG_XMM0)&0xffffffff))[0]
            result=math.acos(value) if -1<=value<=1 else float('nan')
            u.reg_write(self.r.UC_X86_REG_XMM0,struct.unpack('<I',struct.pack('<f',result))[0])
            self.returned()
        elif self.mode=='angles' and address==0x65fc96:
            u.reg_write(self.r.UC_X86_REG_EIP,0x30000000)

    def avoid(self,position,direction,centre,radius,goal,others):
        self.mode='avoid';self.before=None
        self.floats(self.boid,position+direction+(0,0,0,0,.5))
        self.floats(self.filter+0x348,(10,.5))
        self.floats(self.filter+0x374,centre+(radius,))
        self.floats(self.goal,goal)
        records=self.p.allocate(max(44,len(others)*44))
        for i,other in enumerate(others):self.floats(records+44*i,other+(0,0,0,0,.5))
        self.u.mem_write(self.filter+0x384,struct.pack('<3I',records,records+44*len(others),records+44*len(others)))
        self.p.call(0x65ec90,(self.output,self.goal,self.filter),ecx=self.boid)
        assert self.before is not None
        return self.before,self.read(self.output,3)

    def angles(self,state,delta,direction,rates,normal,ratio,dt,landing):
        self.mode='angles';self.delta=delta
        self.floats(self.boid,(0,0,0)+direction+state)
        self.floats(self.filter+0x350,(normal,float(landing),ratio,10,1)+rates+(.7853981852531433,))
        bits=struct.unpack('<I',struct.pack('<f',dt))[0]
        self.p.call(0x65f980,(self.goal,0,self.filter,bits,int(landing)),ecx=self.boid)
        return self.read(self.boid+24,5)

    def outer_hook(self,u,address,stack):
        arg=lambda n:self.p.word(stack+4+4*n)
        if address==0x65db30:  # Existing AvatarFilter output / pose producer.
            self.returned(cleanup=12)
        elif address==0x7d49b0:  # Entity's actual position accessor.
            self.floats(arg(0),self.outer_goal);self.returned(arg(0),4)
        elif address==0x67a3e0:  # Space/scene snapshot service.
            u.mem_write(arg(0),struct.pack('<I',self.outer_space));self.returned(arg(0),4)
        elif address==0x65e7c0:  # Vector resize / constructor randomness boundary.
            count=arg(0);owner=u.reg_read(self.r.UC_X86_REG_ECX)
            for i in range(count):self.floats(self.outer_records+44*i,(0,0,0,0,0,1,0,0,0,0,.1))
            u.mem_write(owner,struct.pack('<3I',self.outer_records,self.outer_records+44*count,self.outer_records+44*count))
            self.returned(cleanup=8)
        elif address==0xab9e20:  # Actual scene transform lookup, keyed by model handle.
            u.mem_write(arg(0),struct.pack('<16f',*self.outer_matrices[arg(1)]))
            self.returned(arg(0),12)
        elif address==0x13dad01:  # CRT asinf scalar primitive.
            import math
            v=struct.unpack('<f',struct.pack('<I',u.reg_read(self.r.UC_X86_REG_XMM0)&0xffffffff))[0]
            u.reg_write(self.r.UC_X86_REG_XMM0,struct.unpack('<I',struct.pack('<f',math.asin(v)))[0]);self.returned()
        elif address in (0x65f2c8,0x65f5ad,0x65f670):  # Interlocked retain/release primitives.
            pointer=self.p.word(stack);value=self.p.word(pointer)+(1 if address==0x65f5ad else -1)
            u.mem_write(pointer,struct.pack('<I',value));u.reg_write(self.r.UC_X86_REG_EAX,value)
            u.reg_write(self.r.UC_X86_REG_ESP,stack+4);u.reg_write(self.r.UC_X86_REG_EIP,address+6)
        elif address==0x65f980:
            self.outer_steps.append((self.read(arg(0),3),struct.unpack('<f',struct.pack('<I',arg(3)))[0]))
            self.returned(cleanup=20)
        else:return False
        return True

    def outer(self,goal,matrices,elapsed,initial=True):
        self.mode='outer';self.outer_goal=goal;self.outer_matrices=matrices;self.outer_steps=[]
        self.outer_space=self.p.allocate(64);self.u.mem_write(self.outer_space+4,struct.pack('<I',100))
        self.u.mem_write(self.outer_space+12,struct.pack('<I',self.p.allocate(64)))
        entity=self.p.allocate(0x100);models=self.p.allocate(4*max(1,len(matrices)))
        for i in range(len(matrices)):
            model=self.p.allocate(32);self.u.mem_write(model+4,struct.pack('<I',100))
            self.u.mem_write(model+16,struct.pack('<2I',i,0));self.u.mem_write(models+4*i,struct.pack('<I',model))
        self.u.mem_write(entity+0xec,struct.pack('<2I',models,models+4*len(matrices)))
        self.outer_records=self.p.allocate(44*max(1,len(matrices)))
        count=0 if initial else len(matrices)
        self.u.mem_write(self.filter+0x384,struct.pack('<3I',self.outer_records,self.outer_records+44*count,self.outer_records+44*count))
        self.u.mem_write(self.filter+0x390,struct.pack('<d',0.))
        self.u.mem_write(self.filter+0x354,struct.pack('<I',0))
        self.u.mem_write(0x1d59546,b'\0')
        words=struct.unpack('<2I',struct.pack('<d',elapsed))
        self.p.call(0x65f1c0,words+(entity,),ecx=self.filter)
        values=[self.read(self.outer_records+44*i,11) for i in range(len(matrices))]
        return values,list(self.outer_steps)

    def matrix_hook(self,u,address,stack):
        arg=lambda n:self.p.word(stack+4+4*n)
        if address not in (0x11c3bd0,0x11c3bf4,0x11c3bee,0x11c3bfa,0x11c3bc4,0x62d9a0):return False
        import math
        f=lambda v:struct.unpack('<f',struct.pack('<f',v))[0]
        if address==0x62d9a0:
            self.model_matrix=self.read(arg(0),16);self.returned(cleanup=4);return True
        out=arg(0);m=[1. if i%5==0 else 0. for i in range(16)]
        if address==0x11c3bd0:
            m[12:15]=[struct.unpack('<f',struct.pack('<I',arg(i)))[0] for i in (1,2,3)];cleanup=16
        elif address==0x11c3bc4:
            a,b=self.read(arg(1),16),self.read(arg(2),16)
            for row in range(4):
                for col in range(4):
                    value=0.
                    for k in range(4):value=f(value+f(a[row*4+k]*b[k*4+col]))
                    m[row*4+col]=value
            cleanup=12
        else:
            angle=struct.unpack('<f',struct.pack('<I',arg(1)))[0];c,s=f(math.cos(angle)),f(math.sin(angle))
            if address==0x11c3bf4:m[0]=m[10]=c;m[2]=-s;m[8]=s
            elif address==0x11c3bee:m[5]=m[10]=c;m[6]=s;m[9]=-s
            else:m[0]=m[5]=c;m[1]=s;m[4]=-s
            cleanup=8
        self.floats(out,m);self.returned(out,cleanup);return True

    def model(self,state,position,direction,rates,centre,radius,normal,ratio,dt,landing,goal):
        self.mode='model';self.model_matrix=None
        self.floats(self.boid,position+direction+state);self.floats(self.goal,goal)
        self.floats(self.filter+0x348,(10,.5,normal,0,ratio,10,1)+rates+(.7853981852531433,)+centre+(radius,))
        self.u.mem_write(self.filter+0x384,struct.pack('<3I',self.boid,self.boid,self.boid))
        bits=struct.unpack('<I',struct.pack('<f',dt))[0]
        self.p.call(0x65f980,(self.goal,0,self.filter,bits,int(landing)),ecx=self.boid)
        assert self.model_matrix is not None
        return self.read(self.boid,11)+self.model_matrix

    def random_values(self,seed,count):
        owner=self.p.allocate(0x9c4)
        self.p.call(0x68cce0,(seed,),ecx=owner)
        result=[]
        for unused in range(count):
            self.p.call(0x68cb30,ecx=owner)
            result.append(self.u.reg_read(self.r.UC_X86_REG_EAX))
        return result
