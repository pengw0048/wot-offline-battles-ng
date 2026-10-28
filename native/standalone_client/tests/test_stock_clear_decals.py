"""BigWorld.wg_clearDecals against the #1513 entry, its consumers and the production owners.

The host compiles unmodified production clear functions. The PE and PYC checks
pin the original scope; they skip without the pinned client and tools.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CLIENT = os.environ.get('WOT_0922_CLIENT')


def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


def line(source, text):
    return next(entry for entry in source.splitlines() if entry.startswith(text))


HOST = r'''
#include "wot/stock_vehicle_traces.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>
struct IRendererPipeline { enum EType { TYPE_DEFERRED_SHADING, TYPE_FORWARD_SHADING }; };
struct Renderer
{
    bool inited = true;
    IRendererPipeline::EType type = IRendererPipeline::TYPE_DEFERRED_SHADING;
    static Renderer* current;
    static Renderer* pInstance() { return current; }
    IRendererPipeline::EType pipelineType() const { assert(inited); return type; }
};
Renderer* Renderer::current = nullptr;
// Owners outside 931650's scope; production must never reach them.
struct Untouched { int publicDynamic = 0, forwardSpace = 0, staticScene = 0; } untouched;
struct DecalsManager
{
    static DecalsManager& instance() { static DecalsManager value; return value; }
    void clearDynamicDecals() { ++untouched.publicDynamic; }
};
void clearDecals() { ++untouched.forwardSpace; }
void finaliseStockSpaceDecals() { ++untouched.staticScene; }

struct Space {};
struct DynamicTextureSet {};
struct DynamicDecal { float lifetime; std::shared_ptr<DynamicTextureSet> textures; };
struct DynamicDecalRenderer
{
    std::vector<DynamicDecal> decals;
    std::map<int, std::weak_ptr<DynamicTextureSet>> textures;
    std::shared_ptr<Space> space;
    float time = 0.f;
    int quality = 0;
};
std::shared_ptr<DynamicDecalRenderer> renderer;
@DYNAMIC@

struct TraceTextureLayout { int key; };
struct TraceRenderer
{
    wot::StockVehicleTraceManager manager;
    std::vector<TraceTextureLayout> layouts;
    std::shared_ptr<Space> space;
    @TRACE_CLEAR@
};
std::unique_ptr<TraceRenderer> traceRenderer;
@TRACE@
@BINDING@

wot::StockTraceSegment segment(int layout)
{
    wot::StockTraceSegment value;
    value.layout = layout; value.lifetime = 10.f; value.direction = {0.f, 0.f, 1.f};
    return value;
}

int main()
{
    // Before Renderer exists and before the deferred owners exist.
    wg_clearDecals();
    Renderer deferred; Renderer::current = &deferred;
    wg_clearDecals();
    assert(!renderer && !traceRenderer);

    auto space = std::make_shared<Space>();
    renderer = std::make_shared<DynamicDecalRenderer>();
    renderer->space = space; renderer->time = 7.f; renderer->quality = 2;
    auto texture = std::make_shared<DynamicTextureSet>();
    std::weak_ptr<DynamicTextureSet> resource = texture;
    for (int index = 0; index != 3; ++index) renderer->decals.push_back({float(index + 1), texture});
    renderer->textures[1] = texture;
    texture.reset();
    traceRenderer.reset(new TraceRenderer);
    traceRenderer->space = space;
    traceRenderer->manager.setQuality(1);
    std::uint32_t left = ~0u, right = ~0u;
    traceRenderer->manager.add(left, segment(0));
    traceRenderer->manager.add(right, segment(1));
    traceRenderer->manager.tick(2.f, {0.f, 0.f, 0.f}, 1.f);
    traceRenderer->layouts = {{1}, {2}};
    assert(traceRenderer->manager.strips().size() == 2 && traceRenderer->manager.time() == 2.f);
    const std::uint32_t retired = right;

    // ForwardPipeline has no decal owner: the call changes nothing.
    deferred.type = IRendererPipeline::TYPE_FORWARD_SHADING;
    wg_clearDecals();
    assert(renderer->decals.size() == 3 && traceRenderer->manager.strips().size() == 2 && traceRenderer->layouts.size() == 2);
    deferred.type = IRendererPipeline::TYPE_DEFERRED_SHADING;

    wg_clearDecals();
    assert(renderer->decals.empty() && resource.expired());
    assert(renderer->time == 7.f && renderer->quality == 2 && renderer->space == space && renderer->textures.size() == 1);
    assert(traceRenderer->manager.strips().empty() && traceRenderer->layouts.empty());
    assert(traceRenderer->manager.time() == 0.f && traceRenderer->manager.quality == 1 && traceRenderer->space == space);
    // The generation changed, so a component token starts a fresh strip at index 0.
    traceRenderer->manager.add(right, segment(1));
    assert(right != retired && (right & 0xffffu) == 0 && traceRenderer->manager.strips().size() == 1);

    // A repeated clear is idempotent for records and retires the new token too.
    const std::uint32_t second = right;
    wg_clearDecals();
    wg_clearDecals();
    assert(renderer->decals.empty() && traceRenderer->manager.strips().empty());
    traceRenderer->manager.add(right, segment(1));
    assert(right != second && traceRenderer->manager.strips().size() == 1);
    renderer->decals.push_back({1.f, nullptr});
    assert(renderer->decals.size() == 1);

    // DecalsManager and DeferredPipeline teardown reset both owners first.
    renderer.reset(); traceRenderer.reset();
    wg_clearDecals();
    Renderer::current = nullptr;
    wg_clearDecals();
    assert(!untouched.publicDynamic && !untouched.forwardSpace && !untouched.staticScene);
    std::puts("wg_clearDecals scope passed");
}
'''


class ClearDecalsTests(unittest.TestCase):
    def test_production_clear_reaches_only_dynamic_and_trace_owners(self):
        bigworld = ROOT / 'bigworld'
        resources = (bigworld / 'stock_decal_resources.cpp').read_text()
        dynamic = (bigworld / 'stock_dynamic_decals.cpp').read_text()
        traces = (bigworld / 'stock_vehicle_traces.cpp').read_text()
        self.assertIn('PY_AUTO_MODULE_FUNCTION(RETVOID, wg_clearDecals, END, BigWorld)', resources)
        host = (HOST.replace('@DYNAMIC@', line(dynamic, 'void clearStockDynamicDecals()'))
                .replace('@TRACE_CLEAR@', line(traces, '    void clear(){').strip())
                .replace('@TRACE@', line(traces, 'void clearStockVehicleTraceRenderer()'))
                .replace('@BINDING@', method(resources, 'void wg_clearDecals()')))
        with tempfile.TemporaryDirectory(prefix='wg-clear-decals-') as directory:
            directory = Path(directory)
            (directory / 'host.cpp').write_text(host)
            command = ['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG', '-I' + str(ROOT / 'include')]
            if sys.platform != 'win32':
                command += ['-fsanitize=address,undefined']
            subprocess.run(command + [str(directory / 'host.cpp'), '-o', str(directory / 'host')], check=True)
            result = subprocess.run([str(directory / 'host')], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('scope passed', result.stdout)

    def test_original_consumers_call_without_arguments_and_discard_the_result(self):
        python27 = os.environ.get('PY27') or shutil.which('python2.7')
        if not CLIENT or not python27:
            self.skipTest('Pinned client and Python 2.7 are required')
        script = r'''
import dis, marshal, sys, zipfile
package = zipfile.ZipFile(sys.argv[1] + '/res/packages/scripts.pkg')
def functions(code):
    for value in code.co_consts:
        if hasattr(value, 'co_code'):
            yield value
            for nested in functions(value): yield nested
def calls(code):
    raw, names, found = code.co_code, code.co_names, []
    index = 0
    ops = []
    while index < len(raw):
        op = ord(raw[index]); argument = None
        if op >= dis.HAVE_ARGUMENT: argument = ord(raw[index + 1]) | ord(raw[index + 2]) << 8; index += 3
        else: index += 1
        ops.append((dis.opname[op], argument))
    for position in range(len(ops) - 3):
        if ops[position + 1] == ('LOAD_ATTR', names.index('wg_clearDecals') if 'wg_clearDecals' in names else -1):
            found.append((names[ops[position][1]], ops[position + 2], ops[position + 3][0]))
    return found
for member, name in (('scripts/client/Avatar.pyc', 'onBecomePlayer'), ('scripts/client/Avatar.pyc', 'onBecomeNonPlayer'),
                     ('scripts/client/BattleReplay.pyc', '__cleanupAfterTimeWarp')):
    code = marshal.loads(package.read(member)[8:])
    sites = [site for function in functions(code) if function.co_name == name for site in calls(function)]
    print(member, name, sites)
    assert sites == [('BigWorld', ('CALL_FUNCTION', 0), 'POP_TOP')], sites
'''
        result = subprocess.run([python27, '-c', script, CLIENT], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_original_entry_clears_dynamic_and_trace_managers_only(self):
        try:
            import capstone
            import pefile
        except ImportError:
            self.skipTest('pefile and capstone are required')
        if not CLIENT:
            self.skipTest('Pinned client is required')
        image = pefile.PE(str(Path(CLIENT) / 'WorldOfTanks.exe'), fast_load=True)
        base, memory = image.OPTIONAL_HEADER.ImageBase, image.get_memory_mapped_image()
        engine = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

        def code(address, end):
            return [(item.mnemonic + ' ' + item.op_str).strip()
                    for item in engine.disasm(memory[address - base:end - base], address)]

        def word(address):
            return int.from_bytes(memory[address - base:address - base + 4], 'little')

        # 5bf8b0 registers BigWorld.wg_clearDecals with its PY_AUTO entry.
        self.assertEqual(code(0x5bf8b0, 0x5bf8c1), ['push 0', 'push 0x931650', 'push 0x14f78cc', 'push 0x14994e4'])
        self.assertEqual(bytes(memory[0x14f78cc - base:0x14f78db - base]), b'wg_clearDecals\0')
        entry = code(0x931650, 0x9316ca)
        self.assertEqual(entry[:5], ['push ebp', 'mov ebp, esp', 'push dword ptr [ebp + 0xc]', 'call 0xfb3610', 'add esp, 4'])
        for expected in ('mov ecx, dword ptr [0x1d20620]', 'call 0xa49a80', 'call dword ptr [edx + 0x8c]',
                         'call 0x13415f0', 'call 0x673600', 'call 0xa61280', 'inc dword ptr [0x1a5c798]',
                         'mov eax, 0x1a5c798', 'call 0x68e100'):
            self.assertIn(expected, entry)
        # Accessors: Renderer m_type, owner +8 DynamicManager, owner +0xc TraceManager, count store.
        self.assertEqual(code(0xa49a80, 0xa49a84), ['mov eax, dword ptr [ecx + 8]', 'ret'])
        self.assertEqual(code(0x673600, 0x673604), ['mov eax, dword ptr [ecx + 0xc]', 'ret'])
        self.assertEqual(code(0x13415f0, 0x13415f8), ['mov dword ptr [ecx + 4], 0', 'ret'])
        # DeferredPipeline slot 35 returns its decal owner; ForwardPipeline has none.
        self.assertEqual(code(word(0x14fd3b0 + 0x8c), word(0x14fd3b0 + 0x8c) + 7), ['mov eax, dword ptr [ecx + 0x88]', 'ret'])
        self.assertEqual(code(word(0x14fd894 + 0x8c), word(0x14fd894 + 0x8c) + 3), ['xor eax, eax', 'ret'])
        # The owner constructor keeps Scene at +4, Dynamic at +8 and Trace at +0xc.
        owner = code(0xa493b0, 0xa49600)
        for expected in ('lea edi, [esi + 4]', 'lea ebx, [esi + 8]', 'call 0xa4ada0', 'call 0xa5e5a0',
                         'call 0xa60870', 'lea eax, [esi + 0xc]'):
            self.assertIn(expected, owner)
        # a61280 is the same instruction sequence as the clear tail of TraceManager quality selection a62c40.
        def shape(start, end):
            local = lambda match: 'L%x' % (int(match.group(), 16) - start) if start <= int(match.group(), 16) < end else match.group()
            return [re.sub(r'0x[0-9a-f]+', local, re.sub(r'\b(e[abcd]x|e[sd]i)\b', 'r', entry))
                    for entry in code(start, end)]
        self.assertEqual(shape(0xa612a7, 0xa612fb), shape(0xa62ccb, 0xa62d1f))
        self.assertEqual(code(0xa612e6, 0xa612fb), ['add dword ptr [ebx + 0x2c], 0x10000',
                                                     'mov dword ptr [ebx + 0x18], 0', 'mov dword ptr [ebx + 0x28], 0'])

if __name__ == '__main__':
    unittest.main()
