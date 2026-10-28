"""Original #1513 SAT/contact manifold for the ground-decal AABB query.

The entire f69650 function and its real polygon/segment helpers execute.
Only allocation, memory copying and the security-cookie boundary are hooked.
This does not substitute triangle contacts or clipping decisions.
"""
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESP
import stock_runtime_services_oracle as base


class OriginalGroundDecalContacts(base.OriginalRuntimeServices):
    def __init__(self,executable):
        super(OriginalGroundDecalContacts,self).__init__(executable)
        self.owner=base.MARKS+0x10000
        self.box=base.MARKS+0x11000
        self.triangle=base.MARKS+0x12000
        self.output=base.MARKS+0x13000
        self.heap=base.MARKS+0x40000
        tls,values=base.MARKS+0x70000,base.MARKS+0x71000
        self.u.mem_write(base.GDT+0x102c,struct.pack('<I',tls))
        self.u.mem_write(tls,struct.pack('<I',values))
        self.u.mem_write(0x215e38c,struct.pack('<I',0))
        self.selected=None
        self.native_hooks={0xf69ec4:lambda:setattr(self,"selected",self.u.reg_read(UC_X86_REG_EDX)),0x13d7fe9:self.allocate,0x13d8019:lambda:self._return(0),
            0x13dabf9:self.copy,0x13d809e:lambda:self._return(self.u.reg_read(UC_X86_REG_EAX))}
        for address in self.native_hooks:
            self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.native_hooks[a](),begin=address,end=address)

    def allocate(self):
        address=self.heap
        self.heap+=(self._arg(1)+31)&~31
        if self.heap>base.MARKS+0x6f000:raise AssertionError('Oracle allocator exhausted')
        self._return(address)

    def copy(self):
        target,source,size=self._arg(1),self._arg(2),self._arg(3)
        self.u.mem_write(target,bytes(self.u.mem_read(source,size)))
        self._return(target)

    def contacts(self,triangle,center,radius):
        self.selected=None
        center=tuple(base.float32(value) for value in center)
        radius=base.float32(radius)
        u,box=self.u,self.box
        u.mem_write(self.owner,struct.pack('<4I',self.output,0,1000,0))
        u.mem_write(self.triangle,struct.pack('<9f',*(value for point in triangle for value in point)))
        # 933610/933220's complete OBB layout for the axis-aligned uniform
        # query constructed by 932000..9320eb. No matrix helper is replaced.
        u.mem_write(box,bytes(0x11c))
        for offset in (0,0x40):
            matrix=[0.]*16
            for axis in range(4):matrix[axis*4+axis]=1.
            for axis in range(3):matrix[12+axis]=center[axis] if offset==0 else -center[axis]
            u.mem_write(box+offset,struct.pack('<16f',*matrix))
        vectors=((0x8c,[radius*2]*3),(0x98,center),(0xa4,[1,0,0]),(0xb0,[0,1,0]),
            (0xbc,[0,0,1]),(0xc8,[radius,0,0]),(0xd4,[0,radius,0]),(0xe0,[0,0,radius]),
            (0xec,[value-radius for value in center]),(0xf8,[value+radius for value in center]))
        for offset,values in vectors:u.mem_write(box+offset,struct.pack('<3f',*values))
        for axis in range(3):u.mem_write(box+0x104+axis*8,struct.pack('<2f',center[axis]-radius,center[axis]+radius))
        stack=base.STACK+0x80000
        u.mem_write(stack,struct.pack('<4I',base.SENTINEL,self.owner,box,self.triangle))
        u.reg_write(UC_X86_REG_ESP,stack)
        u.emu_start(0xf69650,base.SENTINEL+1,count=100000)
        assert u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
        count=struct.unpack('<I',u.mem_read(self.owner+4,4))[0]
        assert bool(u.reg_read(UC_X86_REG_EAX)&255)==bool(count)
        assert count<=1000
        return [bytes(u.mem_read(self.output+index*52,52)) for index in range(count)]


class OriginalGroundDecalBsp(base.OriginalRuntimeServices):
    def __init__(self,executable):
        super(OriginalGroundDecalBsp,self).__init__(executable)
        self.nodes=base.MARKS+0x10000
        self.triangles=base.MARKS+0x20000
        self.stackStorage=base.MARKS+0x30000
        self.inputs=base.MARKS+0x40000
        self.callback=base.MARKS+0x60000
        tls,values=base.MARKS+0x70000,base.MARKS+0x71000
        self.u.mem_write(base.GDT+0x102c,struct.pack('<I',tls));self.u.mem_write(tls,struct.pack('<I',values))
        self.u.mem_write(0x215e38c,struct.pack('<I',0))
        self.u.mem_write(0x21535bc,struct.pack('<4I',self.stackStorage,self.stackStorage+0x10000,0,0))
        def visited():
            self.visited.append((self._arg(1)-self.triangles)//40)
            self._return(0)
        self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:visited(),begin=self.callback,end=self.callback)

    def visit(self,planes,center,axes):
        self.visited=[]
        for index,plane in enumerate(planes):
            front=index*2+1;back=front+1
            children=[self.nodes+child*48 if child<len(planes) else 0 for child in (front,back)]
            # Each node owns one distinct triangle and shares its predecessor.
            shared=self.inputs+0x1000+index*4
            self.u.mem_write(shared,struct.pack('<I',self.triangles+max(0,index-1)*40))
            self.u.mem_write(self.nodes+index*48,struct.pack('<2I4f4I',*(children+list(plane)+
                [self.triangles+index*40,1,shared,1])))
            self.u.mem_write(self.triangles+index*40,bytes(40))
        for index,value in enumerate([center]+list(axes)):
            self.u.mem_write(self.inputs+index*12,struct.pack('<3f',*value))
        stack=base.STACK+0x80000
        self.u.mem_write(stack,struct.pack('<7I',base.SENTINEL,self.inputs,self.inputs+12,self.inputs+24,
            self.inputs+36,self.callback,0))
        self.u.reg_write(UC_X86_REG_ESP,stack)
        from unicorn.x86_const import UC_X86_REG_ECX
        self.u.reg_write(UC_X86_REG_ECX,self.nodes)
        self.u.emu_start(0x1179620,base.SENTINEL+1,count=100000)
        assert self.u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
        return self.visited


class OriginalGroundDecalTerrain(OriginalGroundDecalContacts):
    def __init__(self,executable):
        super(OriginalGroundDecalTerrain,self).__init__(executable)
        self.source=base.MARKS+0x22000
        self.matrix=base.MARKS+0x24000
        self.heightmap=base.MARKS+0x25000
        self.block=base.MARKS+0x26000
        self.release=base.MARKS+0x65000
        self.u.mem_write(0x14913a0,struct.pack('<I',self.release))
        self.terrain_hooks={0xad1460:self.heightmap_source,0xad7f20:self.triangle_source,
            self.release:lambda:self.stdcall(1,1)}
        for address in self.terrain_hooks:
            self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.terrain_hooks[a](),begin=address,end=address)

    def stdcall(self,value,arguments):
        stack=self.u.reg_read(UC_X86_REG_ESP);target=self._arg(0)
        self.u.reg_write(UC_X86_REG_EAX,value)
        self.u.reg_write(UC_X86_REG_ESP,stack+4+arguments*4)
        self.u.reg_write(UC_X86_REG_EIP,target)

    def heightmap_source(self):
        assert self._arg(2)==1
        self.u.mem_write(self._arg(1),struct.pack('<I',self.heightmap))
        self.stdcall(self._arg(1),2)

    def triangle_source(self):
        self.terrain_query=[struct.unpack('<3f',self.u.mem_read(self._arg(index),12)) for index in range(2,8)]
        begin,end=self.source,self.source+len(self.terrain_triangles)*40
        self.u.mem_write(self._arg(1),struct.pack('<3I',begin,end,end))
        self.stdcall(0,7)

    def terrain_records(self,triangles,center,radius,translation=(0.,0.,0.)):
        # Populate the exact complete OBB and reset its contact output.
        self.contacts(((1000.,1000.,1000.),(1001.,1000.,1000.),(1000.,1000.,1001.)),center,radius)
        self.u.mem_write(self.owner,struct.pack('<4I',self.output,0,1000,0))
        self.terrain_triangles=triangles
        for index,triangle in enumerate(triangles):
            self.u.mem_write(self.source+index*40,struct.pack('<9fHH',*(list(value for point in triangle for value in point)+[0,0])))
        matrix=[0.]*16
        for axis in range(4):matrix[axis*4+axis]=1.
        matrix[12:15]=translation
        self.u.mem_write(self.matrix,struct.pack('<16f',*matrix))
        stack=base.STACK+0x80000
        self.u.mem_write(stack,struct.pack('<5I',base.SENTINEL,self.block,self.matrix,self.owner,self.box))
        self.u.reg_write(UC_X86_REG_ESP,stack)
        self.u.emu_start(0xf5ee50,base.SENTINEL+1,count=100000)
        assert self.u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
        result=self.u.reg_read(UC_X86_REG_EAX)&255
        count=struct.unpack('<I',self.u.mem_read(self.owner+4,4))[0]
        return result,[bytes(self.u.mem_read(self.output+index*52,52))for index in range(count)]

    def dispatch(self,order,static_records):
        # The terrain handler is original 6c8fa0 -> f5ee50. Static records
        # enter at its SceneObject-handler boundary; their manifold is already
        # independently executed above. f5e720's OR/ordering runs unchanged.
        u=self.u
        collider,scene,operation,indices=base.MARKS+0x80000,base.MARKS+0x81000,base.MARKS+0x82000,base.MARKS+0x83000
        feature,vt,blocks=base.MARKS+0x84000,base.MARKS+0x85000,base.MARKS+0x86000
        stub=base.MARKS+0x66000
        u.mem_write(collider,bytes(0x308));u.mem_write(collider+4,struct.pack('<I',scene))
        u.mem_write(scene+12,struct.pack('<I',operation));u.mem_write(operation,bytes(0x108))
        u.mem_write(0x213166c,b'\0')
        u.mem_write(self.owner,struct.pack('<4I',self.output,0,1000,0))
        u.mem_write(blocks,bytes(u.mem_read(self.matrix,64)))
        u.mem_write(blocks+0xb0,struct.pack('<I',self.block))
        def static_source():
            target=self._arg(5);destination,count,capacity=struct.unpack('<3I',u.mem_read(target,12))
            assert count+len(static_records)<=capacity
            for index,value in enumerate(static_records):u.mem_write(destination+(count+index)*52,value)
            u.mem_write(target+4,struct.pack('<I',count+len(static_records)))
            self.stdcall(int(bool(static_records)),8)
        if not hasattr(self,'static_source_hooked'):
            self.static_source_callback=static_source
            u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:self.static_source_callback(),begin=stub,end=stub)
            self.static_source_hooked=True
        else:self.static_source_callback=static_source
        for index,name in enumerate(order):
            member=feature+index*0x100
            table=vt+index*16
            u.mem_write(member,struct.pack('<I',table))
            u.mem_write(member+0x44,struct.pack('<I',blocks))
            u.mem_write(table+4,struct.pack('<I',0x6c8fa0 if name=='terrain' else stub))
            u.mem_write(operation+8+index*4,struct.pack('<I',member))
            u.mem_write(indices+index*4,struct.pack('<I',0))
            u.mem_write(collider+8+index*12,struct.pack('<3I',indices+index*4,indices+index*4+4,indices+index*4+4))
        stack=base.STACK+0x80000
        u.mem_write(stack,struct.pack('<7I',base.SENTINEL,0,0,self.owner,self.box,0,0))
        from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_EBP
        u.reg_write(UC_X86_REG_ECX,collider);u.reg_write(UC_X86_REG_ESP,stack)
        u.emu_start(0xf5e720,base.SENTINEL+1,count=100000)
        assert u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
        result=u.reg_read(UC_X86_REG_EAX)&255
        count=struct.unpack('<I',u.mem_read(self.owner+4,4))[0]
        contacts=[bytes(u.mem_read(self.output+index*52,52))for index in range(count)]
        # Execute the original caller's condition too. A nonempty terrain
        # output does not override a false feature-dispatch result.
        stopped=[]
        for address in (0x932123,0x9325bf):
            handle=u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:(stopped.append(a),u.emu_stop()),begin=address,end=address)
            stopped.append(handle)
        u.reg_write(UC_X86_REG_EBP,stack)
        u.emu_start(0x93211b,base.SENTINEL+1,count=10)
        destination=u.reg_read(UC_X86_REG_EIP)
        for handle in stopped[:2]:u.hook_del(handle)
        assert destination==(0x932123 if result else 0x9325bf)
        return result,contacts,destination


class OriginalGroundDecalProjection(base.OriginalRuntimeServices):
    """Execute 932123..932555 with std::vector::push_back and D3DX boundaries."""
    def __init__(self,executable):
        super(OriginalGroundDecalProjection,self).__init__(executable)
        self.frame=base.STACK+0x80000
        self.records=base.MARKS+0x10000
        self.projected=base.MARKS+0x30000
        self.start=base.MARKS+0x40000
        def push():
            from unicorn.x86_const import UC_X86_REG_ECX
            vector=self.u.reg_read(UC_X86_REG_ECX)
            count=len(self.values)
            self.values.append(bytes(self.u.mem_read(self._arg(1),12)))
            self.u.mem_write(self.projected+count*12,self.values[-1])
            self.u.mem_write(vector,struct.pack('<3I',self.projected,self.projected+(count+1)*12,self.projected+0x10000))
            stack=self.u.reg_read(UC_X86_REG_ESP)
            self._return(0);self.u.reg_write(UC_X86_REG_ESP,stack+8)
        def normalize():
            import math
            f=base.float32
            target,source=self._arg(1),self._arg(2)
            values=struct.unpack('<3f',self.u.mem_read(source,12))
            squared=f(f(f(values[0]*values[0])+f(values[1]*values[1]))+f(values[2]*values[2]))
            length=f(math.sqrt(squared))
            result=[f(v/length)for v in values]if length else [0.,0.,0.]
            self.u.mem_write(target,struct.pack('<3f',*result))
            stack=self.u.reg_read(UC_X86_REG_ESP)
            self._return(target);self.u.reg_write(UC_X86_REG_ESP,stack+12)
        self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:push(),begin=0x72f1a0,end=0x72f1a0)
        self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:normalize(),begin=0x11c3bbe,end=0x11c3bbe)
        self.u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:u.emu_stop(),begin=0x932555,end=0x932555)

    def project(self,contacts,start,direction):
        from unicorn.x86_const import UC_X86_REG_EBP,UC_X86_REG_EDI
        u,frame=self.u,self.frame
        self.values=[]
        u.mem_write(frame-0x1000,bytes(0x1100))
        u.mem_write(self.start,struct.pack('<3f',*start))
        for index,(point,normal,flags)in enumerate(contacts):
            u.mem_write(self.records+index*52,struct.pack('<12fHH',*([0.]*3+list(point)+[0.]*3+list(normal)+[flags,0])))
        u.mem_write(frame-0x34,struct.pack('<2I',self.records,len(contacts)))
        u.mem_write(frame-0x28,struct.pack('<3f',*direction))
        u.reg_write(UC_X86_REG_EBP,frame);u.reg_write(UC_X86_REG_EDI,self.start);u.reg_write(UC_X86_REG_ESP,frame-0x1000)
        u.emu_start(0x932123,0x932556,count=100000)
        assert u.reg_read(UC_X86_REG_EIP)==0x932555
        return bytes(u.mem_read(frame-0x1c,12))+bytes(u.mem_read(frame-0x394,4))+bytes(u.mem_read(frame-0x398,4))+struct.pack('<I',u.mem_read(frame-0x389,1)[0])


def wrapper_contract(executable):
    """Original 931270 argument-count/conversion failure paths, unchanged."""
    original=base.OriginalRuntimeServices(executable);u=original.u
    hooks={0x690240:lambda:original._set_data('Vector3'),0x690190:lambda:original._set_data('Vector2'),
           0x68fa20:lambda:original._set_data('int'),0x11e7c30:lambda:original._return(0),
           0x13d809e:lambda:original._return(u.reg_read(UC_X86_REG_EAX))}
    for address in hooks:u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:hooks[a](),begin=address,end=address)
    rows=[]
    cases=[(argc,None)for argc in range(10)if argc!=8]+[(8,index)for index in range(8)]
    for argc,fail in cases:
        original.argc=argc;original.fail_at=fail;original.values=[0]*8;original.calls=[]
        stack=base.STACK+0x80000;u.mem_write(stack,struct.pack('<3I',base.SENTINEL,0,base.ARGS));u.reg_write(UC_X86_REG_ESP,stack)
        u.emu_start(0x931270,base.SENTINEL+1,count=10000)
        assert u.reg_read(UC_X86_REG_EIP)==base.SENTINEL and u.reg_read(UC_X86_REG_EAX)==0
        rows.append((argc,fail,list(original.calls)))
    return rows


def prepare_contract(executable):
    """Real f5fb60 -> ab7fa0 setup; only scene-query consumers are bounded."""
    from unicorn.x86_const import UC_X86_REG_ECX
    original=base.OriginalRuntimeServices(executable);u=original.u
    collider,scene,bounds,empty,flag=(base.MARKS+0x10000,base.MARKS+0x12000,base.MARKS+0x13000,base.MARKS+0x14000,base.MARKS+0x15000)
    u.mem_write(collider,struct.pack('<I',0x174d088)+bytes(0x304))
    u.mem_write(scene,bytes(0x100));u.mem_write(bounds,struct.pack('<6f',-1.,-2.,-3.,4.,5.,6.));u.mem_write(empty,bytes(12));u.mem_write(0x1d59546,b'\0')
    calls=[]
    def consume(address):
        context=original._arg(1)
        calls.append((address,struct.unpack('<I',u.mem_read(context+4,4))[0],bytes(u.mem_read(original._arg(2),24))))
        stack=u.reg_read(UC_X86_REG_ESP);original._return(0);u.reg_write(UC_X86_REG_ESP,stack+16)
    for address in (0xaba0b0,0xaba3a0,0xaba800):u.hook_add(unicorn.UC_HOOK_CODE,lambda u,a,s,d:consume(a),begin=address,end=address)
    stack=base.STACK+0x80000;u.mem_write(stack,struct.pack('<6I',base.SENTINEL,bounds,scene,0xffffffff,empty,0));u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,collider)
    u.emu_start(0xf5fb60,base.SENTINEL+1,count=10000)
    assert u.reg_read(UC_X86_REG_EIP)==base.SENTINEL
    eligibility=[]
    for value in range(256):
        u.mem_write(flag,bytes([value]));u.mem_write(stack,struct.pack('<I',base.SENTINEL));u.reg_write(UC_X86_REG_ESP,stack);u.reg_write(UC_X86_REG_ECX,flag)
        u.emu_start(0xab8060,base.SENTINEL+1,count=10000)
        eligibility.append(u.reg_read(UC_X86_REG_EAX))
    return calls,eligibility
