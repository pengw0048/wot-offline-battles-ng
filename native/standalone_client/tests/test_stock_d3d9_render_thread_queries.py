"""Execute production query, reference-retirement and reset bodies with queued native failures."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def body(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 0
    for end in range(brace, len(source)):
        depth += {'{': 1, '}': -1}.get(source[end], 0)
        if not depth:
            return source[start:end + 1]
    raise AssertionError('Unterminated production body')


HOST = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>
using LONG=int;using ULONG=unsigned;using DWORD=std::uint32_t;using BOOL=int;
using UINT64=std::uint64_t;using HRESULT=std::int32_t;
enum D3DQUERYTYPE {D3DQUERYTYPE_EVENT=1,D3DQUERYTYPE_OCCLUSION,
 D3DQUERYTYPE_TIMESTAMP,D3DQUERYTYPE_TIMESTAMPFREQ,D3DQUERYTYPE_TIMESTAMPDISJOINT,Unknown=77,Empty=78};
constexpr HRESULT S_OK=0,D3D_OK=0,S_FALSE=1,D3DERR_INVALIDCALL=-1,D3DERR_DEVICELOST=-2,E_OUTOFMEMORY=-3;
constexpr DWORD D3DISSUE_BEGIN=1,D3DISSUE_END=2,D3DGETDATA_FLUSH=1;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
#define STDMETHODCALLTYPE
struct SRWLOCK {bool held=false;};
#define SRWLOCK_INIT {}
void AcquireSRWLockExclusive(SRWLOCK*p){assert(!p->held);p->held=true;}
void ReleaseSRWLockExclusive(SRWLOCK*p){assert(p->held);p->held=false;}
void AcquireSRWLockShared(SRWLOCK*p){AcquireSRWLockExclusive(p);}
void ReleaseSRWLockShared(SRWLOCK*p){ReleaseSRWLockExclusive(p);}
LONG InterlockedIncrement(volatile LONG*p){return ++*p;}
LONG InterlockedDecrement(volatile LONG*p){return --*p;}
LONG InterlockedCompareExchange(volatile LONG*p,LONG value,LONG compare){LONG old=*p;if(old==compare)*p=value;return old;}
struct IDirect3DQuery9 {
 virtual ~IDirect3DQuery9(){}virtual ULONG AddRef()=0;virtual ULONG Release()=0;
 virtual D3DQUERYTYPE GetType()=0;virtual DWORD GetDataSize()=0;
 virtual HRESULT Issue(DWORD)=0;virtual HRESULT GetData(void*,DWORD,DWORD)=0;
};
static int liveNative=0;
struct NativeQuery:IDirect3DQuery9 {
 ULONG refs=1;D3DQUERYTYPE type;DWORD size,value=42,seenFlags=0;
 HRESULT issueResult=S_OK,beginResult=S_OK,dataResult=S_FALSE;unsigned polls=0;
 std::function<void()> duringPoll;
 explicit NativeQuery(D3DQUERYTYPE t):type(t),size(t==Unknown?32:t==Empty?0:4){++liveNative;}
 ~NativeQuery(){--liveNative;}ULONG AddRef()override{return ++refs;}
 ULONG Release()override{ULONG n=--refs;if(!n)delete this;return n;}
 D3DQUERYTYPE GetType()override{return type;}DWORD GetDataSize()override{return size;}
 HRESULT Issue(DWORD flags)override{return flags==D3DISSUE_BEGIN?beginResult:issueResult;}
 HRESULT GetData(void*d,DWORD s,DWORD f)override{
  ++polls;seenFlags=f;if(duringPoll){auto call=duringPoll;duringPoll=nullptr;call();}
  assert((!s || (d && s==size)) && (f==0 || f==D3DGETDATA_FLUSH));
  if(dataResult==S_OK && s){memset(d,0,s);memcpy(d,&value,std::min<DWORD>(s,4));}return dataResult;
 }
};
struct NativeDevice {HRESULT result=S_OK;bool nullSuccess=false;NativeQuery*last=nullptr;
 HRESULT CreateQuery(D3DQUERYTYPE t,IDirect3DQuery9**o){if(!o)return result;
  *o=nullptr;if(FAILED(result))return result;if(!nullSuccess)*o=last=new NativeQuery(t);return result;}
};
struct WQuery;
namespace rt {
static bool main=true,render=false,parked=false,g_inlineRetire=false;
static unsigned syncs=0,wakes=0;
static SRWLOCK g_mapLock;static std::unordered_map<void*,void*>g_identity;
static void(*g_onDestroyMain)(void*)=nullptr;
static void*g_vtbls[16];static int g_vtblCount=0;
static std::deque<std::function<void()>>commands;
static std::vector<std::function<void()>>retired;
bool onMain(){return main;}bool onRender(){return render;}bool mainOwnsDevice(){return main&&parked;}
void wake(){++wakes;}HRESULT reportResult(const char*,HRESULT h){return h;}
template<class F>void post(F call){if(mainOwnsDevice())call();else commands.push_back(call);}
void one(){auto call=commands.front();commands.pop_front();bool m=main,r=render;main=false;render=true;
 call();main=m;render=r;}
void drain(){while(!commands.empty())one();}
template<class F>auto sync(F call,const char*)->decltype(call()){
 if(render||mainOwnsDevice())return call();++syncs;drain();bool m=main,r=render;main=false;render=true;
 auto result=call();main=m;render=r;return result;
}
void retire(void(*fn)(void*),void*p){if(g_inlineRetire&&render){fn(p);return;}
 if(main){post([=]{fn(p);});return;}retired.push_back([=]{fn(p);});}
void runDeferred(){auto list=std::move(retired);retired.clear();for(auto&call:list)call();}
IDirect3DQuery9*wrapOut(void*,IDirect3DQuery9*);
}
@REF_BASE@;
static void*reuse=nullptr;
struct WQuery:RefBase<IDirect3DQuery9>{using RefBase::RefBase;
 static void*operator new(size_t n){if(reuse){void*p=reuse;reuse=nullptr;return p;}return ::operator new(n);}
 static void operator delete(void*p){if(reuse)::operator delete(p);else reuse=p;}
 D3DQUERYTYPE GetType()override;DWORD GetDataSize()override;
 HRESULT Issue(DWORD)override;HRESULT GetData(void*,DWORD,DWORD)override;
};
struct WDevice{NativeDevice*real;HRESULT CreateQuery(D3DQUERYTYPE,IDirect3DQuery9**);};
#define REAL real
@QUERIES@
namespace rt {IDirect3DQuery9*wrapOut(void*,IDirect3DQuery9*r){auto*w=new WQuery(r);initQuery(w);return w;}}
namespace sh {void clearAll(){}}
@BEFORE_RESET@
static void collect(){rt::drain();rt::runDeferred();rt::drain();assert(g_qpoll.empty());
 assert(g_qstate.empty() && liveNative==0);}
static WQuery*create(WDevice&d,D3DQUERYTYPE t=D3DQUERYTYPE_OCCLUSION){IDirect3DQuery9*q=nullptr;
 assert(d.CreateQuery(t,&q)==S_OK && q);return static_cast<WQuery*>(q);}
static void poll(){bool m=rt::main;rt::main=false;rt::render=true;pollQueries();rt::render=false;rt::main=m;}
int main(int argc,char**argv){assert(argc==2);std::string scenario=argv[1];NativeDevice native;WDevice device{&native};
 if(scenario=="create_release"){
  auto*q=create(device);assert(q->refs==2);q->Release();rt::drain();collect();
 }else if(scenario=="creation_failure" || scenario=="null_creation"){
  if(scenario=="creation_failure")native.result=D3DERR_DEVICELOST;else native.nullSuccess=true;
  auto*q=create(device);assert(q->Issue(D3DISSUE_END)==S_OK);rt::drain();
  const HRESULT error=scenario=="creation_failure"?D3DERR_DEVICELOST:D3DERR_INVALIDCALL;
  assert(q->GetData(nullptr,0,0)==error && q->Issue(D3DISSUE_END)==error);
  assert(g_qpoll.empty());q->Release();collect();
 }else if(scenario=="issue_failure" || scenario=="begin_failure"){
  auto*q=create(device);rt::drain();native.last->issueResult=D3DERR_DEVICELOST;
  if(scenario=="begin_failure"){native.last->issueResult=S_OK;native.last->beginResult=D3DERR_DEVICELOST;
   assert(q->Issue(D3DISSUE_BEGIN)==S_OK);}
  assert(q->Issue(D3DISSUE_END)==S_OK);rt::drain();assert(q->GetData(nullptr,0,0)==D3DERR_DEVICELOST);
  assert(g_qpoll.empty());q->Release();collect();
 }else if(scenario=="poll_failure"){
  auto*q=create(device);rt::drain();q->Issue(D3DISSUE_END);rt::drain();
  native.last->dataResult=D3DERR_DEVICELOST;
  assert(q->GetData(nullptr,0,D3DGETDATA_FLUSH)==S_FALSE);poll();
  assert(native.last->seenFlags==D3DGETDATA_FLUSH && q->refs==1 && g_qpoll.empty());
  assert(q->GetData(nullptr,0,0)==D3DERR_DEVICELOST);q->Release();collect();
 }else if(scenario=="generation"){
  auto*q=create(device);rt::drain();q->Issue(D3DISSUE_BEGIN);q->Issue(D3DISSUE_END);rt::drain();
  auto*r=native.last;r->dataResult=S_OK;r->duringPoll=[&]{rt::render=false;rt::main=true;
   assert(q->Issue(D3DISSUE_END)==S_OK);rt::main=false;rt::render=true;};
  poll();assert(q->GetData(nullptr,0,0)==S_FALSE);rt::drain();r->value=17;poll();
  DWORD result=0;assert(q->GetData(&result,4,0)==S_OK && result==17 && rt::syncs==0);
  q->Release();collect();
 }else if(scenario=="bulk_poll_membership"){
  std::vector<WQuery*>queries;
  for(unsigned i=0;i<1024;++i){auto*q=create(device);queries.push_back(q);
   q->Issue(D3DISSUE_BEGIN);q->Issue(D3DISSUE_END);}
  rt::drain();assert(g_qpoll.size()==queries.size() && rt::syncs==0);
  auto check=[](){for(size_t i=0;i<g_qpoll.size();++i)
   assert(g_qstate.at(g_qpoll[i].query).pollIndex==i);};check();
  // Resolving alternating items moves unresolved rows into the removed holes.
  for(unsigned i=0;i<queries.size();i+=2)static_cast<NativeQuery*>(queries[i]->real)->dataResult=S_OK;
  poll();assert(g_qpoll.size()==512);check();
  for(unsigned i=1;i<queries.size();i+=2){auto*q=queries[i];
   q->Issue(D3DISSUE_BEGIN);q->Issue(D3DISSUE_END);q->Issue(D3DISSUE_END);}
  rt::drain();assert(g_qpoll.size()==512);check();
  rt::main=false;rt::render=true;beforeReset();rt::render=false;rt::main=true;
  assert(g_qpoll.empty());for(auto*q:queries){
   assert(g_qstate.at(q).pollIndex==size_t(-1));q->Release();}collect();
 }else if(scenario=="retire_pending"){
  auto*q=create(device);rt::drain();q->Issue(D3DISSUE_END);rt::drain();assert(q->refs==2);
  q->Release();poll();collect();
 }else if(scenario=="reset_render" || scenario=="reset_parked" || scenario=="reset_live"){
  auto*q=create(device);rt::drain();q->Issue(D3DISSUE_END);rt::drain();
  const bool live=scenario=="reset_live";if(!live)q->Release();
  if(scenario=="reset_parked")rt::parked=true;else {rt::main=false;rt::render=true;}
  beforeReset();rt::parked=false;rt::render=false;rt::main=true;
  if(live){assert(q->GetData(nullptr,0,0)==D3DERR_DEVICELOST);
   assert(q->Issue(D3DISSUE_END)==D3DERR_DEVICELOST);q->Release();}collect();
 }else if(scenario=="reuse"){
  auto*q=create(device);rt::drain();q->Issue(D3DISSUE_END);rt::drain();native.last->dataResult=S_OK;poll();
  void*address=q;q->Release();collect();q=create(device);assert(q==address);
  assert(q->GetData(nullptr,0,0)==S_FALSE && g_qstate.at(q).generation==0);q->Release();collect();
 }else if(scenario=="vtable_full"){
  rt::g_vtblCount=16;IDirect3DQuery9*q=reinterpret_cast<IDirect3DQuery9*>(1);
  assert(device.CreateQuery(D3DQUERYTYPE_OCCLUSION,&q)==E_OUTOFMEMORY && !q);collect();
 }else if(scenario=="bounds"){
  auto*q=create(device);assert(q->GetType()==D3DQUERYTYPE_OCCLUSION && q->GetDataSize()==4);
  DWORD value=99;assert(q->GetData(nullptr,4,0)==D3DERR_INVALIDCALL);
  assert(q->GetData(&value,3,0)==D3DERR_INVALIDCALL && q->GetData(&value,17,0)==D3DERR_INVALIDCALL);
  assert(q->GetData(&value,4,2)==D3DERR_INVALIDCALL && q->Issue(0)==D3DERR_INVALIDCALL);
  assert(value==99 && rt::syncs==0);q->Release();collect();
 }else if(scenario=="unbuffered" || scenario=="empty"){
  auto*q=create(device,scenario=="empty"?Empty:Unknown);assert(q->GetDataSize()==(scenario=="empty"?0:32));
  q->Issue(D3DISSUE_END);native.last->dataResult=S_OK;unsigned char data[32];
  assert(q->GetData(data,q->GetDataSize(),D3DGETDATA_FLUSH)==S_OK && g_qpoll.empty());q->Release();collect();
 }else assert(false);
 assert(rt::commands.empty() && rt::retired.empty());if(reuse){::operator delete(reuse);reuse=nullptr;}
}
'''


class RenderThreadQueryTest(unittest.TestCase):
    def test_production_query_lifecycle_and_failure_controls(self):
        compiler = os.environ.get('CXX', 'c++')
        if not shutil.which(compiler):
            self.skipTest('C++ compiler required')
        source = (ROOT/'bigworld/stock_d3d9_render_thread.cpp').read_text()
        start = source.index('struct QState {')
        queries = source[start:source.index('// ------------------------------------------------------------------ state blocks', start)]
        cpp = (HOST.replace('@REF_BASE@', body(source, 'template<class I> struct RefBase : I'))
               .replace('@QUERIES@', queries)
               .replace('@BEFORE_RESET@', body(source, 'static void beforeReset()')))
        scenarios = ('create_release', 'creation_failure', 'null_creation', 'issue_failure', 'begin_failure', 'poll_failure',
                     'generation', 'bulk_poll_membership', 'retire_pending', 'reset_render', 'reset_parked', 'reset_live',
                     'reuse', 'bounds', 'vtable_full', 'unbuffered', 'empty')
        with tempfile.TemporaryDirectory(prefix='render-query-host-') as temp:
            root = Path(temp)
            variants = (
                ('fixed', cpp, scenarios, True),
                ('leaked_poll', cpp.replace('if (result == S_FALSE) { ++i; continue; }',
                                           'if (result != S_OK) { ++i; continue; }'), ('poll_failure',), False),
                ('stale_generation', cpp.replace('if (poll.generation == latest.generation)', 'if (true)'),
                 ('generation',), False),
                ('lost_begin_failure', cpp.replace(
                    'if (state.building && FAILED(state.buildingResult)) result = state.buildingResult;', ''),
                 ('begin_failure',), False),
                ('stale_poll_index', cpp.replace(
                    'if (index < g_qpoll.size()) g_qstate.at(g_qpoll[index].query).pollIndex = index;', ''),
                 ('bulk_poll_membership',), False),
                ('unleased_create', cpp.replace('w->AddRef(); // Creation itself owns a lease, even if the caller releases immediately.',
                                                '// Missing creation lease.'), ('create_release',), False),
            )
            for name, code, cases, success in variants:
                path = root/(name+'.cpp');binary = root/name;path.write_text(code)
                command = [compiler, '-std=c++17', '-O1', '-UNDEBUG']
                # MinGW CI has no ASan runtime; still execute every scenario.
                if sys.platform != 'win32':
                    command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
                result = subprocess.run(command + [str(path), '-o', str(binary)],
                                        text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                for case in cases:
                    with self.subTest(variant=name, scenario=case):
                        result = subprocess.run([str(binary), case], text=True, capture_output=True)
                        if success:
                            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
                        else:
                            self.assertNotEqual(result.returncode, 0, 'Regression control unexpectedly passed')


if __name__ == '__main__':
    unittest.main()
