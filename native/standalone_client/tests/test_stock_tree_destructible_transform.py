"""Production old-ST4 transform producer, GPU constants and actual BSP bound reader."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_stock_ground_decals import ROOT,compiler_command
from test_stock_space_models import definition

PRELUDE=r'''
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>
namespace BW {
using uint32=unsigned;using string=std::string;
struct Vector3 {float x=0,y=0,z=0;Vector3(){}Vector3(float a,float b,float c):x(a),y(b),z(c){}float operator[](unsigned n)const{return (&x)[n];}};
struct Quaternion {float x=0,y=0,z=0,w=1;};
struct Matrix {float v[16];Matrix(){std::memset(v,0,64);v[0]=v[5]=v[10]=v[15]=1;}
 bool invert(const Matrix& input){if(input.v[0]*input.v[5]*input.v[10]==0){return false;}*this=input;return true;}
};
// BlendTransform is the existing SDK decomposition boundary, independently
// covered by stock_tree_transform_probe with original Win32 D3DX operands.
struct BlendTransform {Quaternion q;Vector3 p,s;explicit BlendTransform(const Matrix& m):p(m.v[12],m.v[13],m.v[14]),s(m.v[0],m.v[5],m.v[10]){}
 const Quaternion& rotation()const{return q;}const Vector3& translation()const{return p;}const Vector3& scaling()const{return s;}};
struct BoundingBox {Vector3 low{1,1,1},high{-1,-1,-1};static const BoundingBox s_insideOut_;
 void transformBy(const Matrix& m){for(unsigned n=0;n<3;++n){(&low.x)[n]=(&low.x)[n]*m.v[n*5]+m.v[12+n];(&high.x)[n]=(&high.x)[n]*m.v[n*5]+m.v[12+n];}}
};const BoundingBox BoundingBox::s_insideOut_{};
struct BSPTree {BoundingBox boundingBox()const{BoundingBox b;b.low={-1,-2,-3};b.high={1,2,3};return b;}};
struct TreeType {std::shared_ptr<BSPTree> collision;};
struct TreeDefinition {Matrix world;};
float mulTreeScalar(float a,float b){volatile float r=a*b;return r;}float addTreeScalar(float a,float b){volatile float r=a+b;return r;}
@CONSTANTS@
@INSTANCE@
@HELPER@
namespace CompiledSpace {
class StockTreeSceneHandler {public:struct Implementation {bool loaded=true;std::vector<TreeInstance> instances;};
 std::unique_ptr<Implementation> implementation_{new Implementation};
 bool setDestructibleTransform(size_t,const Matrix&,float,std::string&);
 void worldCollisionBounds(size_t,BoundingBox&)const;
};
@SETTER@
@BOUNDS@
}
}
int main(){using namespace BW;using namespace BW::CompiledSpace;
 StockTreeSceneHandler owner;owner.implementation_->instances.resize(2);auto& tree=owner.implementation_->instances[0];
 tree.type=std::make_shared<TreeType>();tree.type->collision=std::make_shared<BSPTree>();tree.constants.windOffset=7;
 Matrix pose;pose.v[0]=2;pose.v[5]=3;pose.v[10]=4;pose.v[12]=41;pose.v[13]=5;pose.v[14]=-7;std::string error;
 assert(owner.setDestructibleTransform(0,pose,.25f,error));assert(std::memcmp(&tree.definition.world,&pose,64)==0);
 assert(tree.constants.position[0]==41&&tree.constants.position[1]==5&&tree.constants.position[2]==-7);
 assert(tree.constants.scale[0]==2&&tree.constants.scale[1]==3&&tree.constants.scale[2]==4);
 assert(tree.constants.uniformScale==mulTreeScalar(9.f,.333f)&&tree.constants.rotation[3]==1);
 // Both opaque and semitransparent draw paths submit this exact64-byte record.
 float packet[16];std::memcpy(packet,&tree.constants,64);assert(packet[12]==7&&packet[15]==.25f);
 BoundingBox bounds;owner.worldCollisionBounds(0,bounds);assert(bounds.low.x==39&&bounds.high.x==43&&bounds.low.y==-1&&bounds.high.y==11);
 owner.worldCollisionBounds(1,bounds);assert(bounds.low.x>bounds.high.x); // No BSP => no fabricated collider.
 assert(owner.setDestructibleTransform(1,pose,0,error));assert(owner.implementation_->instances[1].definition.world.v[12]==41);
 Matrix before=tree.definition.world;pose.v[3]=std::numeric_limits<float>::quiet_NaN();
 assert(!owner.setDestructibleTransform(0,pose,0,error)&&std::memcmp(&tree.definition.world,&before,64)==0);
 pose=before;assert(!owner.setDestructibleTransform(0,pose,std::numeric_limits<float>::infinity(),error));
 pose.v[0]=0;assert(!owner.setDestructibleTransform(0,pose,0,error));
 assert(!owner.setDestructibleTransform(2,before,0,error));owner.implementation_->loaded=false;assert(!owner.setDestructibleTransform(0,before,0,error));
}
'''


class TreeDestructibleTransformTests(unittest.TestCase):
    def test_production_transform_constant_and_bsp_consumers(self):
        production=(ROOT/'bigworld/stock_tree_scene.cpp').read_text()
        replacements={
            '@CONSTANTS@':definition(production,'struct TreeInstanceConstants')+';',
            '@INSTANCE@':definition(production,'struct TreeInstance\n')+';',
            '@HELPER@':definition(production,'bool updateTreeInstanceTransform('),
            '@SETTER@':definition(production,'bool StockTreeSceneHandler::setDestructibleTransform('),
            '@BOUNDS@':definition(production,'void StockTreeSceneHandler::worldCollisionBounds('),
        }
        source=PRELUDE
        for tag,text in replacements.items():source=source.replace(tag,text)
        with tempfile.TemporaryDirectory(prefix='stock-tree-destructible-transform-')as name:
            path=Path(name)/'probe.cpp';binary=Path(name)/'probe';path.write_text(source)
            subprocess.run(compiler_command(self)+[str(path),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)


if __name__=='__main__':unittest.main()
