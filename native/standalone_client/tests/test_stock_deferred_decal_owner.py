"""Execute the production shared-decal admission guard before device access."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DeferredDecalOwnerTests(unittest.TestCase):
    def test_actual_shared_draw_rejects_retired_or_missing_resources(self):
        source = (ROOT / 'bigworld/stock_space_decals.cpp').read_text()
        start = source.index('bool drawStockDeferredDecalPackets(')
        stop = source.index('    DecalDeviceScope states;', start)
        # Preserve the exact production early returns. The sentinel replaces
        # only the first device access and the remaining GPU operations.
        production = source[start:stop] + '''
    (void)techniques;
    (void)material;
    ++deviceEntries;
    return true;
}
'''
        probe = r'''
#include <cassert>
#include <limits>
#include <memory>
#include <string>
#include <vector>
struct StockDeferredDecalPacket {};
struct Resource {};
struct Owner {
    bool active=true,frame=true;
    std::shared_ptr<Resource> material,plane,cube;
    Owner():material(new Resource),plane(new Resource),cube(new Resource){}
};
std::shared_ptr<Owner> renderer;
unsigned failures=0,deviceEntries=0;
bool decalFailure(const char* message) {
    assert(std::string(message)=="Deferred decal drawing requires its current frame owner");
    ++failures;return false;
}
@PRODUCTION@
int main() {
    const std::vector<StockDeferredDecalPacket> packets(1),empty;
    assert(drawStockDeferredDecalPackets(empty,99));
    assert(failures==0&&deviceEntries==0);
    assert(!drawStockDeferredDecalPackets(packets,0));
    assert(failures==1&&deviceEntries==0);
    renderer=std::make_shared<Owner>();
    renderer->active=false;
    assert(!drawStockDeferredDecalPackets(packets,0));
    renderer->active=true;renderer->frame=false;
    assert(!drawStockDeferredDecalPackets(packets,0));
    renderer->frame=true;
    for(unsigned phase:{4u,std::numeric_limits<unsigned>::max()})
        assert(!drawStockDeferredDecalPackets(packets,phase));
    assert(failures==5&&deviceEntries==0);
    // Managed recreation may fail after active was established. No draw may
    // dereference the cleared material or reach the D3D device in that state.
    for(auto member:{&Owner::material,&Owner::plane,&Owner::cube}) {
        auto retained=renderer.get()->*member;
        (renderer.get()->*member).reset();
        const unsigned before=failures;
        assert(!drawStockDeferredDecalPackets(packets,0));
        assert(failures==before+1&&deviceEntries==0);
        assert(drawStockDeferredDecalPackets(empty,0));
        assert(failures==before+1&&deviceEntries==0);
        renderer.get()->*member=retained;
    }
    assert(failures==8);
    for(unsigned phase=0;phase<4;++phase)
        assert(drawStockDeferredDecalPackets(packets,phase));
    assert(deviceEntries==4&&failures==8);
    // A later managed-resource recreation restores admission without changing
    // the independent active/frame lifecycle flags.
    renderer->material.reset();renderer->plane.reset();renderer->cube.reset();
    assert(!drawStockDeferredDecalPackets(packets,0)&&deviceEntries==4);
    renderer=std::make_shared<Owner>();
    assert(drawStockDeferredDecalPackets(packets,0));
    assert(deviceEntries==5&&failures==9);
}
'''.replace('@PRODUCTION@', production)
        with tempfile.TemporaryDirectory(prefix='shared-decal-owner-') as directory:
            cpp, binary = Path(directory) / 'probe.cpp', Path(directory) / 'probe'
            cpp.write_text(probe)
            command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG']
            if sys.platform != 'win32':
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            compiled = subprocess.run(command + [str(cpp), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(executed.returncode, 0, executed.stderr)


if __name__ == '__main__':
    unittest.main()
