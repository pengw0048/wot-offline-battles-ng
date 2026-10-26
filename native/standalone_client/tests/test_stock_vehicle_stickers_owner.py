"""Exercise actual sticker publication and compound callbacks during retirement."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def block(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


class StickerOwnerTests(unittest.TestCase):
    def test_actual_reentrant_reset_replacement_and_transactional_publication(self):
        appearance = (ROOT / 'bigworld/vehicle_appearance.cpp').read_text()
        stickers = (ROOT / 'bigworld/stock_vehicle_stickers.cpp').read_text()
        host = r'''
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
using uint32=unsigned;
namespace BW {using string=std::string;}
struct Matrix {int value=7;void multiply(const Matrix&a,const Matrix&b){value=a.value+b.value;}};
struct Provider {std::function<void()> callback;void matrix(Matrix& m){if(callback)callback();m.value=42;}};
using MatrixProviderPtr=std::shared_ptr<Provider>;
struct Model {void* pSuperModel(){return this;}};
using PyObject=Model;
using PyModelPtr=std::shared_ptr<Model>;
using PyObjectPtr=std::shared_ptr<void>;
int PyExc_ValueError;
void PyErr_SetString(int,const char*){}
struct StockVehicleStickerGeometry {virtual ~StockVehicleStickerGeometry(){};virtual bool acquire(PyModelPtr&,Matrix&)=0;};
@NODE@;
@PART@;
struct CompoundAttachment;
using CompoundAttachmentPtr=std::shared_ptr<CompoundAttachment>;
@STATE@;
using CompoundStatePtr=std::shared_ptr<CompoundState>;
@GEOMETRY@;
struct StickerImage{};
struct Sticker{bool active=true;int id=0;};
@OWNER@;
struct StickerRenderer{bool open=true;};
std::shared_ptr<StickerRenderer> renderer;
struct Harness {std::shared_ptr<StickerOwner> owner_=std::make_shared<StickerOwner>();
@PUBLISH@
};
struct CallbackGeometry:StockVehicleStickerGeometry {
 std::function<void()> callback;
 bool acquire(PyModelPtr& m,Matrix&)override{if(callback)callback();m=std::make_shared<Model>();return true;}
};
int main(){
 auto state=std::make_shared<CompoundState>();state->nodes.resize(1);state->nodes[0].parent=-1;
 state->parts.resize(1);state->parts[0].model=std::make_shared<Model>();state->parts[0].mount=0;
 auto provider=std::make_shared<Provider>();state->matrix=provider;
 auto geometry=std::make_shared<CompoundStickerGeometry>(state,0);
 PyModelPtr model;Matrix matrix;assert(geometry->acquire(model,matrix)&&matrix.value==42);model.reset();
 std::weak_ptr<Model> original=state->parts[0].model;
 provider->callback=[&]{state->parts[0].model=std::make_shared<Model>();};
 assert(!geometry->acquire(model,matrix)&&original.expired());
 geometry=std::make_shared<CompoundStickerGeometry>(state,0);
 std::weak_ptr<Provider> resource=provider;
 provider->callback=[&]{state->active=false;state->parts.clear();state->matrix.reset();};provider.reset();
 assert(!geometry->acquire(model,matrix)&&resource.expired());
 assert(!geometry->acquire(model,matrix));
 state.reset();assert(!geometry->acquire(model,matrix));
 Harness owner;auto source=std::make_shared<CallbackGeometry>();owner.owner_->geometry=source;
 renderer=std::make_shared<StickerRenderer>();auto current=renderer;
 auto sticker=std::make_shared<Sticker>();
 assert(owner.publish(current,source,0,sticker)==1&&owner.owner_->stickers.size()==1);
 auto generation=owner.owner_->generation;
 source->callback=[&]{owner.owner_->clear();};
 assert(owner.publish(current,source,generation,std::make_shared<Sticker>())==-1);
 assert(!sticker->active&&owner.owner_->stickers.empty()&&owner.owner_->nextID==2);
 source->callback=[&]{renderer->open=false;renderer=std::make_shared<StickerRenderer>();};
 assert(owner.publish(current,source,owner.owner_->generation,std::make_shared<Sticker>())==-1);
 current=renderer;source->callback=[&]{owner.owner_->geometry=std::make_shared<CallbackGeometry>();};
 assert(owner.publish(current,source,owner.owner_->generation,std::make_shared<Sticker>())==-1);
 assert(owner.owner_->stickers.empty()&&owner.owner_->nextID==2);
}
'''
        for token, source, signature in (
                ('NODE', appearance, 'struct CompoundNode'),
                ('PART', appearance, 'struct CompoundPart'),
                ('STATE', appearance, 'struct CompoundState'),
                ('GEOMETRY', appearance, 'class CompoundStickerGeometry'),
                ('OWNER', stickers, 'struct StickerOwner'),
                ('PUBLISH', stickers, 'int publish(')):
            host = host.replace('@' + token + '@', block(source, signature))
        with tempfile.TemporaryDirectory(prefix='actual-sticker-owner-') as temporary:
            path = Path(temporary)
            (path / 'test.cpp').write_text(host)
            command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG']
            if sys.platform != 'win32':
                command += ['-fsanitize=address,undefined']
            subprocess.run(command + [str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
