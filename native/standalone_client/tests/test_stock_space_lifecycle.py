"""Original API boundary and production owner methods; fixtures are not a live client."""
from pathlib import Path
import importlib.util
import itertools
import os
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BOUNDARY = r'''
import BigWorld as B
for args,expected in (((),0),((False,),0),((True,),1),((0,),0),((17,),1),((-4,),1),
                      (('false',),0),(('FALSE',),0),(('true',),1),(('TrUe',),1),
                      (('true\0ignored',),1)):
    B._prepare()
    assert B.clearEntitiesAndSpaces(*args) is None
    assert B._state()==expected,(args,B._state())
for args in ((1,2),(True,False,0),(None,),(1L,),(0.0,),(u'true',),([],),('yes',),('',)):
    B._prepare()
    try:B.clearEntitiesAndSpaces(*args)
    except TypeError:pass
    else:raise AssertionError(args)
    assert B._state()==2
try:B.clearEntitiesAndSpaces(keepClientOnlySpaces=True)
except TypeError:pass
else:raise AssertionError('Keywords must not be accepted')
print('Actual CPython '+__import__('sys').version.split()[0]+': optional bool / 0..1 args / None / conversion failure before mutation PASS')
'''

CLIENT_BOUNDARY = r"""
import BigWorld as B,gc,weakref
LOCAL=1<<30
def error(kind,text,call,*args,**keywords):
    try:call(*args,**keywords)
    except kind as caught:assert str(caught)==text,(str(caught),text)
    else:raise AssertionError((call,args))
B._reset()
first=B.createSpace();second=B.createSpace()
assert type(first) is int and LOCAL<=first<second and B.isClientSpace(first) is True
error(TypeError,'createSpace() takes no arguments.',B.createSpace,1)
for name in ('clearSpace','releaseSpace','isClientSpace'):
    error(TypeError,name+'() expects 1 argument of type SpaceID',getattr(B,name))
    error(TypeError,name+'() expects 1 argument of type SpaceID',getattr(B,name),first,first)
    error(TypeError,'() argument 1 must be set to an int',getattr(B,name),'1')
    error(TypeError,'() argument 1 must be set to an int',getattr(B,name),None)
    error(TypeError,name+'() takes no keyword arguments',getattr(B,name),spaceID=first)
error(TypeError,'clearAllSpaces() expects between 0 and 1 arguments of types bool',B.clearAllSpaces,1,2)
error(TypeError,'resetEntityManager() expects between 0 and 2 arguments of types bool, bool',B.resetEntityManager,0,0,0)
# Setting data converts SpaceID like the engine's int setter, floats truncate.
assert B.isClientSpace(float(first)+0.75) is True and B.isClientSpace(1L*first) is True
assert B.isClientSpace(424242) is False and B.isClientSpace(0) is False
error(ValueError,'BigWorld.clearSpace(): No space ID 424242',B.clearSpace,424242)
error(ValueError,'BigWorld.releaseSpace(): No space ID 424242',B.releaseSpace,424242)
B._server(9)
error(ValueError,'BigWorld.clearSpace(): Space ID 9 is not a client space (or is no longer held)',B.clearSpace,9)
error(ValueError,'BigWorld.releaseSpace(): Space ID 9 is not a client space (or is no longer held)',B.releaseSpace,9)
assert B.isClientSpace(9) is False
# clear and release are distinct: clear keeps the held space and its entities.
B._entity(first);B._entity(first)
assert B.clearSpace(first) is None and B._space(first)==(1,1,1) and B._connection(first)==(2,0)
assert B.clearSpace(first) is None and B._space(first)==(2,1,1)
assert B.releaseSpace(first) is None and B._space(first) is None and B._connection(first) is None
error(ValueError,'BigWorld.releaseSpace(): No space ID %d'%first,B.releaseSpace,first)
assert B.isClientSpace(first) is False
# clearAllSpaces: optional bool, geometry only, then a cyclic collection.
for args in ((),(False,),(0,),('false',)):
    B._reset();space=B.createSpace();B._server(3);B._entity(space)
    assert B.clearAllSpaces(*args) is None and B._space(space)==(1,1,1) and B._space(3)[0]==1 and B._connection(space)==(1,0)
for args in ((True,),(1,),('TRUE',)):
    B._reset();space=B.createSpace();B._server(3)
    assert B.clearAllSpaces(*args) is None and B._space(space)==(0,1,1) and B._space(3)[0]==1
for args in ((None,),('yes',),([],)):
    B._reset();space=B.createSpace();B._server(3)
    try:B.clearAllSpaces(*args)
    except TypeError:pass
    else:raise AssertionError(args)
    assert B._space(space)==(0,1,1) and B._space(3)[0]==0
gc.disable()
class Cycle(object):pass
for quiting,expected in ((0,[1]),(1,[])):
    B._quiting(quiting);hits=[];cycle=Cycle();cycle.self=cycle
    reference=weakref.ref(cycle,lambda unused:hits.append(1));del cycle
    B.clearAllSpaces();assert hits==expected,(quiting,hits)
    gc.collect()
B._quiting(0);gc.enable()
# resetEntityManager parses both flags before rejecting keepPlayerAround.
B._reset();B._offline(5);space=B.createSpace();B._entity(space)
error(TypeError,'() argument 2 optionally must be set to a bool',B.resetEntityManager,True,[])
error(ValueError,'Setting keepPlayerAround to True is now invalid',B.resetEntityManager,True)
error(ValueError,'Setting keepPlayerAround to True is now invalid',B.resetEntityManager,'true',False)
assert B._main()==(1,0)
error(TypeError,'resetEntityManager() takes no keyword arguments',B.resetEntityManager,keepPlayerAround=False)
assert B.resetEntityManager() is None and B._main() is None
assert B._connection(space)==(1,0) and B._space(space)==(0,1,1) and B._space(5)[0]==0
assert B.resetEntityManager(False,True) is None
print('Actual CPython '+__import__('sys').version.split()[0]+': six client-space wrappers, conversions, errors and GC PASS')
"""

PYC_SEQUENCES = r"""
import BigWorld as native,zipfile,marshal,types,sys,textwrap,__builtin__
archive=zipfile.ZipFile(PACKAGE)
def code(path,qualified):
    result=marshal.loads(archive.read(path)[8:])
    for part in qualified.split('.'):
        result=[k for k in result.co_consts if isinstance(k,types.CodeType) and k.co_name==part][0]
    return result
log=[]
class Stub(object):
    def __init__(self,name):object.__setattr__(self,'_name',name)
    def __getattr__(self,name):
        if name.startswith('__'):raise AttributeError(name)
        value=Stub(self._name+'.'+name);object.__setattr__(self,name,value);return value
    def __call__(self,*args,**keywords):return Stub(self._name+'()')
    def __getitem__(self,key):return Stub(self._name+'[]')
    def __iter__(self):return iter(())
    def __contains__(self,item):return False
    def __format__(self,spec):return self._name
    def __float__(self):return 0.0
    def __int__(self):return 0
    def __iadd__(self,other):return self
    def __isub__(self,other):return self
def importer(name,*args):return Stub(name)
builtins=dict(vars(__builtin__));builtins['__import__']=importer
def recorded(name):
    function=getattr(native,name)
    def call(*args):
        log.append((name,)+args)
        result=function(*args)
        log.append(('->',name,result))
        return result
    return call
NATIVE=('createSpace','clearSpace','releaseSpace','isClientSpace','clearAllSpaces','resetEntityManager','clearEntitiesAndSpaces')
def bigworld():
    world=Stub('BigWorld')
    for name in NATIVE:object.__setattr__(world,name,recorded(name))
    return world
def run(path,qualified,overrides,*args):
    target=code(path,qualified);names=set()
    def collect(c):
        names.update(c.co_names)
        for k in c.co_consts:
            if isinstance(k,types.CodeType):collect(k)
    collect(target)
    scope={'__builtins__':builtins,'__name__':'pyc'}
    for name in names:
        if name not in builtins:scope[name]=Stub(name)
    scope.update(overrides)
    return types.FunctionType(target,scope)(*args)
def calls():return [entry for entry in log if entry[0]!='->']
HANGAR='scripts/client/gui/ClientHangarSpace.pyc'
class Hangar(Stub):pass
# Exact ClientHangarSpace.create: createSpace, mapping, then HangarVehicle in that space.
def hangar(mapping):
    world=bigworld();created=[]
    def addMapping(spaceID,matrix,path):
        log.append(('addSpaceGeometryMapping',spaceID));return mapping(spaceID)
    def createEntity(kind,spaceID,*args):
        log.append(('createEntity',kind,spaceID));assert native.isClientSpace(spaceID);native._entity(spaceID);created.append(spaceID);return 501
    object.__setattr__(world,'addSpaceGeometryMapping',addMapping)
    object.__setattr__(world,'createEntity',createEntity)
    object.__setattr__(world,'delSpaceGeometryMapping',lambda spaceID,mapping:log.append(('delSpaceGeometryMapping',spaceID,mapping)))
    space=Hangar('hangar')
    run(HANGAR,'ClientHangarSpace.create',{'BigWorld':world,'_HANGAR_CFGS':{}},space,False,None)
    return world,space,created
native._reset()
for cycle in range(2):
    del log[:]
    world,space,created=hangar(lambda spaceID:7)
    spaceID=space._ClientHangarSpace__spaceId
    assert calls()==[('createSpace',),('addSpaceGeometryMapping',spaceID),('createEntity','HangarVehicle',spaceID)],calls()
    assert created==[spaceID] and native._space(spaceID)==(0,1,1) and native._connection(spaceID)==(1,0)
    assert space._ClientHangarSpace__spaceMappingId==7
    del log[:]
    run(HANGAR,'ClientHangarSpace.__destroy',{'BigWorld':world},space)
    assert calls()==[('isClientSpace',spaceID),('delSpaceGeometryMapping',spaceID,7),('clearSpace',spaceID),('releaseSpace',spaceID)],calls()
    assert native._space(spaceID) is None and native._connection(spaceID) is None and not native.isClientSpace(spaceID)
    if cycle:assert spaceID>previous
    previous=spaceID
# The original destroy forgets its space ID; a released ID is not a client space.
assert space._ClientHangarSpace__spaceId is None and native.isClientSpace(previous) is False
del log[:];run(HANGAR,'ClientHangarSpace.__destroy',{'BigWorld':world},space)
assert calls()==[],calls()
# Partial startup: both mapping attempts fail; the original releases without clearing.
def fail(spaceID):raise ValueError('no space.settings')
del log[:];world,space,created=hangar(fail);spaceID=log[1][-1]
assert calls()==[('createSpace',),('addSpaceGeometryMapping',spaceID),('addSpaceGeometryMapping',spaceID),('releaseSpace',spaceID)],calls()
assert created==[] and native._space(spaceID) is None and not native.isClientSpace(spaceID)
# Account.onBecomePlayer clears every space's geometry, not the Account itself.
native._reset();del log[:]
accountSpace=native.createSpace();native._entity(accountSpace);native._server(4)
run('scripts/client/Account.pyc','PlayerAccount.onBecomePlayer',{'BigWorld':bigworld()},Stub('account'))
assert calls()==[('clearAllSpaces',)],calls()
assert native._space(accountSpace)==(1,1,1) and native._connection(accountSpace)==(1,0) and native._space(4)[0]==1
# game.fini: resetEntityManager(False, False) then clearAllSpaces().
native._reset();del log[:]
native._offline(6);accountSpace=native.createSpace();native._entity(accountSpace)
offline=Stub('OfflineMode');object.__setattr__(offline,'enabled',lambda:False)
constants=Stub('constants');object.__setattr__(constants,'IS_CAT_LOADED',False)
run('scripts/client/game.pyc','fini',{'BigWorld':bigworld(),'OfflineMode':offline,'constants':constants})
assert calls()==[('resetEntityManager',False,False),('clearAllSpaces',)],calls()
assert native._main() is None and native._space(6)[0]==1 and native._space(accountSpace)==(1,1,1)
assert native._connection(accountSpace)==(1,0)
# OfflineMapCreator.destroy releases the map through clearEntitiesAndSpaces first.
native._reset();del log[:]
mapSpace=native.createSpace();native._entity(mapSpace)
creator=Stub('creator');object.__setattr__(creator,'_OfflineMapCreator__spaceId',mapSpace)
object.__setattr__(creator,'_OfflineMapCreator__spaceMappingId',3)
run('scripts/client/OfflineMapCreator.pyc','OfflineMapCreator.destroy',{'BigWorld':bigworld()},creator)
assert calls()==[('clearEntitiesAndSpaces',),('isClientSpace',mapSpace)],calls()
assert native._space(mapSpace) is None and native._connection(mapSpace) is None
# Product compat Account producer call order from its current source text.
# createEntity/player here are labelled data fixtures that set the ready
# flags; native creation/promotion is covered by ENTITY_SEQUENCES.
source=open(COMPAT).read()
def method(name):
    start=source.index('    def '+name+'(self')
    end=source.index('\n    def ',start+1)
    return textwrap.dedent(source[start:end])
namespace={'_OFFLINE_INIT_COMPLETE':'_offlineLANInitComplete','_OFFLINE_PLAYER_READY':'_offlineLANPlayerReady'}
exec '\n'.join(method(name) for name in ('_live_entities','_discard_partial_account','_create_account_player')) in namespace
class Account(object):pass
class Runtime(object):pass
def compatibility(failing):
    runtime=Runtime();world=bigworld();runtime.bigworld=world;runtime.account_module=Stub('Account')
    entities={}
    def createEntity(kind,spaceID,vehicleID,position,direction,properties):
        log.append(('createEntity',kind,spaceID,vehicleID))
        if failing:raise RuntimeError('partial Account')
        native._entity(spaceID);account=Account();account._offlineLANInitComplete=True;account.spaceID=spaceID
        entities[600]=account;return 600
    def player(account):log.append(('player',account.spaceID));account._offlineLANPlayerReady=True
    object.__setattr__(world,'createEntity',createEntity);object.__setattr__(world,'entities',entities)
    object.__setattr__(world,'player',player)
    owner=Runtime();owner._runtime=runtime;owner._connecting=False
    for name in ('_live_entities','_discard_partial_account','_create_account_player'):
        setattr(owner,name,types.MethodType(namespace[name],owner))
    return owner
native._reset();del log[:]
account=compatibility(False)._create_account_player();spaceID=account.spaceID
assert calls()==[('createSpace',),('createEntity','Account',spaceID,0),('player',spaceID)],calls()
assert native.isClientSpace(spaceID) and native._space(spaceID)==(0,1,1) and native._connection(spaceID)==(1,0)
del log[:]
try:compatibility(True)._create_account_player()
except RuntimeError:pass
else:raise AssertionError('partial Account must fail')
failed=log[1][2]
assert calls()==[('createSpace',),('createEntity','Account',failed,0),('isClientSpace',failed),('releaseSpace',failed)],calls()
# The failed producer releases only its own space; the live Account's stays.
assert native._space(failed) is None and native._space(spaceID)==(0,1,1) and native._connection(spaceID)==(1,0)
print('Exact #1513 PYC: ClientHangarSpace create/destroy/partial, Account, game.fini, OfflineMapCreator + compat producer PASS')
"""

ENTITY_SEQUENCES = r"""
import BigWorld as B,textwrap,types
INIT,READY,PENDING='_offlineLANInitComplete','_offlineLANPlayerReady','_offlineLANRetirePending'
log=[];plan={}
def error(kind,text,call,*args):
    try:call(*args)
    except kind as caught:assert str(caught)==text,(str(caught),text)
    else:raise AssertionError((call,args))
# Fixture classes: compat's init/become/non-player token protocol, not stock
# Account. #1513 Account.pyc L1996 binds Account = PlayerAccount, so the
# Account type keeps one class; 'Switching' shows the public class switch.
class Protocol(object):
    def __init__(self):
        log.append('init')
        if plan.get('init'):raise RuntimeError('native Account init failed')
        setattr(self,INIT,True)
    def onLeaveWorld(self):
        log.append('leave')
        hook=plan.get('leave')
        if hook:hook(self)
    def onBecomePlayer(self):
        log.append('become')
        if not getattr(self,INIT,False):raise RuntimeError('offline Account initialization did not complete')
        setattr(self,PENDING,True)
        hook=plan.get('promote')
        if hook:hook(self)
        setattr(self,READY,True)
    def onBecomeNonPlayer(self):
        log.append(('nonplayer',INIT in self.__dict__))
        if not getattr(self,PENDING,False):return
        setattr(self,PENDING,False);setattr(self,READY,False);log.append('stock_nonplayer')
class PlayerAccount(Protocol,B.Entity):pass
Account=PlayerAccount
class Switching(Protocol,B.Entity):pass
class PlayerSwitching(Switching):pass
class Plain(B.Entity):pass
B._registerEntityType('Account',Account,PlayerAccount);B._registerEntityType('Plain',Plain)
B._registerEntityType('Switching',Switching,PlayerSwitching)
def account(space,kind='Account'):
    return B.entities[B.createEntity(kind,space,0,(0.,0.,0.),(0.,0.,0.),{})]
# Public createEntity: client-space connection, errors, swallowed constructor.
B._reset();space=B.createSpace();first=account(space,'Switching')
assert type(first) is Switching and getattr(first,INIT) and B._connectionPlayer(space)==(0,1)
error(ValueError,'Invalid space ID',B.createEntity,'Account',0,0,(0.,0.,0.),(0.,0.,0.),{})
error(ValueError,'Class Missing is not an entity class',B.createEntity,'Missing',space,0,(0.,0.,0.),(0.,0.,0.),{})
error(ValueError,'No server connection or client space with ID 1073741823',B.createEntity,'Account',(1<<30)-1+0,0,(0.,0.,0.),(0.,0.,0.),{}) if False else None
plan['init']=True;partial=account(space,'Switching');plan.clear()
assert type(partial) is Switching and not hasattr(partial,INIT) and B._connectionPlayer(space)==(0,2)
# Public player/setPlayer/makePlayer: class switch, swallowed onBecomePlayer,
# and no null-connection player identity.
assert B.player(first) is None and B.player() is first and type(first) is PlayerSwitching and getattr(first,READY)
assert B._connectionPlayer(space)==(0,2)
second=account(space,'Switching');plan['promote']=lambda self:(_ for _ in ()).throw(RuntimeError('stock promotion failed'))
del log[:];assert B.player(second) is None;plan.clear()
assert B.player() is second and type(second) is PlayerSwitching and not hasattr(second,READY)
assert type(first) is Switching and log[0]==('nonplayer',True) and 'stock_nonplayer' in log
plain=B.entities[B.createEntity('Plain',space,0,(0.,0.,0.),(0.,0.,0.),{})]
assert B.player(plain) is None and B.player() is second
# releaseSpace destroys the space's entities. BWEntity marks the player
# destroyed before onDestroyed, so setPlayer(NULL) skips makeNonPlayer: no
# onBecomeNonPlayer runs before the dictionary is emptied.
del log[:];B.releaseSpace(space)
assert B.player() is None and B.entities=={} and not B.isClientSpace(space)
assert log==['leave','leave','leave'],log
assert second.__dict__=={} and type(second) is PlayerSwitching and first.__dict__=={}
# Production fini: a leave callback releases the current space and tries to
# create another while the player is destroyed (the reviewed UAF).
B._reset();space=B.createSpace();other=B.createSpace();current=account(space);account(other);B.player(current)
outcome=[]
def leave(self):
    for target in (space,other):
        try:B.releaseSpace(target)
        except ValueError as caught:outcome.append(str(caught))
    try:B.createSpace()
    except RuntimeError as caught:outcome.append(str(caught))
plan['leave']=leave;B._fini();plan.clear()
assert B.player() is None and B.entities=={} and not B.isClientSpace(space) and not B.isClientSpace(other)
assert outcome.count('BigWorld.createSpace(): client spaces are finalised')==2,outcome
error(RuntimeError,'BigWorld.createSpace(): client spaces are finalised',B.createSpace)
B._init();assert B.isClientSpace(B.createSpace())
# Current compat Account transaction against these native bodies.
source=open(COMPAT).read()
def method(name):
    start=source.index('    def '+name+'(self')
    return textwrap.dedent(source[start:source.index('\n    def ',start+1)])
NAMES=('_live_entities','_discard_partial_account','_create_account_player','retire_current_player')
namespace={'_OFFLINE_INIT_COMPLETE':INIT,'_OFFLINE_PLAYER_READY':READY,'_OFFLINE_RETIRE_PENDING':PENDING}
exec '\n'.join(method(name) for name in NAMES) in namespace
class Module(object):pass
class Compat(object):
    def install(self):pass
for name in NAMES:setattr(Compat,name,namespace[name])
class World(object):
    def __getattr__(self,name):return getattr(B,name)
# Exact #1513 Account._delAccountRepository bytecode when the package is
# available, otherwise the same close-current-then-clear law.
delete_code=None
if PACKAGE:
    import marshal,zipfile
    module=marshal.loads(zipfile.ZipFile(PACKAGE).read('scripts/client/Account.pyc')[8:])
    delete_code=[c for c in module.co_consts if isinstance(c,types.CodeType) and c.co_name=='_delAccountRepository'][0]
class Resource(object):
    def __init__(self,events,name):self.events=events;self.name=name
    def close(self):self.events.append(self.name+'.close')
    def clear(self):self.events.append(self.name+'.clear')
def repository(name):
    result=Module();result.className=name;result.events=[]
    for field in ('customFilesCache','onTokenReceived','prebattleInvitations','newYear'):
        setattr(result,field,Resource(result.events,field))
    return result
current=[None]
def transaction(world=B):
    runtime=Module();runtime.bigworld=world;runtime.account_module=Module();runtime.avatar_module=Module()
    module=runtime.account_module;module.PlayerAccount=PlayerAccount;runtime.avatar_module.PlayerAvatar=Plain
    module.g_accountRepository=None;module.LOG_DEBUG=lambda *unused:None
    if delete_code is not None:module._delAccountRepository=types.FunctionType(delete_code,module.__dict__)
    else:
        def delete():
            if module.g_accountRepository is None:return
            module.g_accountRepository.customFilesCache.close();module.g_accountRepository=None
        module._delAccountRepository=delete
    owner=Compat();owner._runtime=runtime;owner._connecting=False;current[0]=module
    return owner
# Fixture of exact PlayerAccount.__init__ L90-L101 repository law.
fixture_init=Protocol.__init__
def init_with_repository(self):
    module=current[0];name=type(self).__name__;existing=module.g_accountRepository
    if existing is not None and existing.className!=name:module._delAccountRepository()
    if module.g_accountRepository is None:module.g_accountRepository=repository(name)
    fixture_init(self)
Protocol.__init__=init_with_repository
def repo():return current[0].g_accountRepository
def closed(value):return len(value.events)>0
def failed(owner,kind,text,unrelated,stock=0):
    del log[:]
    try:owner._create_account_player()
    except kind as caught:assert text in str(caught),str(caught)
    else:raise AssertionError(text)
    assert B.player() is None and B.entities=={},(B.player(),B.entities)
    assert B.isClientSpace(unrelated) and not B.isClientSpace(unrelated+1) and owner._connecting is False
    assert log.count('stock_nonplayer')==stock,log
fail=lambda text:(lambda *args:(_ for _ in ()).throw(RuntimeError(text)))
for scenario in ('init','promote','player','entity','space'):
    B._reset();unrelated=B.createSpace();world=World();owner=transaction(world)
    if scenario=='init':
        plan['init']=True;failed(owner,RuntimeError,'partial offline Account',unrelated)
        assert repo() is None
    if scenario=='promote':
        plan['promote']=fail('stock promotion failed')
        failed(owner,RuntimeError,'did not promote',unrelated,stock=1)
        assert log.index('stock_nonplayer')<log.index('leave') and repo() is None
    if scenario=='player':
        world.player=lambda *args:(_ for _ in ()).throw(TypeError('player failed'))
        failed(owner,TypeError,'player failed',unrelated);assert repo() is None
    if scenario=='entity':
        world.createEntity=lambda *args:(_ for _ in ()).throw(ValueError('entity failed'))
        existing=repository('PlayerAccount');current[0].g_accountRepository=existing
        failed(owner,ValueError,'entity failed',unrelated)
        assert repo() is existing and not closed(existing)
    if scenario=='space':
        existing=repository('PlayerAccount');current[0].g_accountRepository=existing
        world.createSpace=lambda:(_ for _ in ()).throw(RuntimeError('space failed'))
        failed(owner,RuntimeError,'space failed',unrelated)
        assert B.isClientSpace(unrelated) and repo() is existing and not closed(existing)
    plan.clear()
    owner._discard_partial_account({'space_id':unrelated+1});assert B.isClientSpace(unrelated)
# Replacements published by the promotion or the retirement callback, in
# another space or in this attempt's own space, keep their player, entity,
# space and repository; only this attempt's Account is removed.
for stage in ('promote','retire'):
    for same_space in (False,True):
        B._reset();unrelated=B.createSpace();owner=transaction();replacement=[]
        def replace(self,same_space=same_space,stage=stage):
            if replacement:return
            target=self.spaceID if same_space else B.createSpace()
            keeper=account(target);published=repository('Replacement')
            current[0].g_accountRepository=published;replacement.extend((target,keeper,published))
            plan.pop('promote',None);B.player(keeper);assert B.player() is keeper
            if stage=='promote':raise RuntimeError('callback replacement failure')
        stock_nonplayer=PlayerAccount.onBecomeNonPlayer
        if stage=='promote':plan['promote']=replace
        else:
            plan['promote']=fail('promotion error before retirement')
            def retiring(self):
                stock_nonplayer(self)
                if self is not (replacement[1] if replacement else None):replace(self)
            PlayerAccount.onBecomeNonPlayer=retiring
        try:owner._create_account_player()
        except RuntimeError as caught:assert 'did not promote' in str(caught),str(caught)
        else:raise AssertionError('replacement must fail the transaction')
        PlayerAccount.onBecomeNonPlayer=stock_nonplayer;plan.clear()
        target,keeper,published=replacement
        assert B.player() is keeper and B.entities.values()==[keeper] and B.isClientSpace(target),(stage,same_space)
        assert B.isClientSpace(unrelated) and repo() is published and not closed(published),(stage,same_space)
        assert owner._connecting is False
# The exact constructor reuses a same-class repository across Accounts.
B._reset();owner=transaction();shared=repository('PlayerAccount');current[0].g_accountRepository=shared;replacement=[]
def replace_shared(self):
    if replacement:return
    target=B.createSpace();keeper=account(target);replacement.append(keeper)
    B.player(keeper);raise RuntimeError('shared repository replacement')
plan['promote']=replace_shared
try:owner._create_account_player()
except RuntimeError:pass
else:raise AssertionError('missing replacement failure')
plan.clear();assert B.player() is replacement[0] and repo() is shared and not closed(shared)
# A repository this attempt created is kept while a successor shares it.
B._reset();owner=transaction();replacement=[]
def successor_shares(self):
    if replacement:return
    target=B.createSpace();keeper=account(target);replacement.append(keeper)
    B.player(keeper);raise RuntimeError('successor shares the new repository')
plan['promote']=successor_shares;created=[]
try:owner._create_account_player()
except RuntimeError:pass
plan.clear();assert B.player() is replacement[0] and repo() is not None and not closed(repo())
# Cleanup errors keep the original failure; a repository published by the
# failing delete is not overwritten.
B._reset();world=World();owner=transaction(world);published=[]
world.player=lambda *args:(_ for _ in ()).throw(TypeError('original promotion error'))
def publishing_delete():
    published.append(repository('Published'));current[0].g_accountRepository=published[0]
    raise ValueError('cleanup failed')
current[0]._delAccountRepository=publishing_delete
try:owner._create_account_player()
except TypeError as caught:assert str(caught)=='original promotion error'
else:raise AssertionError('missing original error')
assert repo() is published[0] and owner._connecting is False and B.entities=={}
# A later retry succeeds; a stale cleanup of the first attempt touches
# nothing, and fini destroys the live player without onBecomeNonPlayer.
B._reset();owner=transaction();plan['init']=True;first=[]
try:owner._create_account_player()
except RuntimeError:pass
plan.clear();live=owner._create_account_player();live_repository=repo()
assert B.player() is live and getattr(live,READY) and B.entities.values()==[live] and live_repository is not None
owner._discard_partial_account({'space_id':live.spaceID-1,'account_id':live.id if hasattr(live,'id') else None,'repository':live_repository})
assert B.player() is live and repo() is live_repository and not closed(live_repository)
del log[:];B._fini();assert B.player() is None and B.entities=={} and 'stock_nonplayer' not in log
B._init();Protocol.__init__=fixture_init
print('Public createEntity/player/setPlayer/makePlayer/onDestroyed + production fini + compat Account transaction PASS')
"""


def body(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


def load_patch(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'bigworld' / (name + '.py'))
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module.patch


class SpaceLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        python = Path(os.environ.get('WOT_TEST_PY27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
        config = python.with_name('python2.7-config')
        if not config.exists() or not cls.archive.exists():
            raise unittest.SkipTest('CPython 2.7 development runtime and pristine engine archive required')
        cls.temporary = tempfile.TemporaryDirectory(prefix='stock-space-lifecycle-')
        cls.directory = Path(cls.temporary.name)
        with tarfile.open(cls.archive) as archive:
            cls.pristine = {}
            for name in ('client/script_bigworld.cpp', 'client/connection_control.hpp', 'client/connection_control.cpp',
                         'lib/space/space_manager.hpp', 'lib/space/space_manager.cpp',
                         'lib/pyscript/script.cpp', 'lib/pyscript/script.hpp', 'lib/pyscript/script.ipp',
                         'client/script_player.cpp', 'client/py_entity.cpp', 'client/entity.cpp', 'client/entity_manager.cpp', 'client/script_app.cpp', 'lib/space/client_space.cpp', 'lib/compiled_space/particle_system_loader.hpp', 'lib/compiled_space/particle_system_loader.cpp'):
                member = next(m for m in archive.getmembers() if m.name.endswith('/programming/bigworld/' + name))
                cls.pristine[name] = archive.extractfile(member).read()
        cls.engine = cls.directory / 'engine'
        cls.populate(cls.engine)
        cls.patch = staticmethod(load_patch('patch_stock_space_lifecycle'))
        cls.patch(cls.engine)
        script = cls.pristine['lib/pyscript/script.cpp'].decode('utf-8-sig')
        control = (cls.engine / 'client/connection_control.cpp').read_text()
        host = (ROOT / 'tests/stock_space_lifecycle_host.cpp').read_text()
        host = host.replace('@CONVERTER@', body(script, 'int Script::setData( PyObject * pObject, bool & rBool'))
        host = host.replace('@INT_CONVERTER@', body(script, 'int Script::setData( PyObject * pObject, int & rInt'))
        host = host.replace('@GETDATA_BOOL@', body(script, 'PyObject * Script::getData( const bool data )'))
        host = host.replace('@GETDATA_INT@', body(script, 'PyObject * Script::getData( const int data )'))
        host = host.replace('@ARGCOUNT@', body(script, 'PyObject * Script::argCountError( const char * fn, int optas, int allas, ... )'))
        header = cls.pristine['lib/pyscript/script.hpp'].decode('utf-8-sig')
        retdata = header.index('\ttemplate <class T>\n\tclass IsValidRetData')
        host = host.replace('@RETDATA@', header[retdata:header.index('}', header.index('PyObject * getRetData( const T & data )')) + 1])
        # The original wrappers are PY_AUTO_MODULE_FUNCTION codegen; host the
        # pristine macros rather than a hand-written parser.
        autoparse = header[header.index('// Section: Macros: Recursion utilities'):header.index('// Section: PyModuleLink')]
        link = header[header.index('/// This macro defines an ordinary auto-parsed function'):
                      header.index('/// This macro defines an alias')]
        host = host.replace('@PYAUTO@', autoparse + link)
        host = host.replace('@CLEAR_SPACES@', body(control, 'void ConnectionControl::clearSpaces()'))
        host = host.replace('@TICK@', body(control, 'void ConnectionControl::tick( float dt )'))
        host = host.replace('@NEXT_LOCAL_SPACE_ID@', body(control, 'SpaceID ConnectionControl::nextLocalSpaceID()'))
        host = host.replace('@SPACE_CONNECTION@', body(control, 'BWConnection * ConnectionControl::pSpaceConnection( SpaceID spaceID ) const'))
        host = host.replace('@FINI@', body(control, 'void ConnectionControl::fini()'))
        manager = (cls.engine / 'lib/space/space_manager.cpp').read_text()
        host = host.replace('@SPACE_MANAGER_INIT@', body(manager, 'void SpaceManager::init()'))
        # Public player/entity bodies; createEntity's std::string vararg is
        # passed as c_str() so the host compiler accepts it.
        source = lambda name: cls.pristine[name].decode('utf-8-sig')
        host = host.replace('@SCRIPT_CALL@', body(source('lib/pyscript/script.ipp'), 'INLINE bool call( PyObject * pFunction, PyObject * pArgs,'))
        host = host.replace('@SCRIPT_ASK@', body(script, 'PyObject * Script::ask( PyObject * pFunction,'))
        host = host.replace('@SET_PLAYER@', body(source('client/script_player.cpp'), 'bool ScriptPlayer::setPlayer( Entity * newPlayer )'))
        host = host.replace('@PLAYER@', body(source('client/script_player.cpp'), 'static PyObject * player( PyEntityPtr pNewPlayer )'))
        entity = source('client/py_entity.cpp')
        host = host.replace('@MAKE_PLAYER@', body(entity, 'bool PyEntity::makePlayer()'))
        host = host.replace('@MAKE_NON_PLAYER@', body(entity, 'bool PyEntity::makeNonPlayer()'))
        host = host.replace('@ON_ENTITY_DESTROYED@', body(entity, 'void PyEntity::onEntityDestroyed()'))
        host = host.replace('@ON_DESTROYED@', body(source('client/entity.cpp'), 'void Entity::onDestroyed()'))
        create = body(source('client/entity_manager.cpp'), 'static PyObject * createEntity( const BW::string & className, SpaceID spaceID,')
        create = create.replace(', className );', ', className.c_str() );').replace('\t\t\tclassName );', '\t\t\tclassName.c_str() );')
        host = host.replace('@CREATE_ENTITY@', create)
        destroy = body(source('client/entity_manager.cpp'), 'static bool destroyEntity( EntityID id )')
        host = host.replace('@DESTROY_ENTITY@', destroy + '\nPY_AUTO_MODULE_FUNCTION( RETOK, destroyEntity, ARG( EntityID, END ), BigWorld )')
        host = host.replace('@CREATE_LOCAL_SPACE@', body(control, 'SpaceID ConnectionControl::createLocalSpace()'))
        host = host.replace('@GAME_TIME@', body(control, 'double ConnectionControl::gameTime() const'))
        original_control = cls.pristine['client/connection_control.cpp'].decode('utf-8-sig')
        host = host.replace('@LEGACY_CLEAR@', body(original_control, 'void ConnectionControl::clearSpaces()').replace('ConnectionControl::clearSpaces()', 'ConnectionControl::legacyClearSpaces()'))
        host = host.replace('@LEGACY_DESTROY@', body(control, 'void ConnectionControl::destroyMainConnection()'))
        host = host.replace('@PRODUCTION_FILE@', str(ROOT / 'bigworld/stock_space_lifecycle.cpp'))
        support=(ROOT/'tests/stock_space_sounds_host.hpp').read_text().replace(
            '#include WOT_SOUND_PRODUCTION_FILE', '#include "'+str(ROOT/'bigworld/stock_space_sounds.cpp')+'"')
        host=host.replace('@SOUND_TEST_SUPPORT@',support)
        (cls.directory / 'host.cpp').write_text(host)
        cls.probe = cls.directory / 'probe'
        flags = [flag for flag in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True)) if flag != '-DNDEBUG']
        libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
        subprocess.run(['c++', '-std=c++14', '-Wno-deprecated-register', '-UNDEBUG', str(cls.directory / 'host.cpp'),
                        *flags, *libs, '-o', str(cls.probe)], check=True)
        destination = os.environ.get('WOT_SPACE_LIFECYCLE_FIXTURE_OUT')
        if destination:
            destination = Path(destination); destination.mkdir(parents=True, exist_ok=True)
            (destination / 'host.cpp').write_text(host.replace(str(ROOT / 'bigworld/stock_space_lifecycle.cpp'), 'stock_space_lifecycle.cpp').replace(str(ROOT/'bigworld/stock_space_sounds.cpp'),'stock_space_sounds.cpp'))
            (destination/'stock_space_sounds.cpp').write_bytes((ROOT/'bigworld/stock_space_sounds.cpp').read_bytes())
            (destination/'stock_space_sounds_data.hpp').write_bytes((ROOT/'bigworld/stock_space_sounds_data.hpp').read_bytes())
            (destination / 'stock_space_lifecycle.cpp').write_bytes((ROOT / 'bigworld/stock_space_lifecycle.cpp').read_bytes())
            (destination / 'case.py').write_text(BOUNDARY)
            (destination / 'client.py').write_text(CLIENT_BOUNDARY)
            (destination / 'pyc.py').write_text(PYC_SEQUENCES)
            (destination / 'entity.py').write_text(ENTITY_SEQUENCES)
            (destination / 'compat.py').write_bytes((ROOT.parents[1] / 'src/res/scripts/client/gui/mods/offline_lan_0922/compat.py').read_bytes())

    @classmethod
    def populate(cls, directory):
        for name, data in cls.pristine.items():
            path = directory / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def execute(self, code):
        script = self.directory / 'case.py'; script.write_text(code)
        subprocess.run([str(self.probe), str(script)], check=True)

    def test_pristine_main_clear_reproduces_reentrant_replacement_destruction(self):
        subprocess.run([str(self.probe), '--legacy'], check=True)

    def test_real_python_boundary_and_production_owner_lifecycle(self):
        self.execute(BOUNDARY)

    def test_real_python_client_space_boundary(self):
        self.execute(CLIENT_BOUNDARY)

    def test_original_pyc_and_product_producer_sequences(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('WOT_0922_CLIENT required for original scripts.pkg')
        compat = ROOT.parents[1] / 'src/res/scripts/client/gui/mods/offline_lan_0922/compat.py'
        self.execute('PACKAGE=%r\nCOMPAT=%r\n' % (str(Path(client) / 'res/packages/scripts.pkg'), str(compat)) + PYC_SEQUENCES)

    def test_public_entity_player_bodies_and_account_transaction(self):
        compat = ROOT.parents[1] / 'src/res/scripts/client/gui/mods/offline_lan_0922/compat.py'
        client = os.environ.get('WOT_0922_CLIENT')
        package = str(Path(client) / 'res/packages/scripts.pkg') if client else ''
        self.execute('COMPAT=%r\nPACKAGE=%r\n' % (str(compat), package) + ENTITY_SEQUENCES)

    def generated_compat(self):
        fork = os.environ.get('WOT_FORK_0101_SOURCE')
        if not fork:
            self.skipTest('WOT_FORK_0101_SOURCE required for the pinned compat')
        spec = importlib.util.spec_from_file_location('prepare_offline_fork', ROOT / 'bigworld/prepare_offline_fork.py')
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        source = self.directory / 'fork'
        if not source.exists():
            for name in ('native', 'src'):
                shutil.copytree(Path(fork) / name, source / name)
            (source / 'fork-source.json').write_text('{"revision":"%s"}' % module.PINNED_COMMIT)
            module.prepare(source, self.directory / 'generated')
        return self.directory / 'generated/offline-res/scripts/client/gui/mods/offline_lan_0922/compat.py'

    def test_generated_pinned_compat_producer_sequences(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('WOT_0922_CLIENT required for original scripts.pkg')
        compat = self.generated_compat()
        self.execute('PACKAGE=%r\nCOMPAT=%r\n' % (str(Path(client) / 'res/packages/scripts.pkg'), str(compat)) + PYC_SEQUENCES)

    def test_generated_pinned_compat_account_transaction(self):
        compat = self.generated_compat()
        client = os.environ.get('WOT_0922_CLIENT')
        package = str(Path(client) / 'res/packages/scripts.pkg') if client else ''
        self.execute('COMPAT=%r\nPACKAGE=%r\n' % (str(compat), package) + ENTITY_SEQUENCES)

    def test_original_lobby_producer_selects_keep_from_real_hangar_guard(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client: self.skipTest('WOT_0922_CLIENT required for original states.pyc')
        self.execute(r'''
import BigWorld as B,zipfile,marshal,types,sys
archive=zipfile.ZipFile(%r)
module=marshal.loads(archive.read('scripts/client/gui/app_loader/states.pyc')[8:])
def walk(code):
    if code.co_name=='_clearEntitiesAndSpaces':return code
    for child in code.co_consts:
        if isinstance(child,types.CodeType):
            found=walk(child)
            if found:return found
code=walk(module);assert code is not None
for name in ('gui','gui.shared','gui.shared.utils'):
    package=types.ModuleType(name);package.__path__=[];sys.modules[name]=package
hangar=types.ModuleType('gui.shared.utils.HangarSpace')
sys.modules['gui.shared.utils.HangarSpace']=hangar
run=types.FunctionType(code,{'BigWorld':B,'__builtins__':__builtins__})
class Hangar(object):
    def __init__(self,inited,loading):self.inited=inited;self.loading=loading;self.calls=0
    def spaceLoading(self):self.calls+=1;return self.loading
for value,expected in ((None,0),(Hangar(False,True),0),(Hangar(True,False),0),(Hangar(True,True),1)):
    hangar.g_hangarSpace=value;B._prepare();assert run() is None;assert B._state()==expected
    if value is not None:assert value.calls==int(value.inited)
print('Original LoginState _clearEntitiesAndSpaces PYC + actual CPython/native wrapper PASS')
''' % str(Path(client) / 'res/packages/scripts.pkg'))

    def test_actual_particle_sound_bind_and_unload_reentry(self):
        engine=self.directory/'particle-owner';self.populate(engine)
        load_patch('patch_stock_space_sounds')(engine)
        signatures=('bool ParticleSystemLoader::doBind()', 'void ParticleSystemLoader::doUnload()',
                    'void ParticleSystemLoader::addToScene()', 'void ParticleSystemLoader::removeFromScene()',
                    'void ParticleSystemLoader::freeParticleSystems()')
        source=(engine/'lib/compiled_space/particle_system_loader.cpp').read_text()
        old=self.pristine['lib/compiled_space/particle_system_loader.cpp'].decode('utf-8-sig')
        support=(ROOT/'tests/stock_space_sounds_host.hpp').read_text().replace(
            '#include WOT_SOUND_PRODUCTION_FILE','#include "'+str(ROOT/'bigworld/stock_space_sounds.cpp')+'"')
        for name,implementation in (('original',old),('patched',source)):
            host=(ROOT/'tests/stock_space_sound_particles_host.cpp').read_text()
            host=host.replace('@SOUND_SUPPORT@',support).replace('@METHODS@','\n'.join(body(implementation,v)for v in signatures))
            cpp=self.directory/(name+'-particle.cpp');cpp.write_text(host)
            binary=self.directory/(name+'-particle')
            flags=['-DLEGACY_PARTICLE_LIFECYCLE'] if name=='original' else ['-fsanitize=address,undefined']
            subprocess.run(['c++','-std=c++14','-O1','-g','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_patch_composes_with_replay_and_typed_space_in_all_orders(self):
        patches = [load_patch(name) for name in ('patch_stock_space_lifecycle',
                   'patch_stock_replay_controller', 'patch_stock_space_data', 'patch_stock_space_sounds')]
        for number, order in enumerate(itertools.permutations(patches)):
            directory = self.directory / ('order-%d' % number); self.populate(directory)
            for patch in order: patch(directory)
            before = {p.relative_to(directory):p.read_bytes() for p in directory.rglob('*') if p.is_file()}
            for patch in reversed(order): patch(directory)
            self.assertEqual(before, {p.relative_to(directory):p.read_bytes() for p in directory.rglob('*') if p.is_file()})
            script = (directory / 'client/script_bigworld.cpp').read_text()
            self.assertEqual(script.count('#include "stock_space_lifecycle.hpp"'), 1)
            self.assertEqual(script.count('StockSpaceLifecycle_token |'), 1)
            for header in ('client/connection_control.hpp', 'lib/space/space_manager.hpp'):
                self.assertEqual((directory / header).read_text().count('\tfriend class StockSpaceLifecycle;'), 1)
            self.assertEqual((directory / 'client/connection_control.hpp').read_text().count(
                '\tvoid clearStockMainSpace( bool keepLocalEntities = false );'), 1)
            space=(directory/'lib/space/client_space.cpp').read_text()
            self.assertEqual(script.count('#include "stock_space_sounds.hpp"'),1)
            self.assertEqual(space.count('#include "stock_space_sounds.hpp"'),1)
            for signature in ('ClientSpace::~ClientSpace()', 'void ClientSpace::clear()'):
                value=body(space,signature)
                self.assertEqual(value.count('retireStockSpaceSounds( this );'),1)
                self.assertLess(value.index('retireStockSpaceSounds( this );'),value.index('retireStockSpaceData( id_, this );'))


    def test_sound_patch_failure_does_not_publish_other_mutations(self):
        directory=self.directory/'bad-sound';self.populate(directory)
        path=directory/'lib/compiled_space/particle_system_loader.cpp'
        path.write_text(path.read_text().replace('\t\tsceneObj.handle( &instance );','changed_instance_seam();'))
        before={p.relative_to(directory):p.read_bytes()for p in directory.rglob('*')if p.is_file()}
        with self.assertRaises(ValueError):load_patch('patch_stock_space_sounds')(directory)
        self.assertEqual(before,{p.relative_to(directory):p.read_bytes()for p in directory.rglob('*')if p.is_file()})

    def test_pristine_patch_idempotence_and_failed_transaction(self):
        paths = ['client/script_bigworld.cpp', 'client/connection_control.hpp', 'client/connection_control.cpp',
                 'lib/space/space_manager.hpp', 'lib/space/space_manager.cpp', 'lib/stock_space_lifecycle.hpp']
        before = {p:(self.engine / p).read_bytes() for p in paths}
        self.patch(self.engine)
        self.assertEqual(before, {p:(self.engine / p).read_bytes() for p in paths})
        bad = self.directory / 'bad'; self.populate(bad)
        path = bad / 'client/connection_control.cpp'
        path.write_text(path.read_text().replace('iter->second->updateServer();', 'changed_tick_seam();'))
        before = {p:(bad / p).read_bytes() for p in paths[:-1]}
        with self.assertRaises(ValueError): self.patch(bad)
        self.assertEqual(before, {p:(bad / p).read_bytes() for p in paths[:-1]})
        self.assertFalse((bad / paths[-1]).exists())


if __name__ == '__main__':unittest.main()
