"""Verify the ordinary #1513 FreeCamera tree, provider and reset lifecycle."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_stock_cursor_camera import (ARCHIVE, CLIENT, PY27, ROOT, function,
                                      requirements, run_oracle)
import tarfile


def load_patch():
    spec = importlib.util.spec_from_file_location('free_camera_patch',
                                                ROOT / 'bigworld/patch_stock_free_camera.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fresh_engine(root):
    with tarfile.open(ARCHIVE) as archive:
        prefix = archive.getmembers()[0].name.split('/')[0] + '/programming/bigworld/'
        for name in (load_patch().HEADER, load_patch().SOURCE):
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.extractfile(prefix + name).read())


HOST = r'''
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define Py_Header(a,b)
#define BW_GUARD
struct PyTypeObject {};
struct Vector3 { float x,y,z; Vector3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
    static Vector3 zero(){return Vector3();} };
struct Matrix {
    Vector3 t;
    void invert(const Matrix& m){t=Vector3(-m.t.x,-m.t.y,-m.t.z);}
    void postMultiply(const Matrix&){} void postRotateY(float){} void preRotateX(float){}
    Vector3 applyToOrigin() const{return t;} void translation(Vector3 value){t=value;}
};
std::vector<std::string> events;
struct MatrixProvider { void matrix(Matrix& value){events.push_back("provider");value.t=Vector3(3,5,7);} };
typedef std::shared_ptr<MatrixProvider> MatrixProviderPtr;
void setStockTreeCameraTarget(MatrixProvider* value){events.push_back(value?"tree:1":"tree:0");}
struct BaseCamera {
    BaseCamera(PyTypeObject*){} virtual ~BaseCamera(){events.push_back("base");}
    virtual void update(float)=0; Matrix view_,invView_;
};
#define PY_RW_ATTRIBUTE_DECLARE(member,name) \
    void name(MatrixProviderPtr value){member=value;}
@SHARED@
namespace Math { float decay(float a,float,float,float){return a;} }
struct CameraControl {
    static int keys[3]; static int resetCount;
    static Matrix calculateDeltaMatrix(float){return Matrix();}
    static float yawVelocity(){return 0;} static void yawVelocity(float){}
    static float pitchVelocity(){return 0;} static void pitchVelocity(float){}
    static float orientationDamper(){return 1;} static float cameraMass(){return 0;}
    static void axisNudge(Vector3){}
    static void clearVel(){for(int& key:keys)key=0;++resetCount;}
};
int CameraControl::keys[3]={123,124,125}; int CameraControl::resetCount=0;
struct FreeCamera:SpeedTreeTargetedCamera {
    FreeCamera():SpeedTreeTargetedCamera(nullptr),fixed_(true){}
    bool fixed_; MatrixProviderPtr invViewProvider_;
    void update(float) override; void resetKeys();
};
@UPDATE@
@RESET@
void show(const char* stage,FreeCamera* camera=nullptr){
    std::cout<<stage;for(const auto& event:events)std::cout<<" "<<event;events.clear();
    if(camera)std::cout<<" "<<camera->view_.t.x<<" "<<camera->view_.t.y<<" "<<camera->view_.t.z
                      <<" "<<camera->invView_.t.x<<" "<<camera->invView_.t.y<<" "<<camera->invView_.t.z;
    std::cout<<"\n";
}
int main(){
    auto target=std::make_shared<MatrixProvider>(),provider=std::make_shared<MatrixProvider>();
    auto camera=new FreeCamera();
    camera->update(.016f);show("empty",camera);
    camera->speedTreeTarget(target);camera->update(.016f);show("fixed",camera);
    camera->invViewProvider_=provider;camera->update(.016f);show("provider",camera);
    camera->speedTreeTarget(MatrixProviderPtr());camera->update(.016f);show("disabled",camera);
    camera->speedTreeTarget(target);delete camera;show("destruct");
    std::cout<<"released "<<target.use_count()<<" "<<provider.use_count()<<"\n";
    FreeCamera reset;reset.resetKeys();
    std::cout<<"reset "<<CameraControl::keys[0]<<" "<<CameraControl::keys[1]<<" "<<CameraControl::keys[2]<<"\n";
}
'''


ORIGINAL = r'''
import sys
sys.path.insert(0,sys.argv[1])
from stock_cursor_camera_oracle import OriginalCursorCamera,bits
e=OriginalCursorCamera(sys.argv[2]);events=[]
c=e.alloc(0xb0);target=e.alloc(0x20);provider=e.alloc(0x20)
renderer=e.alloc(0x200);table=e.alloc(0x200);slot=e.entry()
e.put_word(renderer,table);e.put_word(table+0x10c,slot)
e.handle(0x941a10,lambda x:x._return(renderer))
e.handle(slot,lambda x:(events.append('tree:1' if x._arg(0) else 'tree:0'),x._return(0,8)))
e.handle(0x7968e0,lambda x:(events.append('base'),x._return()))
identity=[[1.,0.,0.,0.],[0.,1.,0.,0.],[0.,0.,1.,0.],[0.,0.,0.,1.]]
e.put_matrix(c+0x18,identity);e.put_matrix(c+0x58,identity)
e.uc.mem_write(c+0xac,b'\x01');e.put_word(target+4,100)
def show(stage,view=True):
    values=e.matrix(c+0x18)[3][:3]+e.matrix(c+0x58)[3][:3] if view else []
    print(' '.join([stage]+events+[str(int(v)) for v in values]));events[:]=[]
e.call(0x79f270,(bits(.016),),c);show('empty')
e.put_word(c+0xa4,target);e.call(0x79f270,(bits(.016),),c);show('fixed')
pslot=e.entry();e.put_word(provider,e.vtable(e.entry(),e.entry(),pslot))
e.put_word(provider+4,100);e.put_word(c+0xa8,provider)
def matrix(x):
    events.append('provider');m=[row[:] for row in identity];m[3][:3]=[3.,5.,7.]
    x.put_matrix(x._arg(0),m);x._return(0,4)
e.handle(pslot,matrix)
e.call(0x79f270,(bits(.016),),c);show('provider')
e.put_word(c+0xa4,0);e.call(0x79f270,(bits(.016),),c);show('disabled')
e.put_word(c+0xa4,target);e.call(0x79edb0,(0,),c);show('destruct',False)
print('released %d %d'%(100-e.word(target+4),100-e.word(provider+4)))
for i in range(3):e.put_word(0x1d328ec+4*i,123+i)
e.call(0x79f150,(),c)
print('reset '+' '.join(str(e.word(0x1d328ec+i*4)) for i in range(3)))
'''


class FreeCameraTests(unittest.TestCase):
    def test_patch_restores_native_parent_and_is_idempotent(self):
        requirements(self, oracle=False)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fresh_engine(root)
            patcher = load_patch()
            patcher.patch(root)
            first = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
            patcher.patch(root)
            self.assertEqual(first, {p: p.read_bytes() for p in root.rglob('*') if p.is_file()})
            header = (root / patcher.HEADER).read_text()
            self.assertIn('class FreeCamera : public SpeedTreeTargetedCamera', header)
            self.assertIn('Py_Header( FreeCamera, SpeedTreeTargetedCamera )', header)
            source = (root / patcher.SOURCE).read_text()
            self.assertIn('PY_METHOD( resetKeys )', source)
            self.assertLess(source.index('SpeedTreeTargetedCamera::update( dTime )'),
                            source.index('if ( invViewProvider_ )'))

    def test_patch_failure_does_not_publish_partial_inheritance(self):
        requirements(self, oracle=False)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fresh_engine(root)
            patcher = load_patch()
            source = root / patcher.SOURCE
            source.write_text(source.read_text().replace('void FreeCamera::update( float dTime )',
                                                        'void FreeCamera::update( float changed )'))
            first = {p: p.read_bytes() for p in root.rglob('*') if p.is_file()}
            with self.assertRaisesRegex(ValueError, 'owner anchor changed'):
                patcher.patch(root)
            self.assertEqual(first, {p: p.read_bytes() for p in root.rglob('*') if p.is_file()})

    def test_fixed_provider_and_teardown_match_original_x86(self):
        requirements(self)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fresh_engine(root)
            patcher = load_patch()
            patcher.patch(root)
            source = (root / patcher.SOURCE).read_text()
            shared = (ROOT / 'bigworld/stock_speedtree_targeted_camera.hpp').read_text()
            shared = '\n'.join(line for line in shared.splitlines()
                               if not line.startswith(('#include', '#pragma')))
            host = HOST.replace('@SHARED@', shared).replace(
                '@UPDATE@', function(source, 'void FreeCamera::update( float dTime )')).replace(
                '@RESET@', function(source, 'void FreeCamera::resetKeys()'))
            cpp, binary = root / 'host.cpp', root / 'host'
            cpp.write_text(host)
            subprocess.run(['c++', '-std=c++14', '-Wall', '-Werror', str(cpp), '-o', str(binary)],
                           check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, check=True)
            # The final automatic local camera is destroyed after the last row.
            self.assertEqual(result.stdout.splitlines(), run_oracle(ORIGINAL).splitlines())

    def test_stock_python_consumers_require_tree_and_reset_contract(self):
        requirements(self)
        if not PY27.exists():
            self.skipTest('CPython 2.7 required for the pinned bytecode')
        script = r'''
import json,marshal,sys,types,zipfile
z=zipfile.ZipFile(sys.argv[1]);out={}
def walk(code,path):
    if code.co_name in ('enable','disable','resetMovement') and any(
            name in code.co_names for name in ('speedTreeTarget','resetKeys')):
        out[path+':'+code.co_name]=list(code.co_names)
    for value in code.co_consts:
        if isinstance(value,types.CodeType):walk(value,path)
for path in ('scripts/client/AvatarInputHandler/VideoCamera.pyc',
             'scripts/client/AvatarInputHandler/cameras.pyc'):
    walk(marshal.loads(z.read(path)[8:]),path)
print(json.dumps(out))
'''
        result = subprocess.run([str(PY27), '-c', script, str(CLIENT / 'res/packages/scripts.pkg')],
                                capture_output=True, text=True, check=True)
        consumers = json.loads(result.stdout)
        for method in ('enable', 'disable'):
            self.assertIn('speedTreeTarget', consumers[
                'scripts/client/AvatarInputHandler/VideoCamera.pyc:' + method])
        self.assertIn('resetKeys', consumers[
            'scripts/client/AvatarInputHandler/cameras.pyc:resetMovement'])


if __name__ == '__main__':
    unittest.main()
