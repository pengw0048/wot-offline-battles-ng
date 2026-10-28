"""CursorCamera tree hiding: production publishers with the old-ST4 consumers.

The fade, LineInEllipsoid and target snapshot are compared bit for bit with
the #1513 instructions. The lifecycle harness compiles the production
publisher and consumer functions against recording fakes. Neither proves
rendered output; Windows acceptance owns that.
"""
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest

TESTS = Path(__file__).resolve().parent
sys.path.insert(0, str(TESTS))
from test_stock_cursor_camera import (ROOT, CLIENT, ORACLE_PYTHON, compile_host, fresh_engine, function,
                                      load_patch, requirements)


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def unbits(word):
    return struct.unpack('<f', struct.pack('<I', word))[0]


PROBE = r'''
#include "stock_tree_hiding.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>

static float f( unsigned word ) { float v; std::memcpy( &v, &word, 4 ); return v; }
static unsigned w( float v ) { unsigned word; std::memcpy( &word, &v, 4 ); return word; }

// stock_cursor_camera_oracle.CameraMath: the shared float32 D3DX definitions.
struct HostD3DX
{
    static void transformNormal( float * out, const float * v, const float * m )
    {
        for (int c = 0; c < 3; ++c) out[c] = (v[0] * m[c] + v[1] * m[4 + c]) + v[2] * m[8 + c];
    }
    static void inverse( float * m )
    {
        const float det = (m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8])) +
            m[2] * (m[4] * m[9] - m[5] * m[8]);
        if (det == 0.f) return;
        const float rcp = 1.f / det;
        float r[16] = {};
        r[0] = m[5] * m[10] - m[6] * m[9]; r[1] = m[2] * m[9] - m[1] * m[10]; r[2] = m[1] * m[6] - m[2] * m[5];
        r[4] = m[6] * m[8] - m[4] * m[10]; r[5] = m[0] * m[10] - m[2] * m[8]; r[6] = m[2] * m[4] - m[0] * m[6];
        r[8] = m[4] * m[9] - m[5] * m[8]; r[9] = m[1] * m[8] - m[0] * m[9]; r[10] = m[0] * m[5] - m[1] * m[4];
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r[i * 4 + j] *= rcp;
        for (int j = 0; j < 3; ++j) r[12 + j] = -((m[12] * r[j] + m[13] * r[4 + j]) + m[14] * r[8 + j]);
        r[15] = 1.f;
        std::memcpy( m, r, sizeof( r ) );
    }
    static void transformPoint( float * out, const float * v, const float * m )
    {
        for (int c = 0; c < 3; ++c) out[c] = ((v[0] * m[c] + v[1] * m[4 + c]) + v[2] * m[8 + c]) + m[12 + c];
    }
};

int main()
{
    char kind;
    while (std::scanf( " %c", &kind ) == 1)
    {
        unsigned u[40];
        if (kind == 'L')
        {
            for (int i = 0; i < 9; ++i) std::scanf( "%x", &u[i] );
            float v[9];
            for (int i = 0; i < 9; ++i) v[i] = f( u[i] );
            const double r = wot::stockTreeLineInEllipsoid( v, v + 3, v + 6 );
            std::uint64_t word;
            std::memcpy( &word, &r, 8 );
            std::printf( "%016llx\n", (unsigned long long)word );
        }
        else if (kind == 'C')
        {
            for (int i = 0; i < 19; ++i) std::scanf( "%x", &u[i] );
            float v[19];
            for (int i = 0; i < 19; ++i) v[i] = f( u[i] );
            wot::StockTreeHidingFrame frame;
            wot::captureStockTreeHiding( v, v + 16, frame );
            std::printf( "%08x %08x %08x %08x %08x %08x %08x\n", w( frame.camera[0] ), w( frame.camera[1] ),
                w( frame.camera[2] ), w( frame.target[0] ), w( frame.target[1] ), w( frame.target[2] ),
                w( frame.distance ) );
        }
        else
        {
            // F hide closest squared radius alpha transparency frame? camera target distance
            //   world16 min3 max3 eye3 lod bush model3d
            for (int i = 0; i < 39; ++i) std::scanf( "%x", &u[i] );
            wot::StockTreeHidingSettings settings;
            settings.hideClosest = u[0] != 0;
            settings.squaredHideRadius = f( u[1] );
            settings.hideRadius = f( u[2] );
            settings.hideRadiusAlpha = f( u[3] );
            settings.transparency = u[4] != 0;
            wot::StockTreeHidingFrame frame;
            for (int i = 0; i < 3; ++i) { frame.camera[i] = f( u[6 + i] ); frame.target[i] = f( u[9 + i] ); }
            frame.distance = f( u[12] );
            float world[16];
            for (int i = 0; i < 16; ++i) world[i] = f( u[13 + i] );
            wot::StockTreeFadeInput input = {};
            input.world = world;
            for (int i = 0; i < 3; ++i)
            {
                input.boundsMin[i] = f( u[29 + i] );
                input.boundsMax[i] = f( u[32 + i] );
                input.eye[i] = f( u[35 + i] );
            }
            input.lodDistance = f( u[38] );
            unsigned flags;
            std::scanf( "%x", &flags );
            input.bush = (flags & 1) != 0;
            input.model3d = (flags & 2) != 0;
            const wot::StockTreeFade fade = wot::computeStockTreeFade<HostD3DX>( settings, u[5] ? &frame : NULL, input );
            std::printf( "%08x %08x %d\n", w( fade.leaves ), w( fade.branches ), int( fade.faded() ) );
        }
    }
    return 0;
}
'''


ORACLE_DRIVER = r'''
import json, sys
sys.path.insert(0, sys.argv[1])
from stock_tree_hiding_oracle import TreeHidingOracle
oracle = TreeHidingOracle(sys.argv[2])
cases = json.loads(sys.stdin.read())
out = {'L': [], 'F': [], 'C': []}
for size, start, end in cases['L']:
    out['L'].append(oracle.line_in_ellipsoid(size, start, end).hex())
for case in cases['F']:
    out['F'].append(oracle.compute_fade(case))
for matrix, eye in cases['C']:
    out['C'].append(oracle.tick(matrix, eye))
print(json.dumps(out))
'''


def f32(value):
    return unbits(bits(value))


def rotation_y(angle, scale=1.0, translation=(0.0, 0.0, 0.0)):
    import math
    c, s = math.cos(angle) * scale, math.sin(angle) * scale
    return [f32(v) for v in (c, 0, -s, 0, 0, scale, 0, 0, s, 0, c, 0) + tuple(translation) + (1.0,)]


def hiding_cases():
    rng = random.Random(1513)
    lines = [((2.0, 3.0, 2.0), (-5.0, 0.5, 0.0), (5.0, 0.2, 0.3)), ((2.0, 3.0, 2.0), (-5.0, 9.5, 0.0), (5.0, 9.2, 0.3)),
             ((1.0, 1.0, 1.0), (0.0, 0.0, 0.0), (0.0, 0.0, 0.0)), ((1.0, 2.0, 1.0), (0.2, 0.1, 0.0), (0.3, 0.2, 0.1)),
             ((0.0, 1.0, 1.0), (1.0, 0.0, 0.0), (2.0, 0.0, 0.0))]
    for unused in range(400):
        size = tuple(f32(rng.uniform(0.3, 9.0)) for unused in range(3))
        lines.append((size, tuple(f32(rng.uniform(-12, 12)) for unused in range(3)),
                      tuple(f32(rng.uniform(-12, 12)) for unused in range(3))))
    default = dict(hideClosest=False, squared=0.0, radius=0.0, alphaRadius=0.0, transparency=True)
    sniper = dict(hideClosest=True, squared=225.0, radius=15.0, alphaRadius=10.0, transparency=True)
    fades = []
    for index in range(300):
        camera = [f32(rng.uniform(-40, 40)), f32(rng.uniform(0, 12)), f32(rng.uniform(-40, 40))]
        target = [f32(rng.uniform(-40, 40)), f32(rng.uniform(0, 4)), f32(rng.uniform(-40, 40))]
        # Most trees stand near the camera-target segment, some near the eye.
        along = rng.uniform(-0.2, 1.2)
        tree = tuple(f32(c + along * (t - c) + rng.uniform(-6, 6)) for c, t in zip(camera, target))
        tree = (tree[0], f32(rng.uniform(-2, 2)), tree[2])
        world = rotation_y(rng.uniform(-3, 3), f32(rng.uniform(0.6, 1.6)), tree)
        low = [f32(rng.uniform(-4, -0.5)), f32(rng.uniform(-0.5, 0.5)), f32(rng.uniform(-4, -0.5))]
        high = [f32(rng.uniform(0.5, 4)), f32(rng.uniform(2, 18)), f32(rng.uniform(0.5, 4))]
        distance = f32(sum((a - b) ** 2 for a, b in zip(camera, target)) ** 0.5)
        lod = f32(rng.uniform(0, 80)) if index % 7 else 1e6
        settings = (default, sniper, dict(default, transparency=False))[index % 3 if index % 11 else 1]
        fades.append(dict(lod=lod, settings=settings, min=low, max=high, eye=camera, world=world,
                          frame=dict(camera=camera, target=target, distance=distance) if index % 13 else None,
                          bush=index % 17 == 0, model3d=index % 19 != 0))
    # A tree exactly between the camera and its target, then edge cases.
    between = dict(lod=15.0, settings=default, min=[-3.0, 0.0, -3.0], max=[3.0, 12.0, 3.0], eye=[0.0, 2.0, 0.0],
                   world=rotation_y(0.0, 1.0, (10.0, 0.0, 0.0)), bush=False, model3d=True,
                   frame=dict(camera=[0.0, 2.0, 0.0], target=[20.0, 1.0, 0.0], distance=20.0))
    fades += [between, dict(between, min=[3.0, 0.0, -3.0], max=[-3.0, 12.0, 3.0]),
              dict(between, max=[3.0, 5.0, 3.0]), dict(between, lod=40.0), dict(between, bush=True),
              dict(between, model3d=False), dict(between, frame=None),
              dict(between, world=rotation_y(0.0, 1.0, (10.0, 0.0, 6.0))),
              dict(between, settings=sniper, eye=[8.0, 2.0, 0.0])]
    captures = [(rotation_y(0.4, 1.0, (20.0, 0.25, -3.0)), [1.0, 2.0, 3.0]),
                (rotation_y(1.1, 1.3, (-7.5, 4.0, 11.0)), [-2.0, 30.0, 0.125])]
    return dict(L=lines, F=fades, C=captures)


def probe_input(cases):
    rows = []
    for size, start, end in cases['L']:
        rows.append('L ' + ' '.join('%08x' % bits(v) for v in size + start + end))
    for case in cases['F']:
        s, frame = case['settings'], case['frame'] or dict(camera=[0.0] * 3, target=[0.0] * 3, distance=0.0)
        words = [int(s['hideClosest']), bits(s['squared']), bits(s['radius']), bits(s['alphaRadius']),
                 int(s['transparency']), int(case['frame'] is not None)]
        words += [bits(v) for v in frame['camera'] + frame['target'] + [frame['distance']]]
        words += [bits(v) for v in case['world'] + case['min'] + case['max'] + case['eye'] + [case['lod']]]
        words.append(int(case['bush']) | 2 * int(case['model3d']))
        rows.append('F ' + ' '.join('%x' % word for word in words))
    for matrix, eye in cases['C']:
        rows.append('C ' + ' '.join('%08x' % bits(v) for v in matrix + eye))
    return '\n'.join(rows) + '\n'


class OriginalHidingInstructionTests(unittest.TestCase):
    def test_line_ellipsoid_fade_and_snapshot_match_the_original(self):
        requirements(self)
        cases = hiding_cases()
        run = subprocess.run([str(ORACLE_PYTHON), '-c', ORACLE_DRIVER, str(TESTS), str(CLIENT / 'WorldOfTanks.exe')],
                             input=json.dumps(cases), capture_output=True, text=True,
                             env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        original = json.loads(run.stdout)
        with tempfile.TemporaryDirectory() as folder:
            source = PROBE.replace('#include "stock_tree_hiding.hpp"',
                                   (ROOT / 'bigworld/stock_tree_hiding.hpp').read_text())
            binary = compile_host(Path(folder), source, 'hiding_probe')
            result = subprocess.run([str(binary)], input=probe_input(cases), capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = result.stdout.splitlines()
        lines, fades, captures = (rows[:len(cases['L'])], rows[len(cases['L']):len(cases['L']) + len(cases['F'])],
                                  rows[len(cases['L']) + len(cases['F']):])
        self.assertEqual([float.fromhex(v).hex() for v in original['L']],
                         [struct.unpack('<d', bytes.fromhex(line)[::-1])[0].hex() for line in lines])
        produced = [[int(word, 16) for word in line.split()[:2]] for line in fades]
        self.assertEqual([[bits(v) for v in pair] for pair in original['F']], produced)
        expected = [[bits(v) for v in camera + target + [distance]] for camera, target, distance in original['C']]
        self.assertEqual(expected, [[int(word, 16) for word in line.split()] for line in captures])
        # The corpus exercises every branch: unclamped, both clamps, unfaded.
        faded = [line.split() for line in fades]
        self.assertTrue(any(row[2] == '0' for row in faded))
        self.assertTrue(any(unbits(int(row[0], 16)) == f32(0.4) for row in faded))
        self.assertTrue(any(0.7 < unbits(int(row[1], 16)) < 1.0 for row in faded))
        self.assertTrue(any(0.0 < float.fromhex(v) < 1.0 for v in original['L']))
        # The tree between the camera and its target is hidden; flags cancel it.
        between = produced[-9:]
        self.assertEqual(between[0], [bits(0.4), bits(0.7)])
        self.assertEqual(between[1:7], [[bits(1.0), bits(1.0)]] * 6)


LIFECYCLE_HOST = r'''
#include "stock_tree_hiding.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define NULL nullptr

std::vector<std::string> events;
static void event( const std::string & text ) { events.push_back( text ); }

typedef unsigned uint32;
struct Matrix
{
    float m[16];
    static const Matrix identity;
    float * row( int i ) { return m + 4 * i; }
};
const Matrix Matrix::identity = {{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }};
struct Vector3 { float x, y, z; float operator[]( int i ) const { return (&x)[i]; } };

// A Python-backed provider: references, reentrant matrix() and destruction.
struct MatrixProvider
{
    int references = 0, label = 0;
    Matrix world = Matrix::identity;
    std::function<void()> onMatrix, onDestroy;
    bool dynamic = false, destroyed = false;
    void matrix( Matrix & m ) const
    {
        event( "matrix " + std::to_string( label ) );
        if (onMatrix) { auto call = onMatrix; call(); }
        m = world;
    }
};
static void release( MatrixProvider * p )
{
    if (p && --p->references == 0 && p->dynamic)
    {
        if (p->destroyed) event( "double destroy" );
        p->destroyed = true;
        event( "destroy " + std::to_string( p->label ) );
        if (p->onDestroy) { auto call = p->onDestroy; call(); }
    }
}
class MatrixProviderPtr
{
public:
    MatrixProviderPtr( MatrixProvider * p = nullptr ) : p_( p ) { if (p_) ++p_->references; }
    MatrixProviderPtr( const MatrixProviderPtr & o ) : p_( o.p_ ) { if (p_) ++p_->references; }
    MatrixProviderPtr & operator=( const MatrixProviderPtr & o )
    {
        MatrixProvider * old = p_;
        p_ = o.p_;
        if (p_) ++p_->references;
        release( old );
        return *this;
    }
    ~MatrixProviderPtr() { release( p_ ); }
    MatrixProvider * get() const { return p_; }
    MatrixProvider * operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
private:
    MatrixProvider * p_;
};

namespace Moo
{
enum { CONSTANTS_PER_VIEW = 4 };
struct EffectVisualContext { void updateSharedConstants( int kind ) { event( "constants " + std::to_string( kind ) ); } };
struct RenderContext
{
    Matrix view_ = Matrix::identity, projection_ = Matrix::identity, invView_ = Matrix::identity;
    EffectVisualContext context;
    const Matrix & view() const { return view_; }
    const Matrix & projection() const { return projection_; }
    void view( const Matrix & m ) { view_ = m; event( "view " + std::to_string( int( m.m[12] ) ) ); }
    void projection( const Matrix & m ) { projection_ = m; event( "projection " + std::to_string( int( m.m[0] ) ) ); }
    void updateViewTransforms() { event( "transforms" ); }
    EffectVisualContext & effectVisualContext() { return context; }
    struct Inverse { const Matrix * m; Vector3 applyToOrigin() const { return { m->m[12], m->m[13], m->m[14] }; } };
    Inverse invView() const { return { &invView_ }; }
};
inline RenderContext & rc() { static RenderContext value; return value; }
struct EffectMaterial { int state, part; };
}

struct IRendererPipeline
{
    enum { STENCIL_USAGE_SPEEDTREE = 1 << 5 };
    void setupSystemStencil( int usage ) { event( "stencil " + std::to_string( usage ) ); }
    void restoreSystemStencil() { event( "restore stencil" ); }
};
struct Renderer
{
    IRendererPipeline * pipeline_ = nullptr;
    static Renderer & instance() { static Renderer value; return value; }
    IRendererPipeline * pipeline() { return pipeline_; }
};

namespace wot { enum { treeBranch, treeFrond, treeLeaf, treeBillboard }; struct StockTreeLodValues { int branch = 0; }; }

struct TreeInstance { int label = 0; float leavesAlpha = 1.f, branchAlpha = 1.f; };
struct TreeDrawItem { TreeInstance * instance; wot::StockTreeLodValues lod; };
struct Setting { int option = 0; int activeOption() const { return option; } };
struct TreeRenderingOwner
{
    bool active = true, gpuReady = true;
    float time = 0.f;
    std::array<float, 16> collisionBodyMatrix = {};
    Matrix alphaPassView = Matrix::identity, alphaPassProjection = Matrix::identity;
    std::vector<TreeDrawItem> semitransparent;
    Setting * semitransparentLeaves = nullptr;
    Moo::EffectMaterial materials[2][4];
    Moo::EffectMaterial * semitransparentMaterials[2][4];
    TreeRenderingOwner()
    {
        for (int s = 0; s < 2; ++s) for (int p = 0; p < 4; ++p)
        {
            materials[s][p] = { s, p };
            semitransparentMaterials[s][p] = &materials[s][p];
        }
    }
};
std::shared_ptr<TreeRenderingOwner> renderingOwner;
std::shared_ptr<TreeRenderingOwner> currentRenderingOwner() { return renderingOwner; }
float addTreeScalar( float a, float b ) { return a + b; }

void treeBillboardRotation( float (&rotation)[4] ) { rotation[0] = 1.f; event( "billboards" ); }
bool drawSemitransparentPart( TreeRenderingOwner &, Moo::EffectMaterial & material, unsigned part,
    const TreeDrawItem & item, const float (&)[4] )
{
    event( "draw " + std::to_string( item.instance->label ) + " " + std::to_string( material.state ) + " " +
        std::to_string( part ) + (unsigned( material.part ) == part ? "" : " wrong material") );
    return true;
}

// The float32 D3DX definitions of stock_cursor_camera_oracle.CameraMath.
struct TreeHidingD3DX
{
    static void transformNormal( float * out, const float * v, const float * m )
    {
        for (int c = 0; c < 3; ++c) out[c] = (v[0] * m[c] + v[1] * m[4 + c]) + v[2] * m[8 + c];
    }
    static void inverse( float * m )
    {
        // Pure translations: the tests place unrotated, unscaled trees.
        for (int c = 12; c < 15; ++c) m[c] = -m[c];
    }
    static void transformPoint( float * out, const float * v, const float * m )
    {
        for (int c = 0; c < 3; ++c) out[c] = ((v[0] * m[c] + v[1] * m[4 + c]) + v[2] * m[8 + c]) + m[12 + c];
    }
};

namespace CompiledSpace
{
struct SceneObject {};
class StockTreeSceneHandler
{
public:
    void unload();
    struct Implementation { std::vector<TreeInstance> instances; std::vector<SceneObject> objects; bool loaded = true; };
    std::unique_ptr<Implementation> implementation_{ new Implementation };
};
}

@TREE_STATE@
@TREE_FUNCTIONS@
@CAMERA_FUNCTIONS@

// Script init between teardown and reuse (absent before the close barrier).
#define REINIT() @REINIT@

// The patched CursorCamera members that publish (production text).
class CursorCamera
{
public:
    explicit CursorCamera( MatrixProvider * target ) : pTarget_( target ), sptHiding_( false ) {}
    ~CursorCamera();
    void sptHiding( bool value );
    void updateStock()
    {
@UPDATE_PUBLISH@
    }
    MatrixProviderPtr pTarget_;
    bool sptHiding_;
};
@CAMERA_MEMBERS@

static Matrix at( float x, float y, float z ) { Matrix m = Matrix::identity; m.m[12] = x; m.m[13] = y; m.m[14] = z; return m; }

// One colour-pass instance between the camera (origin) and a target 20 m away.
static bool colourPass( TreeInstance & instance )
{
    static const float world[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 0, 0, 1 };
    wot::StockTreeFadeInput input = {};
    input.world = world;
    const float low[3] = { -3.f, 0.f, -3.f }, high[3] = { 3.f, 12.f, 3.f };
    for (int i = 0; i < 3; ++i) { input.boundsMin[i] = low[i]; input.boundsMax[i] = high[i]; }
    input.eye[1] = 2.f;
    input.lodDistance = 15.f;
    input.model3d = true;
    const bool faded = fadeTreeInstance( instance, input );
    if (faded && renderingOwner) renderingOwner->semitransparent.push_back( TreeDrawItem{ &instance, {} } );
    return faded;
}

static void dump( const char * step, const MatrixProvider & a, const MatrixProvider & b, const TreeInstance & tree )
{
    MatrixProvider * current = treeHiding().target.get();
    std::printf( "%s target=%d refs=%d,%d fade=%.6f,%.6f events=", step, current ? current->label : 0,
        a.references, b.references, tree.leavesAlpha, tree.branchAlpha );
    for (const auto & e : events) std::printf( "[%s]", e.c_str() );
    std::printf( "\n" );
    events.clear();
}

int main()
{
    IRendererPipeline pipeline;
    Renderer::instance().pipeline_ = &pipeline;
    Moo::rc().invView_ = at( 0.f, 2.f, 0.f );
    MatrixProvider first, second;
    first.label = 1; first.world = at( 20.f, 0.f, 0.f );
    second.label = 2; second.world = at( -20.f, 0.f, 0.f );
    TreeInstance tree;
    tree.label = 7;

    // Partial startup: a camera publishes before the tree owner exists.
    {
        CursorCamera camera( &first );
        camera.sptHiding( true );
        camera.updateStock();
        tickStockTreeSceneRendering( .016f );
        colourPass( tree );
        dump( "early", first, second, tree );
    }
    dump( "early-destroyed", first, second, tree );
    finiStockCursorCameraHiding();
    finiStockCursorCameraHiding();
    dump( "fini-twice", first, second, tree );
    REINIT();

    renderingOwner = std::make_shared<TreeRenderingOwner>();
    Setting leaves;
    renderingOwner->semitransparentLeaves = &leaves;
    {
        CursorCamera a( &first ), b( &second );
        a.updateStock();
        tickStockTreeSceneRendering( .016f );
        colourPass( tree );
        dump( "disabled", first, second, tree );
        a.sptHiding( true );
        a.updateStock();
        tickStockTreeSceneRendering( .016f );
        colourPass( tree );
        dump( "enabled", first, second, tree );
        Moo::rc().view_ = at( 5.f, 0.f, 0.f );
        renderingOwner->alphaPassView = at( 9.f, 0.f, 0.f );
        renderingOwner->alphaPassProjection.m[0] = 3.f;
        drawStockTreeSemitransparent( false );
        dump( "drawn", first, second, tree );
        leaves.option = 1;
        colourPass( tree );
        drawStockTreeSemitransparent( false );
        dump( "drawn-leaves", first, second, tree );
        colourPass( tree );
        drawStockTreeSemitransparent( true );
        dump( "skipped", first, second, tree );
        // A second camera replaces the target: its target is behind the camera.
        b.sptHiding( true );
        b.updateStock();
        tickStockTreeSceneRendering( .016f );
        colourPass( tree );
        dump( "replaced", first, second, tree );
        drawStockTreeSemitransparent( false );
        a.updateStock();
        tickStockTreeSceneRendering( .016f );
        dump( "target-changed", first, second, tree );
        a.sptHiding( false );
        tickStockTreeSceneRendering( .016f );
        colourPass( tree );
        dump( "setter-false", first, second, tree );
        b.updateStock();
        dump( "republished", first, second, tree );
    }
    dump( "both-destroyed", first, second, tree );

    // Reentrant matrix(): Python clears the target while the snapshot runs.
    {
        CursorCamera camera( &first );
        camera.sptHiding( true );
        camera.updateStock();
        first.onMatrix = [] { publishStockCursorCameraHiding( nullptr, true ); };
        tickStockTreeSceneRendering( .016f );
        first.onMatrix = nullptr;
        dump( "reentrant-matrix", first, second, tree );
    }

    // Reentrant destruction: the renderer holds the last reference, and the
    // provider's destructor publishes another target from inside the release.
    {
        auto * dynamic = new MatrixProvider;
        dynamic->label = 3;
        dynamic->dynamic = true;
        dynamic->onDestroy = [&second] { publishStockCursorCameraHiding( &second, true ); };
        publishStockCursorCameraHiding( dynamic, true );
        publishStockCursorCameraHiding( nullptr, true );
        dump( "reentrant-destroy", first, second, tree );
        delete dynamic;
        finiStockCursorCameraHiding();
        dump( "reentrant-fini", first, second, tree );
        REINIT();
    }

    // Root repro 1: A.matrix() replaces the target with B during the tick.
    {
        first.onMatrix = [&second] { publishStockCursorCameraHiding( &second, true ); };
        publishStockCursorCameraHiding( &first, true );
        tickStockTreeSceneRendering( .016f );
        first.onMatrix = nullptr;
        colourPass( tree );
        dump( "replaced-in-matrix", first, second, tree );
        tickStockTreeSceneRendering( .016f );
        colourPass( tree );
        dump( "replaced-next-tick", first, second, tree );
        publishStockCursorCameraHiding( nullptr, true );
        dump( "replaced-cleared", first, second, tree );
    }

    // Root repro 2: fini releases the last A reference; A's destructor
    // republishes B. Nothing may stay retained across Python teardown.
    {
        auto * dynamic = new MatrixProvider;
        dynamic->label = 4;
        dynamic->dynamic = true;
        dynamic->onDestroy = [&second] { publishStockCursorCameraHiding( &second, true ); };
        publishStockCursorCameraHiding( dynamic, true );
        finiStockCursorCameraHiding();
        dump( "fini-republish", first, second, tree );
        delete dynamic;
        finiStockCursorCameraHiding();
        dump( "fini-cleanup", first, second, tree );
    }
@REOPEN@
    // Space teardown drops only that space's queued trees; device loss drains.
    {
        renderingOwner->semitransparent.clear();
        CompiledSpace::StockTreeSceneHandler space;
        space.implementation_->instances.resize( 2 );
        space.implementation_->instances[0].label = 11;
        TreeInstance other;
        other.label = 12;
        renderingOwner->semitransparent.push_back( TreeDrawItem{ &space.implementation_->instances[0], {} } );
        renderingOwner->semitransparent.push_back( TreeDrawItem{ &other, {} } );
        space.unload();
        space.unload();
        std::printf( "unload queued=%d first=%d\n", int( renderingOwner->semitransparent.size() ),
            renderingOwner->semitransparent.empty() ? 0 : renderingOwner->semitransparent[0].instance->label );
        renderingOwner->gpuReady = false;
        drawStockTreeSemitransparent( false );
        std::printf( "device-lost queued=%d events=%d\n", int( renderingOwner->semitransparent.size() ), int( events.size() ) );
        renderingOwner.reset();
        drawStockTreeSemitransparent( false );
        tickStockTreeSceneRendering( .016f );
        std::printf( "no-owner events=%d\n", int( events.size() ) );
    }
    return 0;
}
'''


REOPEN = r"""
    // Repeated fini stays closed: stale callbacks cannot reopen the owner.
    publishStockCursorCameraHiding( &first, true );
    dump( "closed-publish", first, second, tree );
    // Script init reopens legitimately; a later fini closes again.
    initStockCursorCameraHiding();
    publishStockCursorCameraHiding( &first, true );
    tickStockTreeSceneRendering( .016f );
    colourPass( tree );
    dump( "reopened", first, second, tree );
    first.onMatrix = [] { finiStockCursorCameraHiding(); };
    tickStockTreeSceneRendering( .016f );
    first.onMatrix = nullptr;
    colourPass( tree );
    dump( "fini-in-matrix", first, second, tree );
    initStockCursorCameraHiding();
    first.onMatrix = [&first, &second] {
        publishStockCursorCameraHiding( &second, true );
        publishStockCursorCameraHiding( nullptr, true );
        publishStockCursorCameraHiding( &first, true );
    };
    publishStockCursorCameraHiding( &first, true );
    tickStockTreeSceneRendering( .016f );
    first.onMatrix = nullptr;
    colourPass( tree );
    dump( "nested-aba", first, second, tree );
    tickStockTreeSceneRendering( .016f );
    colourPass( tree );
    dump( "nested-aba-next", first, second, tree );
    finiStockCursorCameraHiding();
    initStockCursorCameraHiding();
"""


def lifecycle_source(engine_root):
    scene = (ROOT / 'bigworld/stock_tree_scene.cpp').read_text()
    state = scene[scene.index('struct TreeCollisionBodyState'):]
    state = state[:state.index('\n}\n', state.index('TreeHidingState & treeHiding()')) + 3]
    tree = '\n'.join(function(scene, signature) for signature in (
        'bool fadeTreeInstance( TreeInstance & instance, const wot::StockTreeFadeInput & input )',
        'void tickStockTreeSceneRendering( float dTime )',
        'void setStockTreeCameraTarget( MatrixProvider * target )',
        'void setStockTreeCollisionBody( MatrixProvider * target )',
        'void closeStockTreeCollisionBody()',
        'void closeStockTreeCameraTarget()', 'void openStockTreeCameraTarget()',
        'void drawStockTreeSemitransparent( bool skipDrawing )',
        'void StockTreeSceneHandler::unload()'))
    tree = tree.replace('void StockTreeSceneHandler::unload()',
                        'void CompiledSpace::StockTreeSceneHandler::unload()')
    camera = (ROOT / 'bigworld/stock_cursor_camera.cpp').read_text()
    published = '\n'.join(function(camera, signature) for signature in (
        'void publishStockCursorCameraHiding( MatrixProvider * target, bool enabled )',
        'void initStockCursorCameraHiding()', 'void finiStockCursorCameraHiding()'))
    patched = (engine_root / 'lib/camera/cursor_camera.cpp').read_text()
    members = '\n'.join(function(patched, signature) for signature in (
        'CursorCamera::~CursorCamera()', 'void CursorCamera::sptHiding( bool value )'))
    update = function(patched, 'void CursorCamera::updateStock( float deltaTime, bool stablePass )')
    statement = '\tpublishStockCursorCameraHiding( pTarget_.get(), sptHiding_ );'
    assert update.count(statement) == 1, 'The patched update publishes once per pass'
    header = (ROOT / 'bigworld/stock_tree_hiding.hpp').read_text()
    return (LIFECYCLE_HOST.replace('#include "stock_tree_hiding.hpp"', header)
            .replace('@TREE_STATE@', state).replace('@TREE_FUNCTIONS@', tree)
            .replace('@CAMERA_FUNCTIONS@', published)
            .replace('@CAMERA_MEMBERS@', members).replace('@UPDATE_PUBLISH@', statement)
            .replace('@REOPEN@', REOPEN if 'void initStockCursorCameraHiding()' in camera else '')
            .replace('@REINIT@', 'initStockCursorCameraHiding()' if 'void initStockCursorCameraHiding()' in camera
                     else '(void)0'))


class ProductionHidingLifecycleTests(unittest.TestCase):
    def test_publishers_and_tree_consumers_through_the_camera_lifecycle(self):
        requirements(self, oracle=False)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            fresh_engine(root)
            load_patch().patch(root)
            source = lifecycle_source(root)
            binary = compile_host(root, source, 'hiding_lifecycle')
            result = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = {line.split(' ', 1)[0]: line.split(' ', 1)[1] for line in result.stdout.splitlines()}
        hidden, clear = 'fade=0.400000,0.700000', 'fade=1.000000,1.000000'
        # Partial startup: the static target and snapshot do not need the owner.
        self.assertEqual(rows['early'], 'target=1 refs=2,0 %s events=[matrix 1]' % hidden)
        # 0079b679: the destructor clears the target; fini is idempotent.
        self.assertEqual(rows['early-destroyed'], 'target=0 refs=0,0 %s events=' % hidden)
        self.assertEqual(rows['fini-twice'], 'target=0 refs=0,0 %s events=' % hidden)
        # Disabled hiding publishes NULL; the colour pass leaves the tree opaque.
        self.assertEqual(rows['disabled'], 'target=0 refs=1,1 %s events=' % clear)
        self.assertEqual(rows['enabled'], 'target=1 refs=2,1 %s events=[matrix 1]' % hidden)
        # afef60: billboards from the live camera, then the colour-pass camera.
        # afef60 sets no pipeline stencil; vslot 0x14(4) is the per-view constants.
        drawn = ('[billboards][view 9][projection 3][transforms][constants 4]'
                 '[draw 7 0 1][draw 7 0 0][draw 7 0 3][draw 7 1 1][draw 7 1 0][draw 7 1 3]')
        self.assertEqual(rows['drawn'], 'target=1 refs=2,1 %s events=%s' % (hidden, drawn))
        self.assertEqual(rows['drawn-leaves'], 'target=1 refs=2,1 %s events=%s' % (hidden, drawn.replace(
            '[draw 7 0 1]', '[draw 7 0 2][draw 7 0 1]').replace('[draw 7 1 1]', '[draw 7 1 2][draw 7 1 1]')))
        self.assertEqual(rows['skipped'], 'target=1 refs=2,1 %s events=' % hidden)
        # A second camera's target behind the eye leaves the tree opaque.
        self.assertEqual(rows['replaced'], 'target=2 refs=1,2 %s events=[matrix 2]' % clear)
        self.assertEqual(rows['target-changed'], 'target=1 refs=2,1 %s events=[matrix 1]' % clear)
        self.assertEqual(rows['setter-false'], 'target=0 refs=1,1 %s events=' % clear)
        self.assertEqual(rows['republished'], 'target=2 refs=1,2 %s events=' % clear)
        # Destroying either camera clears whatever target was published.
        self.assertEqual(rows['both-destroyed'], 'target=0 refs=0,0 %s events=' % clear)
        # The snapshot keeps the provider alive across a reentrant clear.
        self.assertEqual(rows['reentrant-matrix'], 'target=0 refs=1,0 %s events=[matrix 1]' % clear)
        self.assertEqual(rows['reentrant-destroy'], 'target=2 refs=0,1 %s events=[destroy 3]' % clear)
        self.assertEqual(rows['reentrant-fini'], 'target=0 refs=0,0 %s events=' % clear)
        # A.matrix() replacing the target must not publish A's frame for B.
        self.assertEqual(rows['replaced-in-matrix'], 'target=2 refs=0,1 %s events=[matrix 1]' % clear)
        self.assertEqual(rows['replaced-next-tick'], 'target=2 refs=0,1 %s events=[matrix 2]' % clear)
        self.assertEqual(rows['replaced-cleared'], 'target=0 refs=0,0 %s events=' % clear)
        # fini closes before releasing; A's destructor cannot install B.
        self.assertEqual(rows['fini-republish'], 'target=0 refs=0,0 %s events=[destroy 4]' % clear)
        self.assertEqual(rows['fini-cleanup'], 'target=0 refs=0,0 %s events=' % clear)
        self.assertEqual(rows['closed-publish'], 'target=0 refs=0,0 %s events=' % clear)
        # Script init reopens; fini during matrix() drops that tick's frame.
        self.assertEqual(rows['reopened'], 'target=1 refs=1,0 %s events=[matrix 1]' % hidden)
        self.assertEqual(rows['fini-in-matrix'], 'target=0 refs=0,0 %s events=[matrix 1]' % clear)
        # A->B->NULL->A inside matrix(): the same pointer, a new generation.
        self.assertEqual(rows['nested-aba'], 'target=1 refs=1,0 %s events=[matrix 1]' % clear)
        self.assertEqual(rows['nested-aba-next'], 'target=1 refs=1,0 %s events=[matrix 1]' % hidden)
        self.assertEqual(rows['unload'], 'queued=1 first=12')
        self.assertEqual(rows['device-lost'], 'queued=0 events=0')
        self.assertEqual(rows['no-owner'], 'events=0')
        self.assertNotIn('double destroy', result.stdout)


EFFECT_PROBE = r'''
#include "wot/stock_shader_binary.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
int main( int argc, char ** argv )
{
    for (int i = 1; i < argc; ++i)
    {
        std::ifstream stream( argv[i], std::ios::binary );
        std::vector<unsigned char> bytes( (std::istreambuf_iterator<char>( stream )), {} );
        wot::StockEffectBinary effect;
        std::string error;
        if (!wot::decode_stock_dx9_effect( bytes.data(), bytes.size(), effect, error )) return 1;
        for (const auto & technique : effect.techniques)
            std::printf( "T %s %zu\n", technique.name.c_str(), technique.passes.size() );
        for (const auto & buffer : effect.uniform_buffers)
            for (const auto & uniform : buffer.uniforms)
            {
                std::printf( "U %s %u\n", uniform.name.c_str(),
                    effect.types.at( uniform.type_index ).byte_size * (uniform.elements ? uniform.elements : 1) );
                if (uniform.name == "g_instance")
                {
                    const auto first = buffer.defaults.begin() + uniform.byte_offset;
                    for (auto byte = first; byte != first + 64; ++byte) if (*byte) return 2;
                }
            }
        std::printf( "-\n" );
    }
}
'''


class SemitransparentEffectTests(unittest.TestCase):
    def test_original_semitransparent_effect_has_every_requested_state(self):
        import io
        import zipfile
        if not (CLIENT / 'res/packages/shaders.pkg').exists():
            self.skipTest('Exact #1513 client required')
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            (folder / 'probe.cpp').write_text(EFFECT_PROBE)
            subprocess.run(['c++', '-std=c++17', '-O1', '-I' + str(ROOT / 'include'), str(folder / 'probe.cpp'),
                            str(ROOT / 'src/stock_shader_binary.cpp'), '-o', str(folder / 'probe')], check=True)
            effects = []
            with zipfile.ZipFile(CLIENT / 'res/packages/shaders.pkg') as archive:
                for variant in (10, 11):
                    name = 'shaders/speedtree/semitransparent.%d.dx9.fxo' % variant
                    with zipfile.ZipFile(io.BytesIO(archive.read(name))) as outer:
                        effects.append(folder / ('semitransparent-%d.effect' % variant))
                        effects[-1].write_bytes(outer.read('effect'))
            output = subprocess.run([str(folder / 'probe')] + [str(e) for e in effects], capture_output=True,
                                    text=True, check=True).stdout
        source = (ROOT / 'bigworld/stock_tree_scene.cpp').read_text()
        for variant in output.split('-\n')[:2]:
            techniques = dict(line.split()[1:] for line in variant.splitlines() if line.startswith('T '))
            uniforms = dict(line.split()[1:] for line in variant.splitlines() if line.startswith('U '))
            for state in ('DEPTH', 'ALPHA'):
                for part in ('BRANCH', 'FROND', 'LEAF', 'BILLBOARD'):
                    self.assertEqual(techniques[part + '_' + state], '1')
                    self.assertIn('"%s_%s"' % (part, state), source)
            self.assertEqual((uniforms['g_instance'], uniforms['g_material'], uniforms['g_billboardCameraRotation']),
                             ('64', '32', '16'))
            # a54fa0 sets conversions the effect does not declare; they must be optional.
            self.assertNotIn('g_albedoConversions', uniforms)


API_HOST = r"""
#include "stock_tree_hiding.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define BW_STATIC_ASSERT( condition, message ) static_assert( condition, #message )
typedef long Py_ssize_t;
typedef unsigned uint32;

// Python objects: tag 1 bool, 2 float, 3 other; raw is the bool or binary32.
struct PyObject { unsigned tag, raw; long references; std::vector<PyObject *> items; };
PyObject pyNone = { 0, 0, 1, {} };
#define Py_RETURN_NONE return (++pyNone.references, &pyNone)
Py_ssize_t PyTuple_Size( PyObject * tuple ) { return Py_ssize_t( tuple->items.size() ); }
PyObject * PyTuple_GetItem( PyObject * tuple, Py_ssize_t index ) { return tuple->items[index]; }
std::vector<std::string> events;
namespace Script
{
int setData( PyObject * object, bool & value, const char * name )
{
    if (object->tag != 1) { events.push_back( std::string( "error|" ) + name ); return -1; }
    value = (object->raw & 1) != 0;
    return 0;
}
int setData( PyObject * object, float & value, const char * name )
{
    if (object->tag != 2) { events.push_back( std::string( "error|" ) + name ); return -1; }
    std::memcpy( &value, &object->raw, 4 );
    return 0;
}
PyObject * argCountError( const char * name, int low, int high, ... )
{
    std::string text = std::string( "count|" ) + name + "|" + std::to_string( low ) + "|" + std::to_string( high );
    va_list types;
    va_start( types, high );
    for (int i = 0; i < high; ++i) text += std::string( "|" ) + va_arg( types, const char * );
    va_end( types );
    events.push_back( text );
    return nullptr;
}
}
std::vector<std::string> registered;
struct PyModuleMethodLink
{
    PyModuleMethodLink( const char * module, const char * name, PyObject * (*)( PyObject *, PyObject * ), const char * )
    { registered.push_back( std::string( module ) + "." + name ); }
};
@MACROS@

struct MatrixProvider { int references = 1; };
class MatrixProviderPtr
{
public:
    MatrixProviderPtr( MatrixProvider * p = nullptr ) : p_( p ) {}
    MatrixProvider * get() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
private:
    MatrixProvider * p_;
};
struct TreeInstance { float leavesAlpha = 1.f, branchAlpha = 1.f; };
@D3DX@
@STATE@
@FUNCTIONS@
@FADE@

int main()
{
    for (const auto & name : registered) std::printf( "R %s\n", name.c_str() );
    MatrixProvider provider;
    char line[512];
    while (std::fgets( line, sizeof( line ), stdin ))
    {
        char kind = line[0];
        if (kind == 'C')
        {
            // C function count tag raw ...
            int function, count, offset = 2, used = 0;
            std::sscanf( line + offset, "%d %d%n", &function, &count, &used );
            offset += used;
            PyObject args = { 0, 0, 1, {} };
            std::vector<PyObject> items( count );
            for (int i = 0; i < count; ++i)
            {
                items[i].references = 1;
                std::sscanf( line + offset, "%u %x%n", &items[i].tag, &items[i].raw, &used );
                offset += used;
                args.items.push_back( &items[i] );
            }
            PyObject * (*functions[])( PyObject *, PyObject * ) =
                { _py_wg_enableTreeHiding, _py_wg_setTreeHidingRadius, _py_wg_enableTreeTransparency };
            events.clear();
            const long before = pyNone.references;
            PyObject * result = functions[function]( nullptr, &args );
            const auto & s = treeHiding().settings;
            unsigned words[3];
            std::memcpy( words, &s.squaredHideRadius, 4 );
            std::memcpy( words + 1, &s.hideRadius, 4 );
            std::memcpy( words + 2, &s.hideRadiusAlpha, 4 );
            std::printf( "C %s %ld %d %08x %08x %08x %d", result == &pyNone ? "none" : "null",
                pyNone.references - before, int( s.hideClosest ), words[0], words[1], words[2], int( s.transparency ) );
            for (auto e : events)
            {
                for (auto & c : e) if (c == ' ') c = '_';
                std::printf( " %s", e.c_str() );
            }
            std::printf( "\n" );
        }
        else
        {
            // F target? frame7 world16 min3 max3 eye3 lod flags (hex binary32)
            unsigned u[34];
            int offset = 2, used = 0;
            for (int i = 0; i < 34; ++i) { std::sscanf( line + offset, "%x%n", &u[i], &used ); offset += used; }
            auto f = []( unsigned w ) { float v; std::memcpy( &v, &w, 4 ); return v; };
            TreeHidingState & state = treeHiding();
            state.target = MatrixProviderPtr( u[0] ? &provider : nullptr );
            for (int i = 0; i < 3; ++i) { state.frame.camera[i] = f( u[1 + i] ); state.frame.target[i] = f( u[4 + i] ); }
            state.frame.distance = f( u[7] );
            state.frameGeneration = state.generation;
            float world[16];
            for (int i = 0; i < 16; ++i) world[i] = f( u[8 + i] );
            wot::StockTreeFadeInput input = {};
            input.world = world;
            for (int i = 0; i < 3; ++i)
            {
                input.boundsMin[i] = f( u[24 + i] );
                input.boundsMax[i] = f( u[27 + i] );
                input.eye[i] = f( u[30 + i] );
            }
            input.lodDistance = f( u[33] );
            input.model3d = true;
            TreeInstance instance;
            const bool faded = fadeTreeInstance( instance, input );
            unsigned leaves, branches;
            std::memcpy( &leaves, &instance.leavesAlpha, 4 );
            std::memcpy( &branches, &instance.branchAlpha, 4 );
            std::printf( "F %08x %08x %d\n", leaves, branches, int( faded ) );
        }
    }
    return 0;
}
"""


def script_macros():
    import tarfile
    from test_stock_cursor_camera import ARCHIVE
    with tarfile.open(ARCHIVE) as archive:
        name = next(n for n in archive.getnames() if n.endswith('/programming/bigworld/lib/pyscript/script.hpp'))
        text = archive.extractfile(name).read().decode('utf-8', 'replace')
    start = text.index('// Section: Macros: Recursion utilities')
    end = text.index('// Section: PyModuleLink')
    module = text[text.index('#define PY_AUTO_MODULE_FUNCTION( RET, FUNC_NAME, ARGS, MODULE_NAME )'):]
    module = module[:module.index('/// This macro defines an alias')]
    return text[start:end] + '\n' + module


def api_host_source():
    scene = (ROOT / 'bigworld/stock_tree_scene.cpp').read_text()
    state = scene[scene.index('struct TreeCollisionBodyState'):]
    state = state[:state.index('\n}\n', state.index('TreeHidingState & treeHiding()')) + 3]
    functions = scene[scene.index('void wg_enableTreeHiding( bool enable )'):]
    functions = functions[:functions.index('\n', functions.index('PY_AUTO_MODULE_FUNCTION( RETVOID, wg_enableTreeTransparency')) + 1]
    fade = function(scene, 'bool fadeTreeInstance( TreeInstance & instance, const wot::StockTreeFadeInput & input )')
    d3dx = PROBE[PROBE.index('struct HostD3DX'):PROBE.index('int main()')].replace('struct HostD3DX', 'struct TreeHidingD3DX')
    return (API_HOST.replace('#include "stock_tree_hiding.hpp"', (ROOT / 'bigworld/stock_tree_hiding.hpp').read_text())
            .replace('@MACROS@', script_macros()).replace('@D3DX@', d3dx).replace('@STATE@', state)
            .replace('@FUNCTIONS@', functions).replace('@FADE@', fade))


API_ORACLE = r"""
import json, sys
sys.path.insert(0, sys.argv[1])
import stock_tree_hiding_oracle as o
oracle = o.TreeHidingOracle(sys.argv[2])
plan = json.loads(sys.stdin.read())
functions = (o.ENABLE_HIDING, o.SET_RADIUS, o.ENABLE_TRANSPARENCY)
out = []
for function, values, fades in plan:
    step = oracle.call_script(functions[function], [tuple(v) for v in values])
    step.append([oracle.compute_fade(dict(case, settings=None)) for case in fades])
    out.append(step)
print(json.dumps(out))
"""


def api_plan():
    # The retail sequence: Avatar init, sniper enable, visibility toggles,
    # sniper disable; then legal values and every invalid call shape.
    between = dict(lod=15.0, min=[-3.0, 0.0, -3.0], max=[3.0, 12.0, 3.0], eye=[0.0, 2.0, 0.0],
                   world=rotation_y(0.0, 1.0, (10.0, 0.0, 0.0)), bush=False, model3d=True,
                   frame=dict(camera=[0.0, 2.0, 0.0], target=[20.0, 1.0, 0.0], distance=20.0))
    near = dict(between, eye=[-3.0, 2.0, 0.0],
                frame=dict(camera=[-3.0, 2.0, 0.0], target=[-20.0, 1.0, 0.0], distance=17.0))
    lone = dict(near, frame=None)
    fades = [between, near, lone]
    true, false = ['bool', True], ['bool', False]
    calls = [(0, [false]), (0, [true]), (1, [['float', 15.0], ['float', 10.0]]), (2, [false]), (2, [true]),
             (0, [false]), (1, [['float', 7.5], ['float', 9.25]]), (0, [true]), (1, [['float', 0.0], ['float', 0.0]]),
             (1, [['float', 15.0], ['float', 10.0]]), (0, []), (0, [true, true]), (1, [['float', 15.0]]),
             (1, [['float', 15.0], ['float', 10.0], ['float', 1.0]]), (1, [['bad', 0], ['float', 3.0]]),
             (1, [['float', 3.0], ['bad', 0]]), (2, []), (2, [['bad', 0]]), (0, [['float', 1.0]]), (2, [true])]
    return [(function, values, fades) for function, values in calls]


def api_host_input(plan):
    rows = []
    for function, values, fades in plan:
        words = []
        for kind, value in values:
            tag = {'bool': 1, 'float': 2}.get(kind, 3)
            words.append('%d %x' % (tag, bits(value) if kind == 'float' else int(bool(value))))
        rows.append('C %d %d %s' % (function, len(values), ' '.join(words)))
        for case in fades:
            frame = case['frame'] or dict(camera=[0.0] * 3, target=[0.0] * 3, distance=0.0)
            values32 = frame['camera'] + frame['target'] + [frame['distance']] + case['world'] + case['min'] + \
                case['max'] + case['eye'] + [case['lod']]
            rows.append('F %x %s' % (int(case['frame'] is not None), ' '.join('%x' % bits(v) for v in values32)))
    return '\n'.join(rows) + '\n'


class TreeHidingApiTests(unittest.TestCase):
    def test_script_calls_reach_the_tree_consumer_like_the_original(self):
        requirements(self)
        plan = api_plan()
        run = subprocess.run([str(ORACLE_PYTHON), '-c', API_ORACLE, str(TESTS), str(CLIENT / 'WorldOfTanks.exe')],
                             input=json.dumps(plan), capture_output=True, text=True,
                             env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        original = json.loads(run.stdout)
        with tempfile.TemporaryDirectory() as folder:
            binary = compile_host(Path(folder), api_host_source(), 'tree_hiding_api')
            result = subprocess.run([str(binary)], input=api_host_input(plan), capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual([line for line in lines if line.startswith('R ')],
                         ['R BigWorld.wg_enableTreeHiding', 'R BigWorld.wg_setTreeHidingRadius',
                          'R BigWorld.wg_enableTreeTransparency'])
        calls = [line.split() for line in lines if line.startswith('C ')]
        fades = [line.split()[1:3] for line in lines if line.startswith('F ')]
        srt = []
        for index, (step, call) in enumerate(zip(original, calls)):
            returned, none, events, state, fade = step
            events = [e for e in events if not e[0].startswith('srt_')]
            srt.extend(e for e in step[2] if e[0].startswith('srt_'))
            expected = ['C', 'none' if returned == 'none' else 'null', str(none), str(state[0]),
                        '%08x' % bits(state[1]), '%08x' % bits(state[2]), '%08x' % bits(state[3]), str(state[4])]
            expected += ['|'.join(str(v) for v in e).replace(' ', '_') for e in events]
            self.assertEqual(call, expected, 'step %d %r' % (index, plan[index][:2]))
            self.assertEqual(fades[3 * index:3 * index + 3], [['%08x' % bits(a), '%08x' % bits(b)] for a, b in fade],
                             'fade after step %d' % index)
        # The SRT SpeedTreeRender writes (00748190/007481d0) have no owner here.
        self.assertEqual(srt, [['srt_hiding', 0], ['srt_hiding', 1], ['srt_radius', 15.0, 10.0], ['srt_hiding', 0],
                               ['srt_radius', 7.5, 9.25], ['srt_hiding', 1], ['srt_radius', 0.0, 0.0],
                               ['srt_radius', 15.0, 10.0]])
        # Each retail state reaches the consumer: the target fade at init, the
        # sniper near fade (15/10 m) with or without a target, none after disable.
        flat = [[unbits(int(a, 16)), unbits(int(b, 16))] for a, b in fades]
        hidden, opaque = [f32(0.4), f32(0.7)], [1.0, 1.0]
        self.assertEqual(flat[0:3], [hidden, opaque, opaque])
        for index in (7, 8, 11):
            self.assertTrue(f32(0.4) < flat[index][0] < 1.0 and flat[index][1] == f32(0.7), index)
        self.assertEqual(flat[15:18], [hidden, opaque, opaque])


STAGE_HOST = r"""
#include "wot/stock_dynamic_resolution.hpp"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#define NULL nullptr
typedef long HRESULT;
#define FAILED( h ) ((h) < 0)
#define SUCCEEDED( h ) ((h) >= 0)
enum { D3DCLEAR_TARGET = 1, D3DCLEAR_ZBUFFER = 2, D3DCLEAR_STENCIL = 4 };
typedef unsigned long DWORD;
typedef unsigned uint;
std::vector<std::string> events;
static void event( const std::string & text ) { events.push_back( text ); }
static std::string number( float v ) { char b[32]; std::snprintf( b, sizeof( b ), "%g", v ); return b; }
struct Vector4 { float x, y, z, w; Vector4( float a, float b, float c, float d ) : x( a ), y( b ), z( c ), w( d ) {} };
namespace DX
{
struct Surface { int label; };
struct Viewport { DWORD X, Y, Width, Height; float MinZ, MaxZ; };
struct Texture { int label; };
}
template <class T> struct ComObjectWrap
{
    T * p = nullptr;
    T * pComObject() const { return p; }
    operator T * () const { return p; }
};
int failBind = 0, failClear = 0, failBlur = 0, failTechnique = 0;
struct Device
{
    HRESULT SetDepthStencilSurface( DX::Surface * depth )
    { event( "depth " + std::to_string( depth ? depth->label : 0 ) ); return failBind ? -1 : 0; }
    HRESULT Clear( DWORD, const void *, DWORD flags, DWORD colour, float z, DWORD stencil )
    {
        event( "clear " + std::to_string( flags ) + " " + std::to_string( colour ) + " " + number( z ) + " " +
            std::to_string( stencil ) );
        return failClear ? -1 : 0;
    }
};
struct ID3DXEffect
{
    HRESULT SetBool( const char * name, int v ) { event( std::string( "bool " ) + name + " " + std::to_string( v ) ); return 0; }
    HRESULT SetInt( const char * name, int v ) { event( std::string( "int " ) + name + " " + std::to_string( v ) ); return 0; }
    HRESULT SetFloat( const char * name, float v ) { event( std::string( "float " ) + name + " " + number( v ) ); return 0; }
    HRESULT SetVector( const char * name, const Vector4 * v )
    {
        event( std::string( "vector " ) + name + " " + number( v->x ) + " " + number( v->y ) + " " + number( v->z ) + " " + number( v->w ) );
        return 0;
    }
    HRESULT SetTexture( const char * name, DX::Texture * t )
    { event( std::string( "texture " ) + name + " " + std::to_string( t ? t->label : 0 ) ); return 0; }
};
struct ManagedEffect { ID3DXEffect effect; ID3DXEffect * pEffect() { return &effect; } };
namespace Moo
{
enum { CONSTANTS_PER_SCREEN = 2 };
struct EffectMaterial
{
    std::string name;
    std::unique_ptr<ManagedEffect> managed{ new ManagedEffect };
    ManagedEffect * pEffect() { return managed.get(); }
    // Binding isolation is exercised by test_stock_tree_material_binding;
    // this oracle records the original stage's target/constant call order.
    struct Properties { bool select( ManagedEffect & ) const { return true; } } properties_;
    Properties & properties() { return properties_; }
    bool hTechnique( const char * technique )
    {
        event( "technique " + name + " " + technique );
        const char * failing[] = { "", "HORIZONTAL_PASS", "VERTICAL_PASS", "UPSAMPLE_RT" };
        return std::strcmp( technique, failing[failTechnique] ) != 0;
    }
};
typedef std::shared_ptr<EffectMaterial> EffectMaterialPtr;
struct RenderTarget
{
    int label; uint w, h;
    DX::Surface surface, depth;
    DX::Texture texture;
    HRESULT pSurface( ComObjectWrap<DX::Surface> & out ) { out.p = &surface; return 0; }
    DX::Surface * depthBuffer() { return &depth; }
    uint width() const { return w; }
    uint height() const { return h; }
    DX::Texture * pTexture() { return &texture; }
};
typedef std::shared_ptr<RenderTarget> RenderTargetPtr;
// The provider caches PerScreen data at each update; a target pop does not.
struct EffectVisualContext { float perScreen[2] = { 1600, 900 }; void updateSharedConstants( int kind ); };
struct FullscreenQuad
{
    bool drawChecked( EffectMaterial & material, uint w, uint h )
    { event( "quad " + material.name + " " + std::to_string( w ) + " " + std::to_string( h ) ); return !failBlur || material.name != "blur"; }
};
struct RenderContext
{
    Device device_;
    EffectVisualContext context_;
    FullscreenQuad quad_;
    float width_ = 1600, height_ = 900;
    Device * device() { return &device_; }
    std::vector<std::pair<float, float>> saved;
    // RenderTargetStack::pop restores the saved viewport and screen size.
    bool pushRenderTarget() { event( "push" ); saved.emplace_back( width_, height_ ); return true; }
    void popRenderTarget() { event( "pop" ); width_ = saved.back().first; height_ = saved.back().second; saved.pop_back(); }
    HRESULT setRenderTarget( int index, DX::Surface * s )
    { if (s) event( "target " + std::to_string( index ) + " " + std::to_string( s->label ) ); return 0; }
    float screenWidth() const { return width_; }
    float screenHeight() const { return height_; }
    void screenWidth( float v ) { width_ = v; }
    void screenHeight( float v ) { height_ = v; }
    EffectVisualContext & effectVisualContext() { return context_; }
    FullscreenQuad & fsQuad() { return quad_; }
};
inline RenderContext & rc() { static RenderContext value; return value; }
// Per-screen constants read the current screen size (EffectVisualContext).
inline void EffectVisualContext::updateSharedConstants( int kind )
{
    event( "constants " + std::to_string( kind ) + " " + number( rc().screenWidth() ) + "x" + number( rc().screenHeight() ) );
    if (kind & CONSTANTS_PER_SCREEN)
    {
        perScreen[0] = rc().screenWidth();
        perScreen[1] = rc().screenHeight();
    }
}
}
HRESULT setStockSceneViewport( DX::Viewport * v, bool scene )
{ event( "viewport " + std::to_string( v->Width ) + " " + std::to_string( v->Height ) + " " + std::to_string( int( scene ) ) ); return 0; }
struct StockDynamicResolutionSupport { wot::StockDynamicResolution value; const wot::StockDynamicResolution & state() const { return value; } };
StockDynamicResolutionSupport * stockDynamicResolution() { return nullptr; }

struct Settings { bool hideClosest = false; };
struct TreeHidingState { Settings settings; };
TreeHidingState & treeHiding() { static TreeHidingState state; return state; }
struct TreeRenderingOwner
{
    std::array<Moo::RenderTargetPtr, 2> hidingTargets;
    Moo::EffectMaterialPtr upsampleMaterial, blurMaterial;
    std::array<std::array<Moo::EffectMaterialPtr, 4>, 2> semitransparentMaterials;
    bool fail( const std::string & error ) { event( "fail " + error.substr( 0, 24 ) ); return false; }
};
std::shared_ptr<TreeRenderingOwner> owner;
std::shared_ptr<TreeRenderingOwner> currentRenderingOwner() { return owner; }
void drawStockTreeSemitransparent( bool skip ) { event( "trees " + std::to_string( int( skip ) ) ); }
@PRODUCTION@

int main()
{
    char line[256];
    while (std::fgets( line, sizeof( line ), stdin ))
    {
        int hide, optimiser, draw, blur, hasOwner, resources;
        std::sscanf( line, "%d %d %d %d %d %d %d %d %d %d", &hide, &optimiser, &draw, &blur, &hasOwner, &resources,
            &failBind, &failClear, &failBlur, &failTechnique );
        treeHiding().settings.hideClosest = hide != 0;
        owner.reset();
        if (hasOwner)
        {
            owner = std::make_shared<TreeRenderingOwner>();
            for (int i = 0; i < 2 && resources; ++i)
            {
                owner->hidingTargets[i] = std::make_shared<Moo::RenderTarget>();
                owner->hidingTargets[i]->label = 10 + i;
                owner->hidingTargets[i]->w = 400;
                owner->hidingTargets[i]->h = 225;
                owner->hidingTargets[i]->surface.label = 20 + i;
                owner->hidingTargets[i]->depth.label = 30 + i;
                owner->hidingTargets[i]->texture.label = 40 + i;
            }
            if (resources)
            {
                owner->upsampleMaterial = std::make_shared<Moo::EffectMaterial>();
                owner->upsampleMaterial->name = "upsample";
                owner->blurMaterial = std::make_shared<Moo::EffectMaterial>();
                owner->blurMaterial->name = "blur";
            }
            for (auto & state : owner->semitransparentMaterials)
                for (auto & m : state) { m = std::make_shared<Moo::EffectMaterial>(); m->name = "tree"; }
        }
        events.clear();
        Moo::rc().width_ = 1600;
        Moo::rc().height_ = 900;
        Moo::rc().context_.perScreen[0] = 1600;
        Moo::rc().context_.perScreen[1] = 900;
        drawStockTreeSemitransparentStage( !draw, optimiser != 0, blur != 0 );
        std::printf( "S" );
        for (const auto & e : events) std::printf( "|%s", e.c_str() );
        std::printf( "\n" );
        std::printf( "R screen=%gx%g PerScreen=%gx%g stack=%d\n", Moo::rc().width_, Moo::rc().height_,
            Moo::rc().context_.perScreen[0], Moo::rc().context_.perScreen[1], int( Moo::rc().saved.size() ) );
    }
    return 0;
}
"""


def stage_host_source():
    scene = (ROOT / 'bigworld/stock_tree_scene.cpp').read_text()
    start = scene.index('namespace\n{\nclass HidingTargetScope')
    helpers = scene[start:scene.index('// #1513 009c4ed0. Near-camera')]
    stage = function(scene, 'void drawStockTreeSemitransparentStage( bool skipDrawing, bool optimiser, bool blur )')
    binding = function(scene, 'ID3DXEffect * selectTreeBinding(')
    return STAGE_HOST.replace('@PRODUCTION@', binding + helpers + stage)


STAGE_ORACLE = r"""
import json, sys
sys.path.insert(0, sys.argv[1])
from stock_tree_hiding_oracle import OptimiserStage
oracle = OptimiserStage(sys.argv[2])
print(json.dumps([oracle.stage(**case) for case in json.loads(sys.stdin.read())]))
"""


def normalise_original(trace):
    """The original stage in production terms (see HANDOFF for each delta)."""
    out = []
    for item in trace:
        kind = item[0]
        if kind in ('acquire', 'release', 'srt'):
            continue  # pool lifetime and the SRT owner (separate tests)
        if kind == 'stencil':
            # ctx vslot 0x14 (94b5a0 -> 9b0e80) is updateSharedConstants(mask).
            out.append('constants %d' % item[1])
            continue
        if kind == 'scope':
            out.append('bind')
        elif kind == 'colour':
            out.append('clear colour %d' % item[1])
        elif kind == 'depth':
            out.append('clear depth %g %d %d' % tuple(item[2:5]))
        elif kind == 'bool':
            out.append('alpha %d' % item[2])
        elif kind == 'trees':
            out.append('trees %d' % item[1])
        elif kind == 'end':
            out.append('end')
        elif kind == 'blur':
            out.append('blur')
        elif kind == 'technique':
            out.append('upsample')
        elif kind in ('bind', 'quad'):
            out.append(kind if kind == 'quad' else 'source')
    return out


def normalise_production(row):
    out, alpha, bound, blurring = [], None, False, False
    for item in row.split('|')[1:]:
        if blurring:
            # The blur owner's own target binds and constants (9d62e0).
            blurring = item != 'pop'
            if item == 'technique blur HORIZONTAL_PASS':
                out.append('blur')
            continue
        if item == 'push' and not bound:
            out.append('bind')
            bound = True
        elif item == 'push':
            blurring = True
        elif item.startswith('constants '):
            out.append(' '.join(item.split()[:2]))
        elif item == 'pop' and 'end' not in out:
            out.append('end')
        elif item.startswith('clear '):
            flags, colour, z, stencil = item.split()[1:]
            # D3D9 TARGET|ZBUFFER|STENCIL is the original colour clear plus
            # 998a20's depth|stencil flags 3.
            assert flags == '7', item
            out += ['clear colour %s' % colour, 'clear depth %s %s 3' % (z, stencil)]
        elif item.startswith('bool g_useAlpha'):
            if item.split()[-1] != alpha:
                alpha = item.split()[-1]
                out.append('alpha %s' % alpha)
        elif item.startswith('trees') or item.startswith('fail'):
            out.append(item.split()[0] if item.startswith('fail') else item)
        elif item == 'technique blur HORIZONTAL_PASS':
            out.append('blur')
        elif item == 'technique upsample UPSAMPLE_RT':
            out.append('upsample')
        elif item == 'texture g_srcMap 40' and out and out[-1] == 'upsample':
            out.append('source')
        elif item.startswith('quad upsample'):
            out.append('quad')
    return out


class OptimiserStageTests(unittest.TestCase):
    def test_deferred_stage_matches_the_original_paths(self):
        requirements(self)
        cases = [dict(hide=False), dict(hide=True), dict(hide=True, optimiser=False), dict(hide=True, blur=False),
                 dict(hide=True, draw=False), dict(hide=False, draw=False), dict(hide=True, bound=False)]
        run = subprocess.run([str(ORACLE_PYTHON), '-c', STAGE_ORACLE, str(TESTS), str(CLIENT / 'WorldOfTanks.exe')],
                             input=json.dumps(cases), capture_output=True, text=True,
                             env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        original = json.loads(run.stdout)
        rows = []
        for case in cases + [dict(hide=True, owner=False), dict(hide=True, resources=False),
                             dict(hide=True, clear=False), dict(hide=True, blurred=False),
                             dict(hide=True, technique=1), dict(hide=True, technique=2),
                             dict(hide=True, technique=3)]:
            rows.append('%d %d %d %d %d %d %d %d %d %d' % (
                case['hide'], case.get('optimiser', True), case.get('draw', True), case.get('blur', True),
                case.get('owner', True), case.get('resources', True), not case.get('bound', True),
                not case.get('clear', True), not case.get('blurred', True), case.get('technique', 0)))
        with tempfile.TemporaryDirectory() as folder:
            source = stage_host_source().replace('#include "wot/stock_dynamic_resolution.hpp"',
                                                 (ROOT / 'include/wot/stock_dynamic_resolution.hpp').read_text())
            binary = compile_host(Path(folder), '#include <array>\n' + source, 'tree_hiding_stage')
            result = subprocess.run([str(binary)], input='\n'.join(rows) + '\n', capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        produced = [line for line in result.stdout.splitlines() if line.startswith('S')]
        restored = [line for line in result.stdout.splitlines() if line.startswith('R ')]
        # Every exit, including clear, blur and technique failures after the
        # quarter apply, leaves the scene screen, the cached PerScreen data and
        # an empty target stack.
        self.assertEqual(restored, ['R screen=1600x900 PerScreen=1600x900 stack=0'] * len(rows))
        # Every legal path, including skip and blur off, has the original order.
        for index in range(6):
            self.assertEqual(normalise_production(produced[index]), normalise_original(original[index]),
                             '%r\n%s' % (cases[index], produced[index]))
        quarter = produced[1].split('|')
        # The two vslot 0x14(2) calls apply per-screen constants for the
        # quarter target and then for the restored scene target.
        self.assertEqual([e for e in quarter if e.startswith('constants 2')][0], 'constants 2 400x225')
        self.assertEqual(quarter[quarter.index('technique upsample UPSAMPLE_RT') - 1], 'constants 2 1600x900')
        # The quarter pass binds the first target with its depth over the
        # scene-resolution viewport; the blur binds none and uses its DRR source.
        self.assertEqual(quarter[1:5], ['push', 'depth 30', 'target 0 20', 'viewport 400 225 1'])
        self.assertEqual([e for e in quarter if e.startswith('bool g_useAlpha')], ['bool g_useAlpha 1'] * 8 + ['bool g_useAlpha 0'] * 8)
        blur = quarter[quarter.index('technique blur HORIZONTAL_PASS') - 4:quarter.index('quad upsample 1600 900') + 1]
        self.assertIn('depth 0', blur)
        for expected in ('int g_depthMode 2', 'bool g_useMaskRT 0', 'bool g_useBilateralFilter 0', 'float g_blurMip 0',
                         'vector g_blurScreen 400 225 0.0025 0.00444444', 'texture g_srcMap 40', 'texture g_srcMap 41',
                         'technique blur VERTICAL_PASS', 'texture g_srcMap 40', 'quad upsample 1600 900'):
            self.assertIn(expected, blur)
        # What normalise_original drops: pool slots 3/4 are acquired before and
        # released in reverse after the quarter pass, and the SRT stage (753d80,
        # no .srt instances) follows the trees with the path flag.
        for index, case in enumerate(cases):
            quarter_path = case['hide'] and case.get('optimiser', True)
            trees = [['trees', int(not case.get('draw', True))], ['srt', int(quarter_path)]]
            if not case.get('bound', True):
                trees = []
            expected = ([['acquire', 3], ['acquire', 4]] + trees + [['release', 0x704], ['release', 0x703]]
                        if quarter_path else trees)
            self.assertEqual([e[:2] for e in original[index] if e[0] in ('acquire', 'release', 'srt', 'trees')],
                             expected, case)
        # The original blur struct is local lighting's (a69bb7) plus +0x28 DRR.
        self.assertEqual(original[1][11][:9], ['blur', 0x703, 0x704, 0, 0.5, 1.0, 2, 9, 0])
        # Deliberate containment: the original still blurs and composites stale
        # data after a failed bind; production drains and stops.
        self.assertIn('blur', normalise_original(original[6]))
        self.assertEqual(normalise_production(produced[6]), ['bind', 'fail', 'trees 1', 'end'])
        self.assertEqual(normalise_production(produced[7]), ['trees 0'])
        self.assertEqual(normalise_production(produced[8]), ['fail', 'trees 1'])
        self.assertEqual(normalise_production(produced[9]),
                         ['bind', 'constants 2', 'clear colour 0', 'clear depth 1 0 3', 'fail', 'trees 1', 'end',
                          'constants 2'])
        self.assertEqual(normalise_production(produced[10]),
                         normalise_original(original[1])[:9] + ['fail', 'constants 2'])
        for index in (9, 10, 11, 12):
            self.assertEqual(produced[index].split('|')[-1], 'constants 2 1600x900', produced[index])
        # A failed upsample technique comes after the 9c511d restore: no second one.
        self.assertEqual(normalise_production(produced[13])[-3:], ['constants 2', 'upsample', 'fail'])
        self.assertEqual(produced[13].count('constants 2 1600x900'), 1)
        # The successful path keeps exactly the two original vslot 0x14(2)
        # calls outside the blur owner.
        self.assertEqual([e for e in normalise_production(produced[1]) if e.startswith('constants')],
                         ['constants 2', 'constants 2'])


POOL_HOST = r"""
#include <array>
#include <cstdio>
#include <string>
typedef unsigned uint;
enum D3DFORMAT { D3DFMT_UNKNOWN = 0, D3DFMT_D24S8 = 75, D3DFMT_D24X8 = 77, D3DFMT_A16B16G16R16F = 113 };
float mulTreeScalar( float a, float b ) { volatile float result = a * b; return result; }
namespace DX { struct Surface {}; }
template <class T> struct ComObjectWrap { T * p = nullptr; };
#define FAILED( hr ) ((hr) < 0)
namespace Moo
{
struct Size { uint Width, Height; };
struct PresentParameters { D3DFORMAT AutoDepthStencilFormat; };
struct RenderContext
{
    Size size; PresentParameters present;
    const Size & backBufferDesc() const { return size; }
    const PresentParameters & presentParameters() const { return present; }
    bool usingD3DDeviceEx() const { return false; }
} context;
RenderContext & rc() { return context; }
// lib/moo/render_target.cpp: create() only records; pSurface() allocates
// (ensureAllocated): a non-multisampled depth in AutoDepthStencilFormat unless
// overridden, or the main one, and a colour clear when clearOnRecreate is set.
struct RenderTarget
{
    std::string name; bool autoClear = false, reuse = false, discard = false;
    int width = 0, height = 0; D3DFORMAT pixel = D3DFMT_UNKNOWN, depth = D3DFMT_UNKNOWN;
    explicit RenderTarget( const char * label ) : name( label ) {}
    void clearOnRecreate( bool enable ) { autoClear = enable; }
    bool create( int w, int h, bool reuseZ, D3DFORMAT format, RenderTarget * parent, D3DFORMAT depthFormat,
        bool discardDepth )
    {
        width = w; height = h; reuse = reuseZ || parent; pixel = format; depth = depthFormat; discard = discardDepth;
        return true;
    }
    long pSurface( ComObjectWrap<DX::Surface> & )
    {
        const D3DFORMAT used = depth != D3DFMT_UNKNOWN ? depth : rc().presentParameters().AutoDepthStencilFormat;
        if (!reuse && used == D3DFMT_UNKNOWN) return -1;  // CreateDepthStencilSurface fails
        std::printf( "%d %d %d %d %d %d %d %d\n", width, height, pixel, reuse ? -1 : used, 0, int( autoClear ),
            int( discard ), 0 );
        return 0;
    }
};
typedef RenderTarget * RenderTargetPtr;
}
struct Owner
{
    std::array<Moo::RenderTargetPtr, 2> hidingTargets;
    void fail( const char * message ) { std::printf( "fail %s\n", message ); }
@MEMBERS@
@BODY@
};
int main()
{
    uint width, height, depth;
    while (std::scanf( "%u %u %u", &width, &height, &depth ) == 3)
    {
        Moo::context.size = { width, height };
        Moo::context.present.AutoDepthStencilFormat = D3DFORMAT( depth );
@MAIN@
    }
}
"""

# WG format enum (01851600) -> D3DFORMAT, for the formats these targets use.
D3D_FORMATS = {'D24S8': 75, 'D24X8': 77, 'A16B16G16R16F': 113}


class HidingTargetPoolTests(unittest.TestCase):
    def test_private_targets_match_the_original_pool_creation(self):
        """9c4c20 -> 999fc0 -> 99c1d0 -> 998170, against production createHidingTargets."""
        requirements(self)
        cases = [(1600, 900, 'D24S8'), (1366, 768, 'D24X8'), (1921, 1081, 'D24S8'), (3, 7, 'D24S8'),
                 (2560, 1440, 'D24X8'), (1023, 767, 'D24S8')]
        script = ('import json, sys\nsys.path.insert(0, sys.argv[1])\n'
                  'from stock_tree_hiding_oracle import PoolTargets\n'
                  'o = PoolTargets(sys.argv[2])\n'
                  'print(json.dumps([[o.format_name(f) for f in (0x1b, 0x1c, 0x24)]] +\n'
                  '    [o.pool_targets(w, h, {"D24S8": 0x1b, "D24X8": 0x1c}[d]) for w, h, d in %r]))' % cases)
        names, *original = json.loads(run_oracle_script(script))
        self.assertEqual(names, ['D24S8', 'D24X8', 'A16B16G16R16F'])
        wg = {0x1b: 'D24S8', 0x1c: 'D24X8', 0x24: 'A16B16G16R16F'}
        expected = []
        for (width, height, depth), targets in zip(cases, original):
            self.assertEqual([t['index'] for t in targets], [0, 1])
            for target in targets:
                # Both registrations ask for depth mode 3: an own depth surface.
                self.assertEqual((target['requested'], target['mode']), (3, 3))
                release, texture, surface, clear = target['calls']
                self.assertEqual((release, clear), (['release'], ['clear on create']))
                self.assertEqual(texture[3:], [1, 12, 0x28, 0x24])
                self.assertEqual(surface[1:3], texture[1:3])
                # Depth: kind 8, bind 0x40, the device format, no multisampling.
                self.assertEqual(surface[3:7] + surface[8:], [1, 8, 0, 0x40, 0, 0])
                self.assertEqual(wg[surface[7]], depth)
                self.assertEqual(target['clear'], [1, 0.0, 0.0, 0.0, 0.0])
                expected.append([texture[1], texture[2], D3D_FORMATS[wg[texture[6]]], D3D_FORMATS[depth], 0,
                                 target['clear'][0]])
        self.assertEqual(original[0][0]['calls'][1][1:3], [400, 225])
        scene = (ROOT / 'bigworld/stock_tree_scene.cpp').read_text()
        body = function(scene, 'void createHidingTargets()')
        with tempfile.TemporaryDirectory() as folder:
            binary = compile_host(Path(folder), POOL_HOST.replace('@MEMBERS@', '').replace('@BODY@', body).replace(
                '@MAIN@', '        Owner().createHidingTargets();'), 'pool_targets')
            result = subprocess.run([str(binary)], input=''.join(
                '%d %d %d\n' % (w, h, D3D_FORMATS[d]) for w, h, d in cases), capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        # Discard (column 7) has no original equivalent to compare: every bind clears depth first.
        produced = [line.split()[0] if line.startswith('fail') else list(map(int, line.split()))[:6]
                    for line in result.stdout.splitlines()]
        # Deliberate difference below 4 pixels: 999fc0 truncates the quarter to 0
        # and 998bc0 re-resolves 0 as the full back buffer; production refuses it.
        tiny = cases.index((3, 7, 'D24S8'))
        self.assertEqual(expected[2 * tiny:2 * tiny + 2], [[3, 1, 113, 75, 0, 1]] * 2)
        expected[2 * tiny:2 * tiny + 2] = ['fail'] * 2
        self.assertEqual(produced, expected)


RESET_MEMBERS = """
    bool active = true, gpuReady = true;
    void deleteGpuObjects() { std::printf( "delete gpu\\n" ); }
    void createGpuObjects() { std::printf( "create gpu\\n" ); }
"""
RESET_MAIN = """
        static Owner owner;
        owner.active = width != 7;
        owner.deleteUnmanagedObjects();
        std::printf( "deleted %d %d\\n", int( owner.hidingTargets[0] != NULL ), int( owner.hidingTargets[1] != NULL ) );
        owner.createUnmanagedObjects();
        std::printf( "created %d %d\\n", int( owner.hidingTargets[0] != NULL ), int( owner.hidingTargets[1] != NULL ) );
"""


class HidingTargetResetTests(unittest.TestCase):
    def test_device_reset_recreates_and_failed_init_clears_the_targets(self):
        requirements(self, oracle=False)
        scene = (ROOT / 'bigworld/stock_tree_scene.cpp').read_text()
        body = '\n'.join(function(scene, signature) for signature in (
            'virtual void deleteUnmanagedObjects()', 'virtual void createUnmanagedObjects()',
            'void createHidingTargets()'))
        with tempfile.TemporaryDirectory() as folder:
            binary = compile_host(Path(folder), POOL_HOST.replace('@MEMBERS@', RESET_MEMBERS).replace('@BODY@', body).replace(
                '@MAIN@', RESET_MAIN), 'hiding_reset')
            result = subprocess.run([str(binary)], input='1600 900 75\n1920 1080 77\n3 900 75\n1600 900 0\n7 900 75\n'
                                    '1600 900 75\n', capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        lost, made = 'deleted 0 0', 'created 1 1'
        self.assertEqual(result.stdout.splitlines(), [
            lost, '400 225 113 75 0 1 0 0', '400 225 113 75 0 1 0 0', made,
            # A reset at another size or depth format recreates both targets.
            lost, '480 270 113 77 0 1 0 0', '480 270 113 77 0 1 0 0', made,
            # A zero quarter dimension fails and leaves no stale target.
            lost, 'fail Cannot create the original tree-hiding target',
            'fail Cannot create the original tree-hiding target', 'created 0 0',
            # Allocation happens at creation (998170), so its failure does too.
            lost, 'fail Cannot create the original tree-hiding target',
            'fail Cannot create the original tree-hiding target', 'created 0 0',
            # An inactive owner (no space) creates none; reactivation does.
            lost, 'created 0 0', lost, '400 225 113 75 0 1 0 0', '400 225 113 75 0 1 0 0', made])


def run_oracle_script(script):
    run = subprocess.run([str(ORACLE_PYTHON), '-c', script, str(TESTS), str(CLIENT / 'WorldOfTanks.exe')],
                         capture_output=True, text=True, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
    if run.returncode:
        raise AssertionError(run.stdout + run.stderr)
    return run.stdout


class UnrestoredFocusConsumerGraphTests(unittest.TestCase):
    def test_srt_csm_and_asm_focus_consumers_have_the_recorded_original_graph(self):
        """The camera-target consumers this client does not own, as #1513 wires them.

        HANDOFF records why each one is unreachable or needs its own owner.
        """
        requirements(self)
        script = ('import json, sys\nsys.path.insert(0, sys.argv[1])\n'
                  'from stock_tree_hiding_oracle import focus_consumer_graph\n'
                  'print(json.dumps(focus_consumer_graph(sys.argv[2])))')
        graph = json.loads(run_oracle_script(script))
        callers = {int(k, 16): v for k, v in graph['callers'].items()}
        code = {int(k, 16): bytes.fromhex(v) for k, v in graph['bytes'].items()}
        # Slot 0x10c: both pipelines gate the old-ST4 and SRT targets by
        # `enabled`; only deferred also hands the raw target to 96ea70.
        deferred, forward = code[0x9c5dd0], code[0x9c6a40]
        self.assertEqual(deferred[:0x0f], forward[:0x0f])
        self.assertEqual(deferred[0x27:0x2f], bytes.fromhex('ff7508e8718cfaff'))
        self.assertEqual(forward[0x27:], bytes.fromhex('5e5dc2'))
        self.assertEqual(callers[0x748150], [0x9c5df2, 0x9c6a62])
        self.assertEqual(callers[0x96ea70], [0x9c5dfa])
        # 96ea70 stores the raw pointer, unreferenced, into one global each.
        self.assertEqual(code[0x96ea70], bytes.fromhex('558becff7508e855e7050083c4045de95cd90100'))
        self.assertEqual(code[0x9cd1d0], bytes.fromhex('558bec8b4508a3448ed3015dc3'))
        self.assertEqual(code[0x98c3e0], bytes.fromhex('558bec8b4508a33088d3015dc3'))
        self.assertEqual(graph['references'], {'0x1d38e44': [0x9cb0a2, 0x9cd1d7], '0x1d38830': [0x9863a5, 0x98c3e7],
                                               '0x74e510': [], '0x74f150': []})
        # WG CSM split (9cb010) runs from the shadow update; ASM (986220) per atlas.
        self.assertEqual(callers[0x9cb010], [0x96e415])
        self.assertEqual(callers[0x986220], [0x9852c8, 0x9852e2, 0x9852fc])
        # SRT: writers 748150/748190/7481d0 and the 74c9a0 constructor; readers
        # 74e510 (per frame) and 74f150 (per SRT instance) only.
        writers = {0x748178, 0x7481b8, 0x7481fa, 0x748206, 0x748213}
        constructor = {0x74cdda, 0x74cde4, 0x74cdf9, 0x74ce03, 0x74ce0d}
        sites = {site for field in graph['fields'].values() for site, unused in field}
        self.assertEqual(sites - writers - constructor,
                         {0x74ead9, 0x74eb01, 0x74eb1b, 0x74eb31, 0x74eb29, 0x74eb23, 0x74eb49, 0x74eb74,
                          0x74f207, 0x74f4ee, 0x74f2d6, 0x74f17e, 0x74f1d7, 0x74f1ba, 0x74f1cf})
        self.assertTrue(writers | constructor <= sites)
        self.assertEqual(callers[0x748190], [0x6b1851])
        self.assertEqual(callers[0x7481d0], [0x6b3dac])
        # 74e510: App tick 637e62 -> 748370 -> 755670 -> jmp.
        self.assertEqual((callers[0x748370], callers[0x755670], callers[0x74e510]), ([0x637e62], [0x7483fe], [0x755679]))
        self.assertEqual(code[0x755670], bytes.fromhex('66c781751100000000e9928effff'))
        # 74f150: only StaticTreeFeature's draw, for an instance whose +8 SRT
        # model is set (748260 '.srt'); +4 old-ST4 instances go to afed60.
        self.assertEqual((callers[0x74f150], callers[0x753330], callers[0x747180]), ([0x7533c8], [0x7471ad], [0x6c58ff]))
        # Forward slot 9c69c0: afef30( ctx, 0 ) draws the faded old-ST4 trees, then
        # the SRT stage 753d80( ctx, 0 ); the deferred 9c4fff/9c512b pair the same way.
        self.assertEqual(code[0x9c69c0], bytes.fromhex(
            '558bec568b7508576a00568bf9e85e8513008b4f3083c4086a0056e8a0d3d8ff'))
        self.assertEqual(callers[0xafef30], [0x9c5025, 0x9c5191, 0x9c69cd])
        self.assertEqual(callers[0x753d80], [0x9c5041, 0x9c51ad, 0x9c69db])
        self.assertEqual(code[0x6c58ef], bytes.fromhex(
            '8b460885c074108b0db411d2016a0150e87c180800eb118b4e0485c9740aff36ff759ce849944300'))


class HidingConsumerReachabilityTests(unittest.TestCase):
    def test_every_published_target_has_a_production_reader(self):
        """The base adapter stored targets that no production code read."""
        sources = {path.name: path.read_text() for path in (ROOT / 'bigworld').glob('*.cpp')}
        publish = function(sources['stock_cursor_camera.cpp'],
                           'void publishStockCursorCameraHiding( MatrixProvider * target, bool enabled )')
        self.assertIn('setStockTreeCameraTarget( enabled ? target : NULL );', publish)
        self.assertNotIn('stockCameraFocusTarget', ''.join(sources.values()))
        scene = sources['stock_tree_scene.cpp']
        readers = [signature for signature in (
            'void tickStockTreeSceneRendering( float dTime )',
            'bool fadeTreeInstance( TreeInstance & instance, const wot::StockTreeFadeInput & input )')
            if 'treeHiding()' in function(scene, signature)]
        self.assertEqual(len(readers), 2)
        fade = function(scene, 'bool fadeTreeInstance( TreeInstance & instance, const wot::StockTreeFadeInput & input )')
        self.assertIn('hiding.settings, hiding.target && hiding.frameGeneration == hiding.generation ? &hiding.frame : NULL, input', fade)
        self.assertNotIn('StockTreeHidingSettings()', scene)
        draw = function(scene, 'virtual void doDraw( const SceneDrawContext & context, const SceneObject * objects, size_t count )')
        self.assertIn('fadeTreeInstance( *instance, input )', draw)
        self.assertIn('owner->semitransparent.push_back( item )', draw)
        trees = (ROOT / 'bigworld/patch_stock_space_trees.py').read_text()
        self.assertEqual(trees.count('drawStockTreeSemitransparent( false )'), 1)
        self.assertEqual(trees.count('drawStockTreeSemitransparentStage('), 1)


if __name__ == '__main__':
    unittest.main()
