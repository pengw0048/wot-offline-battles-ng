"""Compile the actual WheelsAnimator record, manager and compound linkage.

The host proves the exact #1513 angle law, stop bits, leading-wheel spline
phase, LOD gate, node binding and teardown. It does not stand in for the
Python C ABI, the compound renderer or Windows presentation.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'bigworld/stock_vehicle_wheels.cpp'

HOST = r'''
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
using uint32=std::uint32_t;
namespace BW { using string=std::string; }
struct Vector4 { float x,y,z,w; Vector4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){} };
struct Matrix {
    float m[4][4];
    static const Matrix identity;
    float* operator[](unsigned i) { return m[i]; }
    const float* operator[](unsigned i) const { return m[i]; }
    // D3DXMatrixRotationX and D3DXMatrixMultiply (row vectors, out=a*b).
    void setRotateX(float a) {
        const float s=sinf(a),c=cosf(a);
        *this=identity;m[1][1]=c;m[1][2]=s;m[2][1]=-s;m[2][2]=c;
    }
    void multiply(const Matrix& a,const Matrix& b) {
        Matrix r;
        for(int i=0;i<4;++i) for(int j=0;j<4;++j)
            r.m[i][j]=a.m[i][0]*b.m[0][j]+a.m[i][1]*b.m[1][j]+a.m[i][2]*b.m[2][j]+a.m[i][3]*b.m[3][j];
        *this=r;
    }
};
const Matrix Matrix::identity={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
struct PyObject { virtual ~PyObject()=default; };
struct PyObjectPlus : PyObject {};
struct Compound : PyObjectPlus {
    bool active=true;std::map<std::string,Matrix> authored,local;unsigned writes=0;
};
struct PyObjectPtr {
    PyObjectPtr(PyObject* p=nullptr):value(p){}
    explicit operator bool()const { return value!=nullptr; }
    PyObject* get()const { return value; }PyObject* value;
};
template<class T> struct WeakPyPtr {
    WeakPyPtr(T* p=nullptr):value(p){}
    const PyObject* getPyObj()const { return value; }T* value;
};
struct Provider { Vector4 value{0,0,0,0};unsigned reads=0;
    void output(Vector4& v) { ++reads;v=value; } };
using Vector4ProviderPtr=std::shared_ptr<Provider>;
struct LodLink : PyObject { bool alive=true;uint32 priority=0;float distance=0; };
unsigned errors=0;
#define ERROR_MSG(...) (++errors)
bool sampleStockLodLink(PyObject* p,uint32& priority,float& distance) {
    auto link=dynamic_cast<LodLink*>(p);
    if(!link || !link->alive) return false;
    priority=link->priority;distance=link->distance;return true;
}
bool stockVehicleCompoundNode(PyObject* p,const BW::string& name,Matrix& authored,Matrix& parent,Matrix& world) {
    auto model=dynamic_cast<Compound*>(p);
    if(!model || !model->active || !model->authored.count(name)) return false;
    authored=model->local.count(name)?model->local[name]:model->authored[name];
    parent=Matrix::identity;world=authored;return true;
}
bool writeStockVehicleCompoundNodeLocal(PyObject* p,const BW::string& name,const Matrix& local) {
    auto model=dynamic_cast<Compound*>(p);
    if(!model || !model->active || !model->authored.count(name)) return false;
    model->local[name]=local;++model->writes;return true;
}
struct StockComponentState : std::enable_shared_from_this<StockComponentState> {
    bool valid_=true;bool valid()const { return valid_; }
    virtual ~StockComponentState()=default;
    virtual void activate()=0;virtual void deactivate()=0;virtual void destroy()=0;
    virtual PyObject* wrap(bool)=0;
};
struct StockWheelsDataProvider {
    virtual ~StockWheelsDataProvider()=default;
    virtual size_t wheelCount()const=0;virtual float wheelRadius(size_t)const=0;
    virtual bool wheelIsLeft(size_t)const=0;virtual bool wheelWorld(size_t,Matrix&)const=0;
};
using StockSplineMovementReader=std::function<bool(float&)>;
'''

CASES = r'''
PyObject* WheelsState::wrap(bool) { return nullptr; }
bool close(float a,float b) { return std::fabs(a-b)<0.00001f; }
WheelRecord wheel(Compound& model,WheelsState& state,const char* name,bool left,float radius,
                  bool leading=false,float sync=0.f) {
    WheelRecord w;w.node=name;w.left=left;w.radius=radius;w.inverseRadius=1.f/radius;
    w.leading=leading;w.leadingFactor=sync*0.017453292f;state.link(w);return w;
}
int main() {
    Compound model;
    Matrix authored=Matrix::identity;authored[3][0]=1.5f;authored[3][1]=0.4f;
    model.authored["W_L0"]=authored;model.authored["W_R0"]=Matrix::identity;
    model.authored["W_L1"]=Matrix::identity;
    auto state=std::make_shared<WheelsState>(&model);
    state->motion.wheels.push_back(wheel(model,*state,"W_L0",true,0.5f));
    state->motion.wheels.push_back(wheel(model,*state,"W_R0",false,0.25f));
    state->motion.wheels.push_back(wheel(model,*state,"W_L1",true,0.4f,true,30.f));
    // A missing descriptor node is reported, keeps identity and is retained.
    state->motion.wheels.push_back(wheel(model,*state,"W_missing",true,0.5f));
    assert(errors==1 && state->motion.wheels.size()==4 && !state->motion.wheels[3].linked);
    assert(state->motion.wheels[0].authored[3][0]==1.5f);
    assert(state->wheelCount()==4 && state->wheelIsLeft(0) && state->wheelRadius(1)==0.25f);
    Matrix world;assert(!state->wheelWorld(3,world));

    // An inactive component never writes; the Hangar's unset movementInfo
    // republishes the authored pose and fabricates no rotation.
    state->update();assert(model.writes==0);
    state->activate();
    for(int frame=0;frame<5;++frame) state->update();
    assert(model.writes==15 && activeWheels.size()==1);
    assert(model.local["W_L0"][3][0]==1.5f && model.local["W_L0"][1][1]==1.f);
    for(auto& w:state->motion.wheels) assert(w.angle==0.f);

    // Left uses movementInfo.y, right .z, as per-frame displacements.
    auto provider=std::make_shared<Provider>();provider->value={9.f,0.25f,-0.5f,7.f};
    state->movement=provider;state->update();
    assert(close(state->motion.wheels[0].angle,0.5f));
    assert(close(state->motion.wheels[1].angle,-2.f));
    assert(close(state->motion.wheels[2].angle,0.625f));
    // Rotation is applied in the node's own frame before its authored local.
    const Matrix& posed=model.local["W_L0"];
    assert(close(posed[1][1],cosf(0.5f)) && close(posed[1][2],sinf(0.5f)) && posed[3][0]==1.5f);
    assert(posed[3][1]==0.4f && posed[3][2]==0.f);
    // fmodf keeps the remainder's sign and wraps at the original 2*pi float.
    provider->value={0.f,3.f,-3.f,0.f};
    for(int i=0;i<3;++i) state->update();
    const float left=fmodf(fmodf(fmodf(0.5f+6.f,6.2831855f)+6.f,6.2831855f)+6.f,6.2831855f);
    assert(state->motion.wheels[0].angle==left && state->motion.wheels[1].angle<0.f);
    assert(state->motion.wheels[1].angle>-6.2831855f);

    // stopAnimatingWheels bits freeze a side; poses are still republished.
    const float frozen=state->motion.wheels[0].angle,right=state->motion.wheels[1].angle;
    state->motion.stopped=1;const unsigned writes=model.writes;state->update();
    assert(state->motion.wheels[0].angle==frozen && state->motion.wheels[1].angle!=right);
    assert(model.writes==writes+3);state->motion.stopped=0;

    // A leading wheel follows the absolute spline phase on its own side only.
    state->spline[0]=[](float& v) { v=2.f;return true; };
    state->update();
    assert(close(state->motion.wheels[2].angle,30.f*0.017453292f*2.f));
    assert(state->motion.wheels[0].angle!=60.f*0.017453292f);
    // A failed spline sample falls back to ordinary displacement.
    state->spline[0]=[](float&) { return false; };
    const float before=state->motion.wheels[2].angle;state->update();
    assert(close(state->motion.wheels[2].angle,fmodf(3.f/0.4f+before,6.2831855f)));

    // LOD: None is always eligible; a farther or lower-priority link skips.
    LodLink lod;lod.distance=50.f;lod.priority=3;state->lodLink=&lod;
    state->settings.maxDistance=40.f;state->settings.maxPriority=5;
    const float held=state->motion.wheels[0].angle;state->update();
    assert(state->motion.wheels[0].angle==held);
    state->settings.maxDistance=60.f;state->update();
    assert(state->motion.wheels[0].angle!=held);
    state->settings.maxPriority=2;const float lodHeld=state->motion.wheels[0].angle;
    state->update();assert(state->motion.wheels[0].angle==lodHeld);
    state->settings.maxPriority=0xffffffffU;lod.distance=NAN;state->update();
    assert(state->motion.wheels[0].angle!=lodHeld);
    // A destroyed LOD producer is contained at this component.
    lod.alive=false;const unsigned lodErrors=errors;state->update();
    assert(!state->active && errors==lodErrors+1 && activeWheels.empty());
    state->lodLink=PyObjectPtr();state->activate();

    // A reset compound is contained: one error, deactivation and no write.
    model.active=false;const unsigned resetWrites=model.writes;state->update();
    assert(!state->active && model.writes==resetWrites && errors==lodErrors+2);

    // Destroy twice is harmless and leaves no active or published wheels.
    state->activate();state->destroy();state->destroy();
    assert(!state->valid() && activeWheels.empty() && state->wheelCount()==0);
    state->activate();assert(activeWheels.empty());
    tickStockVehicleWheels(0.016f);
}
'''


def extract():
    source = SOURCE.read_text()
    begin = source.index('struct WheelRecord\n')
    end = source.index('bool readStockLodSettings(')
    state_begin = source.index('struct WheelsState : public StockComponentState')
    state_end = source.index('\nclass WheelsAnimator : public StockComponentWrapper')
    tick_begin = source.index('void tickStockVehicleWheels(')
    tick_end = source.index('\nnamespace\n{\nclass WheelsScriptShutdown')
    middle = source[state_begin:state_end]
    # Close the extracted anonymous namespace around the state bodies.
    return ('namespace {\n' + source[begin:end] + '\nnamespace {\n' + middle + '\n' +
            source[tick_begin:tick_end] + '\n')


class VehicleWheelsTest(unittest.TestCase):
    def test_original_wheel_manager_contract(self):
        with tempfile.TemporaryDirectory(prefix='stock-wheels-') as directory:
            path = Path(directory)
            (path / 'probe.cpp').write_text(HOST + extract() + CASES)
            subprocess.run([
                os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall', '-Wextra',
                '-Werror', '-Wno-unused-parameter', '-ffp-contract=off',
                str(path / 'probe.cpp'), '-o', str(path / 'probe')], check=True)
            subprocess.run([str(path / 'probe')], check=True)

    def test_python_binding_matches_original_arity(self):
        source = SOURCE.read_text()
        for name, arity in (('addWheel', 'OOOOO'), ('addWheelGroup', 'OOO'),
                            ('setLodLink', 'O'), ('setLodSettings', 'O'),
                            ('setMovementInfo', 'O'), ('stopAnimatingWheels', 'OO'),
                            ('setSplineTrackMovementData', 'OO')):
            self.assertIn('"%s:%s"' % (arity, name), source)
            self.assertIn('PY_METHOD( %s )' % name, source)
        self.assertIn('PY_FACTORY( WheelsAnimator, Vehicular )', source)
        self.assertIn('fmodf( angle, 6.2831855f )', source)


if __name__ == '__main__':
    unittest.main()
