"""Original #1513 tree body retention and both pipeline updates without SRT.

Only Python tuple/converter services and a custom MatrixProvider are harness
boundaries; manager update and native pipeline getters execute unchanged.
"""
import struct
from stock_tree_lod_oracle import OriginalTreeInstructionOracle
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP


def probe(executable):
    oracle=OriginalTreeInstructionOracle(executable)
    u=oracle.emulator
    manager=oracle.allocate(0x60)
    head=oracle.allocate(32)
    renderer_root=oracle.allocate(32)
    pipeline=oracle.allocate(0x100)
    vegetation=oracle.allocate(0x2000)
    body=oracle.allocate(48)
    body_vtable=oracle.allocate(16)
    write=lambda address,*words:u.mem_write(address,struct.pack('<'+'I'*len(words),*words))
    u.mem_write(manager,bytes(0x60));write(manager+0x14,head,0,0);write(head,head)
    write(0x1d211b4,manager);write(0x1d20620,renderer_root);write(renderer_root+0x10,pipeline)
    write(pipeline,0x14fd3b0);write(pipeline+0xe4,0)
    write(body_vtable+8,0x3000e010)
    write(body,body_vtable,100,0,0)
    state={'provider':body,'samples':0,'matrices':[]}
    source=struct.pack('<16f',*range(1,17))

    def hook(uc,address,size,context):
     stack=uc.reg_read(UC_X86_REG_ESP);cleanup=0
     if address==0x3000e010:
      assert uc.reg_read(UC_X86_REG_ECX)==body
      output=oracle.word(stack+4);uc.mem_write(output,source)
      state['samples']+=1;state['matrices'].append(output);result=0;cleanup=4
     elif address==0xfb3610:result=1
     elif address==0xfb3390:assert oracle.word(stack+8)==0;result=0xdead0000
     else:
      assert address==0x69ef50
      output=oracle.word(stack+8);write(output,state['provider'])
      if state['provider']:write(body+4,oracle.word(body+4)+1)
      result=0
     uc.reg_write(UC_X86_REG_EAX,result);uc.reg_write(UC_X86_REG_EIP,oracle.word(stack))
     uc.reg_write(UC_X86_REG_ESP,stack+4+cleanup)
    for address in (0x3000e010,0xfb3610,0xfb3390,0x69ef50):
     u.hook_add(UC_HOOK_CODE,hook,begin=address,end=address)
    # The setter retains even when no rendering owner is returned and no trees exist.
    oracle.call(0x746d60,(0,0xdead0000))
    assert oracle.word(manager+0x4c)==body and oracle.word(body+4)==101
    oracle.call(0x748370,ecx=manager)
    assert state['samples']==0
    # A renderer with no vegetation configuration (manager+1c==0) still samples
    # once per manager update, before its exact renderer update's empty guard.
    for table,offset in ((0x14fd3b0,0xe4),(0x14fd894,0x30)):
     write(pipeline,table);write(pipeline+offset,vegetation)
     before=state['samples']
     for index in range(3):
      oracle.call(0x748370,ecx=manager)
      assert state['samples']==before+index+1
      assert bytes(u.mem_read(vegetation+0x1c8c,64))==source
    assert state['matrices']==[vegetation+0x1c8c]*6
    # No provider clears only m44; it preserves the rest of the sampled matrix.
    state['provider']=0
    oracle.call(0x746d60,(0,0xdead0000))
    assert oracle.word(manager+0x4c)==0 and oracle.word(body+4)==100
    oracle.call(0x748370,ecx=manager)
    assert state['samples']==6
    assert bytes(u.mem_read(vegetation+0x1c8c,60))==source[:60]
    assert oracle.word(vegetation+0x1cc8)==0
    return {'original_setter':'00746d60','original_update':'00748370',
     'original_renderer_update':'00755670 -> 0074e510',
     'vegetation_configuration':None,'tree_count':0,'retention_without_renderer':True,
     'samples_with_nullable_renderer':0,'samples_with_empty_renderer':6,'actual_pipeline_getters':['009c5e20','0135c1d0'],
     'released_after_clear':True,'clear_only_m44':True,
     'renderer_lookup':'original 00941a10 and actual deferred/forward vtables executed'}
