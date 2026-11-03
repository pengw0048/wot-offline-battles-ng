"""Execute the production focus owner and projection with host math boundaries.

These tests establish ownership and geometry, not Windows GPU acceptance.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

HOST = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <utility>
#include "wot/stock_shadow_rectangle.hpp"
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
struct Vector3 {float x,y,z;Vector3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}};
typedef Vector3 D3DXVECTOR3;
struct Matrix {float m[4][4];Matrix(){for(int i=0;i<4;++i)for(int j=0;j<4;++j)m[i][j]=float(i==j);}float& operator()(int i,int j){return m[i][j];}};
void D3DXMatrixIdentity(Matrix* out){*out=Matrix();}
void D3DXMatrixMultiply(Matrix* out,const Matrix* a,const Matrix* b){Matrix c;for(int i=0;i<4;++i)for(int j=0;j<4;++j){c.m[i][j]=0;for(int k=0;k<4;++k)c.m[i][j]+=a->m[i][k]*b->m[k][j];}*out=c;}
Matrix* D3DXMatrixInverse(Matrix* out,void*,const Matrix* in){double a[4][8];for(int i=0;i<4;++i)for(int j=0;j<8;++j)a[i][j]=j<4?in->m[i][j]:double(j-4==i);for(int i=0;i<4;++i){int pivot=i;for(int k=i+1;k<4;++k)if(std::fabs(a[k][i])>std::fabs(a[pivot][i]))pivot=k;if(a[pivot][i]==0)return nullptr;for(int j=0;j<8;++j)std::swap(a[i][j],a[pivot][j]);double scale=a[i][i];for(int j=0;j<8;++j)a[i][j]/=scale;for(int k=0;k<4;++k)if(k!=i){scale=a[k][i];for(int j=0;j<8;++j)a[k][j]-=scale*a[i][j];}}for(int i=0;i<4;++i)for(int j=0;j<4;++j)out->m[i][j]=float(a[i][j+4]);return out;}
float dot(Vector3 a,Vector3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vector3 unit(Vector3 a){float n=std::sqrt(dot(a,a));return {a.x/n,a.y/n,a.z/n};}
void D3DXVec3Cross(Vector3* out,const Vector3* a,const Vector3* b){*out={a->y*b->z-a->z*b->y,a->z*b->x-a->x*b->z,a->x*b->y-a->y*b->x};}
void D3DXVec3TransformNormal(Vector3* out,const Vector3* p,const Matrix* m){*out={p->x*m->m[0][0]+p->y*m->m[1][0]+p->z*m->m[2][0],p->x*m->m[0][1]+p->y*m->m[1][1]+p->z*m->m[2][1],p->x*m->m[0][2]+p->y*m->m[1][2]+p->z*m->m[2][2]};}
void D3DXVec3TransformCoord(Vector3* out,const Vector3* p,const Matrix* m){Vector3 q;D3DXVec3TransformNormal(&q,p,m);float w=p->x*m->m[0][3]+p->y*m->m[1][3]+p->z*m->m[2][3]+m->m[3][3];*out={(q.x+m->m[3][0])/w,(q.y+m->m[3][1])/w,(q.z+m->m[3][2])/w};}
void D3DXMatrixLookAtLH(Matrix* out,const Vector3* eye,const Vector3* at,const Vector3* up){Vector3 z=unit({at->x-eye->x,at->y-eye->y,at->z-eye->z}),x,y;D3DXVec3Cross(&x,up,&z);x=unit(x);D3DXVec3Cross(&y,&z,&x);*out=Matrix();for(int j=0;j<3;++j){out->m[j][0]=(&x.x)[j];out->m[j][1]=(&y.x)[j];out->m[j][2]=(&z.x)[j];out->m[j][3]=0;}out->m[3][0]=-dot(x,*eye);out->m[3][1]=-dot(y,*eye);out->m[3][2]=-dot(z,*eye);}
void D3DXMatrixOrthoLH(Matrix* out,float w,float h,float near,float far){*out=Matrix();out->m[0][0]=2/w;out->m[1][1]=2/h;out->m[2][2]=1/(far-near);out->m[3][2]=-near/(far-near);}
struct MatrixProvider {int references=1;Matrix pose;std::function<void()> read,destroy;void matrix(Matrix& out){out=pose;if(read)read();}~MatrixProvider(){if(destroy)destroy();}};
struct MatrixProviderPtr {MatrixProvider* p=nullptr;MatrixProviderPtr(){}MatrixProviderPtr(MatrixProvider* q):p(q){if(p)++p->references;}MatrixProviderPtr(const MatrixProviderPtr& q):MatrixProviderPtr(q.p){}~MatrixProviderPtr(){if(p&&!--p->references)delete p;}MatrixProviderPtr& operator=(MatrixProviderPtr q){std::swap(p,q.p);return *this;}MatrixProvider* get()const{return p;}MatrixProvider* operator->()const{return p;}explicit operator bool()const{return p!=nullptr;}};
// @PRODUCTION@
MatrixProvider* treeTarget=nullptr;
void setStockTreeCameraTarget(MatrixProvider* value){treeTarget=value;}
// @PUBLISH@
void release(MatrixProvider* p){if(!--p->references)delete p;}
int main(){
 Matrix out;out(3,0)=-99;auto* a=new MatrixProvider;auto* b=new MatrixProvider;
 a->pose(3,0)=7;b->pose(3,0)=11;
 setStockDirectionalShadowFocus(a);assert(a->references==1);assert(!stockDirectionalShadowFocus(out));
 openStockDirectionalShadowFocus();publishStockCursorCameraHiding(a,false);assert(a->references==2&&treeTarget==nullptr);
 publishStockCursorCameraHiding(a,true);assert(treeTarget==a&&a->references==2);
 setStockDirectionalShadowFocus(a);assert(a->references==2);assert(stockDirectionalShadowFocus(out)&&out(3,0)==7);
 a->read=[&](){setStockDirectionalShadowFocus(b);};out(3,0)=-99;
 assert(!stockDirectionalShadowFocus(out)&&out(3,0)==-99);assert(a->references==1&&b->references==2);
 assert(stockDirectionalShadowFocus(out)&&out(3,0)==11);release(a);
 bool destroyed=false;b->destroy=[&](){destroyed=true;setStockDirectionalShadowFocus(nullptr);};release(b);
 closeStockDirectionalShadowFocus();assert(destroyed);closeStockDirectionalShadowFocus();assert(!stockDirectionalShadowFocus(out));
 openStockDirectionalShadowFocus();auto* late=new MatrixProvider;setStockDirectionalShadowFocus(late);release(late);closeStockDirectionalShadowFocus();
 // Execute the actual tight projection over varied suns and translations.
 for(int sun=0;sun<30;++sun){
  Vector3 light=unit({.2f+sun*.013f,-.9f,.3f-sun*.009f});
  float previousWidth=0,previousHeight=0;Vector3 previousUV;
  for(int step=0;step<20;++step){
   float shift=step*.001f;Vector3 eye(15+shift,6,-9+shift),at(15+shift,1,shift),up(0,1,0);
   Matrix camera,projection,vp,view,p,texture,cull;D3DXMatrixLookAtLH(&camera,&eye,&at,&up);
   for(int i=0;i<4;++i)for(int j=0;j<4;++j)projection.m[i][j]=0;
   projection(0,0)=1.f/(std::tan(.5235988f)*1.7777778f);projection(1,1)=1.f/std::tan(.5235988f);
   projection(2,2)=14.f/(14.f-.1f);projection(2,3)=1;projection(3,2)=-.1f*14.f/(14.f-.1f);
   D3DXMatrixMultiply(&vp,&camera,&projection);
   assert(stockDirectionalShadowTightMatrices(view,p,texture,cull,vp,light,500));
   Matrix inverse;assert(D3DXMatrixInverse(&inverse,nullptr,&vp));
   for(unsigned corner=0;corner<8;++corner){Vector3 n((corner&1)?-1.f:1.f,(corner&2)?-1.f:1.f,(corner&4)?0.f:1.f),world,clip;D3DXVec3TransformCoord(&world,&n,&inverse);D3DXVec3TransformCoord(&clip,&world,&cull);assert(std::fabs(clip.x)<1.0003f&&std::fabs(clip.y)<1.0003f&&clip.z>=-.0003f&&clip.z<=1.0003f);}
   Vector3 receiver(15+shift,1,shift),uv;D3DXVec3TransformCoord(&uv,&receiver,&texture);
   if(step){assert(std::fabs(p(0,0)-previousWidth)<.0001f&&std::fabs(p(1,1)-previousHeight)<.0001f);assert(std::fabs(uv.x-previousUV.x)<.0001f&&std::fabs(uv.y-previousUV.y)<.0001f);}
   previousWidth=p(0,0);previousHeight=p(1,1);previousUV=uv;
  }
 }
 std::cout<<"focus lifecycle and 600 tight-projection translation cases passed\n";
}
'''


class ShadowFocusTests(unittest.TestCase):
    def test_original_high_medium_profile_producer(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original profile table')
        try:
            import pefile
        except ImportError:
            self.skipTest('pefile is required for the original profile table')
        pe = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
        image = pe.get_memory_mapped_image()
        base = pe.OPTIONAL_HEADER.ImageBase
        table = 0x14f9ee0
        # 96ea90 indexes this table by quality*56, then calls 9cbd30.
        self.assertIn(struct.pack('<I', table), image[0x96ea90-base:0x96eab7-base])
        for quality, final_flag in ((1, 1), (2, 0)):
            row = image[table+quality*56-base:table+(quality+1)*56-base]
            self.assertEqual(row[0], 1)
            self.assertEqual(struct.unpack_from('<2I', row, 4), (0, 0))
            self.assertEqual(row[12:28], struct.pack('<4f', 70., 12., .6, .4))
            self.assertEqual(row[28:30], b'\x01\xfe')
            self.assertEqual(struct.unpack_from('<5I', row, 32), (2048, 1024, 1024, 0, 0))
            # sparse, lowQuality, intercascadeBlending, tree-quality flag.
            self.assertEqual(row[52:56], bytes((1, 0, 0, final_flag)))
        # 9cbfb6..9cc052 divides these scalars by physical atlas dimensions.
        self.assertEqual(image[0x1a1b2b4-base:0x1a1b2bc-base], struct.pack('<2f', .9, 1.3))

    def test_rectangle_matches_original_250_projected_boxes(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT is required for the original instruction oracle')
        try:
            import pefile  # noqa: F401
            import unicorn  # noqa: F401
        except ImportError:
            self.skipTest('pefile and Unicorn are required for the original instruction oracle')
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler required')
        # The helper is also directly importable by the baseline gate's
        # discovery entry point; no local cache or evidence-directory path.
        import importlib.util
        path = Path(__file__).with_name('stock_shadow_rectangle_oracle.py')
        spec = importlib.util.spec_from_file_location('shadow_rectangle_oracle', path)
        oracle = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(oracle)
        cases = list(oracle.projected_focus_boxes())
        expected = oracle.original_rectangles(client, cases)
        source = r'''
#include "wot/stock_shadow_rectangle.hpp"
#include <iostream>
int main(){unsigned count;
while(std::cin.read(reinterpret_cast<char*>(&count),4)){
    std::vector<wot::StockShadowPoint2> points(count);
    std::cin.read(reinterpret_cast<char*>(points.data()),count*8);
    const auto result=wot::stock_shadow_rectangle(points.data(),count);
    std::cout.write(reinterpret_cast<const char*>(&result),24);
}}
'''
        with tempfile.TemporaryDirectory(prefix='shadow-rectangle-oracle-') as temporary:
            root = Path(temporary)
            (root / 'probe.cpp').write_text(source)
            compiled = subprocess.run([compiler, '-std=c++14', '-ffp-contract=off', '-Wall', '-Wextra',
                                       '-Werror', '-I', str(ROOT / 'include'), str(root / 'probe.cpp'),
                                       '-o', str(root / 'probe')], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            payload = b''.join(struct.pack('<I', len(points)) +
                               struct.pack('<%df' % (len(points)*2),
                                           *[value for point in points for value in point])
                               for points in cases)
            result = subprocess.run([str(root / 'probe')], input=payload, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(len(result.stdout), len(expected)*24)
            for index, original in enumerate(expected):
                self.assertEqual(result.stdout[index*24:(index+1)*24], original,
                                 'Original rectangle mismatch at deterministic case %d' % index)

    def test_production_focus_lifecycle_and_tight_projection(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler required')
        source = (ROOT / 'bigworld/stock_directional_shadow.cpp').read_text()
        source = re.sub(r'^#include[^\n]*\n', '', source, flags=re.MULTILINE)
        cursor = (ROOT / 'bigworld/stock_cursor_camera.cpp').read_text()
        start = cursor.index('void publishStockCursorCameraHiding(')
        end = cursor.index('\n}', start) + 2
        publish = cursor[start:end]
        with tempfile.TemporaryDirectory(prefix='shadow-focus-') as temporary:
            path = Path(temporary)
            (path / 'probe.cpp').write_text(HOST.replace('// @PRODUCTION@', source).replace('// @PUBLISH@', publish))
            result = subprocess.run([compiler, '-std=c++14', '-ffp-contract=off', '-Wall', '-Wextra',
                                     '-Werror', '-I', str(ROOT / 'include'), str(path / 'probe.cpp'),
                                     '-o', str(path / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(path / 'probe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('600 tight-projection translation cases passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
