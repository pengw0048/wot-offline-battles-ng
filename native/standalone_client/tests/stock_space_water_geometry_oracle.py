"""Run original water-cell producer and generic culler with real x86 D3DX math."""
import argparse
import json
from pathlib import Path
import random
import struct


def fixtures(destination):
    rng=random.Random(1513);rows=[]
    for case in range(4000):
        low=[rng.uniform(-500,500) for unused in range(3)]
        high=[v+rng.uniform(0,100) for v in low]
        if case%11==0:high[1]=low[1]
        if case%17==0:high[0]=low[0]-1
        if case%29==0:high=low[:]
        world=[rng.uniform(-2,2) for unused in range(16)]
        for i in (3,7,11):world[i]=0
        world[15]=1
        world[12:15]=[rng.uniform(-10000,10000) for unused in range(3)]
        rows.append(struct.pack('<22f',*(low+high+world)))
    Path(destination).write_bytes(b''.join(rows))


def audit(client,dll_path,inputs,dispatch,actual,destination=None):
    import pefile
    from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_EDI,UC_X86_REG_EDX,UC_X86_REG_EBP,UC_X86_REG_ESP,UC_X86_REG_EAX,UC_X86_REG_EIP
    game=pefile.PE(str(Path(client)/'WorldOfTanks.exe'));dll=pefile.PE(str(dll_path));uc=Uc(UC_ARCH_X86,UC_MODE_32)
    for image in (game,dll):
        data=image.get_memory_mapped_image();base=image.OPTIONAL_HEADER.ImageBase;uc.mem_map(base,(len(data)+4095)//4096*4096);uc.mem_write(base,data)
    uc.mem_map(0x30000000,0x20000)
    loader,cell,instance,water,frame,stack,box,finish=0x30000000,0x30001000,0x30002000,0x30003000,0x30005000,0x30008000,0x3000a000,0x30010000
    exports={s.name:dll.OPTIONAL_HEADER.ImageBase+s.address for s in dll.DIRECTORY_ENTRY_EXPORT.symbols if s.name}
    for lib in game.DIRECTORY_ENTRY_IMPORT:
        for symbol in lib.imports:
            if symbol.name in exports:uc.mem_write(symbol.address,struct.pack('<I',exports[symbol.name]))
    uc.mem_write(dll.OPTIONAL_HEADER.ImageBase+0x1ca000,Path(dispatch).read_bytes())
    def crt(uc,address,unused_size,unused_data):
        esp=uc.reg_read(UC_X86_REG_ESP)
        if address==0x13dac1d:
            target,value,size=struct.unpack('<III',uc.mem_read(esp+4,12));uc.mem_write(target,bytes([value&255])*size);uc.reg_write(UC_X86_REG_EAX,target)
        target=struct.unpack('<I',uc.mem_read(esp,4))[0];uc.reg_write(UC_X86_REG_ESP,esp+4);uc.reg_write(UC_X86_REG_EIP,target)
    for address in (0x13dac1d,0x13d809e):uc.hook_add(UC_HOOK_CODE,crt,begin=address,end=address)
    uc.mem_write(loader+0x40,struct.pack('<I',cell));uc.mem_write(cell+0x18,struct.pack('<I',instance));uc.mem_write(instance+0x64,struct.pack('<I',water))
    output=[];blob=Path(inputs).read_bytes()
    for offset in range(0,len(blob),88):
        row=blob[offset:offset+88];uc.mem_write(cell,row[:24]);uc.mem_write(water+0x17c,row[24:]);uc.reg_write(UC_X86_REG_EDI,loader);uc.reg_write(UC_X86_REG_EDX,0);uc.reg_write(UC_X86_REG_EBP,frame);uc.reg_write(UC_X86_REG_ESP,stack)
        uc.emu_start(0x6c7380,0x6c74b2,count=100000)
        matrix=bytes(uc.mem_read(frame-0x50,64));floats=struct.unpack('<16f',matrix);enabled=any(floats[i]!=0 for i in (0,5,10))
        bounds=bytes(24)
        if enabled:
            uc.mem_write(box,struct.pack('<6f',*([-.5]*3+[.5]*3)));uc.mem_write(stack,struct.pack('<II',finish,frame-0x50));uc.reg_write(UC_X86_REG_ECX,box);uc.reg_write(UC_X86_REG_ESP,stack)
            uc.emu_start(0x11bc780,finish,count=100000);bounds=bytes(uc.mem_read(box,24))
        output.append(' '.join(map(str,(int(enabled),)+struct.unpack('<22I',matrix+bounds))))
    observed=Path(actual).read_text().splitlines()
    bad=[(i,a,b) for i,(a,b) in enumerate(zip(observed,output)) if a.split()!=b.split()]
    if len(observed)!=len(output) or bad:raise AssertionError('Original water geometry mismatch: '+repr(bad[:3]))
    if destination:
        target=Path(destination);target.mkdir(parents=True,exist_ok=True);(target/'geometry-expected.txt').write_text('\n'.join(output)+'\n')
    return {'original':['006c72c0','011bf030','011bc780'],'sdk':'Actual x86 D3DX9_43 math instructions','cases':len(output),'bitMismatches':0,'status':'PASS'}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='mode',required=True)
    p=sub.add_parser('fixtures');p.add_argument('destination',type=Path)
    p=sub.add_parser('audit')
    for n in ('client','dll','inputs','dispatch','actual'):p.add_argument(n,type=Path)
    p.add_argument('--output',type=Path)
    args=parser.parse_args()
    if args.mode=='fixtures':fixtures(args.destination)
    else:print(json.dumps(audit(args.client,args.dll,args.inputs,args.dispatch,args.actual,args.output)))
