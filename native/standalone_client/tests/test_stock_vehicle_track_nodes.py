"""Compile the actual TrackNodesAnimator oscillator, linkage and manager.

The host proves the #1513 spring algebra, wheel/sibling linkage, the static
Hangar settle, LOD history, forward-pipeline branch and teardown. It does
not prove the Python C ABI or the Windows rendered track shape.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'bigworld/stock_vehicle_track_nodes.cpp'

HOST = r'''
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
using uint32=std::uint32_t;
namespace BW { using string=std::string; }
struct Vector3 {
    float x=0,y=0,z=0;Vector3()=default;Vector3(float a,float b,float c):x(a),y(b),z(c){}
    static Vector3 zero() { return Vector3(); }
    Vector3 operator-(const Vector3& o)const { return {x-o.x,y-o.y,z-o.z}; }
    Vector3 operator*(float s)const { return {x*s,y*s,z*s}; }
};
struct Matrix {
    float m[4][4];
    static const Matrix identity;
    float* operator[](unsigned i) { return m[i]; }
    const float* operator[](unsigned i) const { return m[i]; }
    void setZero() { std::memset(m,0,sizeof(m)); }
    void multiply(const Matrix& a,const Matrix& b) {
        Matrix r;
        for(int i=0;i<4;++i) for(int j=0;j<4;++j)
            r.m[i][j]=a.m[i][0]*b.m[0][j]+a.m[i][1]*b.m[1][j]+a.m[i][2]*b.m[2][j]+a.m[i][3]*b.m[3][j];
        *this=r;
    }
    Vector3 applyVector(const Vector3& v)const {
        return {v.x*m[0][0]+v.y*m[1][0]+v.z*m[2][0],v.x*m[0][1]+v.y*m[1][1]+v.z*m[2][1],
                v.x*m[0][2]+v.y*m[1][2]+v.z*m[2][2]};
    }
    Vector3 applyToOrigin()const { return {m[3][0],m[3][1],m[3][2]}; }
    // Rigid inverse is sufficient for the authored test poses.
    bool invert(const Matrix& a) {
        Matrix r=identity;
        for(int i=0;i<3;++i) for(int j=0;j<3;++j) r.m[i][j]=a.m[j][i];
        for(int j=0;j<3;++j) r.m[3][j]=-(a.m[3][0]*r.m[0][j]+a.m[3][1]*r.m[1][j]+a.m[3][2]*r.m[2][j]);
        *this=r;return true;
    }
};
const Matrix Matrix::identity={{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
Matrix translate(float x,float y,float z) { Matrix r=Matrix::identity;r[3][0]=x;r[3][1]=y;r[3][2]=z;return r; }
struct PyObject { virtual ~PyObject()=default; };
struct PyObjectPlus : PyObject {};
struct Node { std::string parent;Matrix authored,local; };
struct Compound : PyObjectPlus {
    bool active=true;std::map<std::string,Node> nodes;unsigned writes=0;
    Matrix world(const std::string& name) {
        if(name.empty()) return nodes[""].local;
        Matrix w;w.multiply(nodes[name].local,world(nodes[name].parent));return w;
    }
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
    if(!model || !model->active || !model->nodes.count(name)) return false;
    authored=model->nodes[name].authored;
    parent=name.empty()?Matrix::identity:model->world(model->nodes[name].parent);
    world=model->world(name);return true;
}
bool writeStockVehicleCompoundNodeLocal(PyObject* p,const BW::string& name,const Matrix& local) {
    auto model=dynamic_cast<Compound*>(p);
    if(!model || !model->active || !model->nodes.count(name)) return false;
    model->nodes[name].local=local;++model->writes;return true;
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
struct IRendererPipeline { enum { TYPE_FORWARD_SHADING=1, TYPE_DEFERRED_SHADING=2 }; };
struct Renderer {
    int type=IRendererPipeline::TYPE_DEFERRED_SHADING;
    static Renderer* pInstance() { static Renderer r;return &r; }
    static Renderer& instance() { return *pInstance(); }
    int pipelineType()const { return type; }
};
'''

CASES = r'''
PyObject* TrackNodesState::wrap(bool) { return nullptr; }
struct Wheels : StockWheelsDataProvider {
    Compound* model;std::vector<std::string> names;std::vector<bool> left;std::vector<float> radius;
    size_t wheelCount()const override { return names.size(); }
    float wheelRadius(size_t i)const override { return radius[i]; }
    bool wheelIsLeft(size_t i)const override { return left[i]; }
    bool wheelWorld(size_t i,Matrix& w)const override { w=model->world(names[i]);return true; }
};
TrackNode node(TrackNodesState& s,const char* name,bool left,float offset,const char* l,const char* r,
               float damping,float elasticity,float fwd,float back) {
    TrackNode n;n.name=name;n.isLeft=left;n.leftName=l;n.rightName=r;
    n.damping=damping;n.elasticity=elasticity;n.forwardCoefficient=fwd;n.backwardCoefficient=back;
    if(0.001f<fabsf(fwd-back)) n.blend=(1.f-back)/(fwd-back);
    n.previousOffset=n.offset=offset;
    n.spring.setMaxAmplitude(s.maxAmplitude);n.spring.setMass(TRACK_NODE_MASS);
    n.spring.decay=n.damping;n.spring.setForce(s.gravity);n.spring.setStiffness(n.elasticity);n.spring.reset();
    Matrix parent,world;n.linked=s.node(n.name,n.authored,parent,world);
    if(!n.linked) { n.authored=Matrix::identity;++errors; }
    return n;
}
bool finite(float v) { return std::isfinite(v); }
int main() {
    // Oscillator algebra: a rebase preserves the current value and the
    // amplitude limit clips the rebased amplitudes.
    TrackOscillator o;o.setMaxAmplitude(0.5f);o.setMass(60.f);o.decay=1.f;o.setForce(-9.8f);
    o.setStiffness(1500.f);o.reset();
    assert(o.frequency==(1.f/60.f)*1500.f && fabsf(o.rest+9.8f/(o.frequency*o.frequency))<1e-7f);
    o.addImpulse(600.f);assert(fabsf(o.sine-(1.f/60.f)*600.f/o.frequency)<1e-6f && o.cosine==0.f);
    o.setMaxAmplitude(10.f);o.update(0.1f);
    const float before=o.value();o.setStiffness(900.f);assert(o.time==0.f);assert(fabsf(o.value()-before)<1e-4f);
    o.setMaxAmplitude(0.5f);o.addImpulse(1e9f);assert(sqrtf(o.sine*o.sine+o.cosine*o.cosine)<=0.5f+1e-6f);
    o.update(0.5f);assert(o.time==0.f+0.5f && finite(o.value()));
    TrackOscillator d;d.cosine=1.f;d.sine=-2.f;d.decay=2.f;d.update(0.5f);
    assert(d.cosine==expf(-1.f) && d.sine==-2.f*expf(-1.f));

    // A chassis with hull, two track nodes and one left wheel below node 0.
    Compound model;
    model.nodes[""]={"",translate(0,0,0),translate(0,0,0)};
    model.nodes["Tank"]={"",translate(0,0.2f,0),translate(0,0.2f,0)};
    model.nodes["V"]={"Tank",Matrix::identity,Matrix::identity};
    model.nodes["T0"]={"V",translate(1.5f,0.6f,0.1f),translate(1.5f,0.6f,0.1f)};
    model.nodes["T1"]={"V",translate(1.5f,0.6f,0.8f),translate(1.5f,0.6f,0.8f)};
    model.nodes["TR"]={"V",translate(-1.5f,0.6f,0.1f),translate(-1.5f,0.6f,0.1f)};
    model.nodes["W"]={"Tank",translate(1.5f,0.2f,0.f),translate(1.5f,0.2f,0.f)};
    auto wheels=std::make_shared<Wheels>();wheels->model=&model;
    wheels->names={"W"};wheels->left={true};wheels->radius={0.4f};
    auto state=std::make_shared<TrackNodesState>(&model,"V");
    state->captureHull();assert(state->hullLinked && errors==0);
    state->thickness=-0.07f;state->gravity=-9.8f;state->maxAmplitude=0.5f;state->maxOffset=0.2f;
    state->nodes.push_back(node(*state,"T0",true,0.f,"","T1",1.f,1500.f,1.f,1.f));
    state->nodes.push_back(node(*state,"T1",true,-0.01f,"T0","",1.f,1500.f,1.2f,0.8f));
    state->nodes.push_back(node(*state,"TR",false,0.f,"","",1.f,1500.f,1.f,1.f));
    state->nodes.push_back(node(*state,"missing",true,0.f,"","",1.f,1500.f,1.f,1.f));
    assert(errors==1 && !state->nodes[3].linked);
    state->wheels=wheels;

    // Activation links once and settles 60 steps of 1/60 s with extra damping.
    state->activate();
    assert(state->linked && state->warmup==0.f && state->resetVelocity && activeTrackNodes.size()==1);
    assert(state->nodes[0].wheel==0 && state->nodes[1].wheel==-1 && state->nodes[2].wheel==-1);
    assert(state->nodes[0].right==1 && state->nodes[1].left==0 && state->nodes[0].left==-1);
    assert(state->nodes[0].hullChild && state->nodes[2].left==-1);
    // The node above the wheel is pushed to the wheel surface; the others
    // sag under gravity and are bounded by the oscillator limit.
    const float surface=sqrtf(0.4f*0.4f-0.01f)+0.4f-0.035f;
    assert(0.8f+state->nodes[0].offset>=surface-1e-4f);
    assert(state->nodes[2].offset<0.f && state->nodes[2].offset>=-0.5f-1e-5f);

    // The one-second warmup leaves an unsupported node at its spring rest.
    assert(fabsf(state->nodes[2].offset-state->nodes[2].spring.rest)<1e-3f);
    assert(state->nodes[2].spring.decay==1.f);

    // A static Hangar vehicle stays bounded and settles rather than drifting.
    std::vector<float> history;
    for(int frame=0;frame<600;++frame) {
        tickStockVehicleTrackNodes(1.f/60.f);
        history.push_back(state->nodes[2].offset);
    }
    assert(!state->resetVelocity && model.writes==600*3);
    assert(fabsf(history[599]-history[598])<1e-5f && fabsf(history[599]-history[400])<1e-4f);
    for(auto& n:state->nodes) assert(finite(n.offset) && n.offset<=state->maxOffset);
    // Published locals are the authored locals moved along their own Y.
    assert(fabsf(model.nodes["TR"].local[3][1]-(0.6f+state->nodes[2].offset))<1e-6f);
    assert(model.nodes["TR"].local[3][0]==-1.5f);

    // A tiny step is ignored entirely but the pose is still republished.
    const float held=state->nodes[2].offset;tickStockVehicleTrackNodes(1e-8f);
    assert(state->nodes[2].offset==held);

    // LOD history: an ineligible frame publishes nothing, the first eligible
    // frame after it starts from zero node velocity.
    LodLink lod;lod.distance=500.f;state->lodLink=&lod;state->maxDistance=100.f;
    const unsigned writes=model.writes;tickStockVehicleTrackNodes(1.f/60.f);
    assert(model.writes==writes && (state->eligibility&3U)==2U);
    lod.distance=50.f;model.nodes["V"].local=translate(0,0.05f,0);
    tickStockVehicleTrackNodes(1.f/60.f);assert((state->eligibility&3U)==1U);
    assert(state->nodes[2].previousVelocity.y==0.f);
    tickStockVehicleTrackNodes(1.f/60.f);assert(state->nodes[2].previousVelocity.y==0.f);
    lod.alive=false;const unsigned lodErrors=errors;tickStockVehicleTrackNodes(1.f/60.f);
    assert(!state->active && errors==lodErrors+1);
    state->lodLink=PyObjectPtr();state->activate();assert(state->active);

    // Forward pipeline: no spring step, authored + current offset is kept.
    Renderer::instance().type=IRendererPipeline::TYPE_FORWARD_SHADING;
    const float frozen=state->nodes[0].offset;tickStockVehicleTrackNodes(1.f/60.f);
    assert(state->nodes[0].offset==frozen);
    Renderer::instance().type=IRendererPipeline::TYPE_DEFERRED_SHADING;

    // A destroyed wheel provider is contained: no contact, no crash.
    wheels.reset();tickStockVehicleTrackNodes(1.f/60.f);assert(state->active);
    // A reset compound deactivates with one error and no write.
    model.active=false;const unsigned resetErrors=errors;tickStockVehicleTrackNodes(1.f/60.f);
    assert(!state->active && errors==resetErrors+1);
    model.active=true;state->destroy();state->destroy();
    assert(!state->valid() && state->nodes.empty() && activeTrackNodes.empty());
    state->activate();assert(activeTrackNodes.empty());

    // The 19 shipped chassis without trackNodes still build an empty animator.
    auto empty=std::make_shared<TrackNodesState>(&model,"V");empty->captureHull();
    empty->activate();tickStockVehicleTrackNodes(1.f/60.f);assert(empty->active);
    empty->destroy();
    // Forward first activation links without the settle loop.
    Renderer::instance().type=IRendererPipeline::TYPE_FORWARD_SHADING;
    auto forward=std::make_shared<TrackNodesState>(&model,"V");forward->captureHull();
    forward->gravity=-9.8f;forward->maxAmplitude=0.5f;forward->maxOffset=0.2f;
    forward->nodes.push_back(node(*forward,"TR",false,0.05f,"","",1.f,1500.f,1.f,1.f));
    forward->activate();assert(forward->warmup==0.f && !forward->resetVelocity);
    tickStockVehicleTrackNodes(1.f/60.f);
    // V swung up by 0.05 since assembly; forward rendering cancels it.
    assert(fabsf(model.nodes["TR"].local[3][1]-0.6f)<1e-6f);
    assert(fabsf(model.world("TR")[3][1]-0.85f)<1e-6f);
    // Focused single-step laws on a fresh deferred component.
    Renderer::instance().type=IRendererPipeline::TYPE_DEFERRED_SHADING;
    model.nodes["V"].local=Matrix::identity;
    model.nodes["W2"]={"Tank",translate(1.5f,0.1f,0.f),translate(1.5f,0.1f,0.f)};
    model.nodes["W3"]={"Tank",translate(1.5f,0.3f,0.f),translate(1.5f,0.3f,0.f)};
    auto laws=std::make_shared<TrackNodesState>(&model,"V");laws->captureHull();
    laws->gravity=-9.8f;laws->maxAmplitude=0.5f;laws->maxOffset=0.2f;laws->thickness=0.f;
    // T0 sits inside W3: its surface 0.5+sqrt(0.16-0.01) is above 0.8.
    laws->nodes.push_back(node(*laws,"T0",true,-0.05f,"","T1",1.f,1500.f,1.f,1.f));
    laws->nodes.push_back(node(*laws,"T1",true,0.f,"T0","",1.f,1500.f,1.f,1.f));
    auto two=std::make_shared<Wheels>();two->model=&model;
    two->names={"W2","W3"};two->left={true,true};two->radius={0.4f,0.4f};
    laws->wheels=two;laws->link();
    // The higher of two same-side wheels below the node is selected.
    assert(laws->nodes[0].wheel==1 && fabsf(laws->nodes[0].wheelPosition.y-0.5f)<1e-6f);
    laws->nodes[0].previousOffset=0.02f;laws->nodes[1].previousOffset=0.06f;
    TrackOscillator mirror=laws->nodes[1].spring;mirror.update(0.01f);
    laws->simulate(0.01f);
    // A sibling-only node: oscillator value plus the mean previous offsets.
    assert(fabsf(laws->nodes[1].offset-(mirror.value()+(0.f+0.02f)*0.5f))<1e-6f);
    // The contacting node is lifted exactly to the wheel surface.
    const float lifted=sqrtf(0.16f-0.01f)+0.5f;
    assert(fabsf(0.8f+laws->nodes[0].offset-lifted)<1e-5f);
    // Penetration beyond maxOffset is clamped to it.
    laws->maxOffset=-0.02f;laws->nodes[0].previousOffset=0.f;laws->simulate(0.01f);
    assert(laws->nodes[0].offset==-0.02f);
    // Gravity follows the root up vector: a root tilted 60 degrees halves it.
    Matrix tilt=Matrix::identity;tilt[1][1]=0.5f;tilt[1][2]=sqrtf(3.f)/2.f;tilt[2][1]=-sqrtf(3.f)/2.f;tilt[2][2]=0.5f;
    model.nodes[""].local=tilt;laws->simulate(0.01f);
    assert(fabsf(laws->nodes[1].spring.force+9.8f*0.5f)<1e-6f);
    model.nodes[""].local=Matrix::identity;
    // The activation settle adds a decaying 10/s damping bonus.
    laws->warmup=10.f;laws->simulate(1.f/60.f);
    assert(laws->warmup==10.f-(1.f/60.f)*10.f && laws->nodes[1].spring.decay==1.f+laws->warmup);
    laws->destroy();

    // A missing hull node is reported and the factory still succeeds.
    auto unhulled=std::make_shared<TrackNodesState>(&model,"absent");
    const unsigned hullErrors=errors;unhulled->captureHull();
    assert(!unhulled->hullLinked && errors==hullErrors+1);
}
'''


def extract():
    source = SOURCE.read_text()
    begin = source.index('float trackCos(')
    end = source.index('\nclass TrackNodesAnimator : public StockComponentWrapper')
    tick_begin = source.index('void tickStockVehicleTrackNodes(')
    tick_end = source.index('\nnamespace\n{\nclass TrackNodesScriptShutdown')
    return 'namespace {\n' + source[begin:end] + '\n' + source[tick_begin:tick_end] + '\n'


class VehicleTrackNodesTest(unittest.TestCase):
    def test_original_track_node_manager_contract(self):
        with tempfile.TemporaryDirectory(prefix='stock-track-nodes-') as directory:
            path = Path(directory)
            (path / 'probe.cpp').write_text(HOST + extract() + CASES)
            subprocess.run([
                os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall', '-Wextra',
                '-Werror', '-Wno-unused-parameter', '-Wno-unused-function',
                '-ffp-contract=off', str(path / 'probe.cpp'), '-o', str(path / 'probe')],
                check=True)
            subprocess.run([str(path / 'probe')], check=True)

    def test_python_binding_matches_original_arity(self):
        source = SOURCE.read_text()
        for name, arity in (('setParameters', 'ffff'), ('addTrackNode', 'OOOOOO'),
                            ('setWheelsDataProvider', 'O'), ('setLodLink', 'O'),
                            ('setLodSettings', 'O'), ('TrackNodesAnimator', 'OO')):
            self.assertIn('"%s:%s"' % (arity, name), source)
        self.assertIn('PY_FACTORY( TrackNodesAnimator, Vehicular )', source)
        for constant in ('60.f;          // 01a12f2c', '0.3f;   // 01a12f30',
                         '-1.f; // 01a12f34', '10.f;    // 01a12f38'):
            self.assertIn(constant, source)


if __name__ == '__main__':
    unittest.main()
