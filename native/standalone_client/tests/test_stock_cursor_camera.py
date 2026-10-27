"""Verify the #1513 CursorCamera extensions against the original instructions."""
import importlib.util
import math
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TESTS = Path(__file__).resolve().parent
ARCHIVE = Path(os.environ.get('WOT_PUBLIC_BIGWORLD_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
CLIENT = Path(os.environ.get('WOT_0922_CLIENT', '/Users/peng/Downloads/World_of_Tanks_0.09.22.00.01_CH_1513_HD'))
ORACLE_PYTHON = Path(os.environ.get('WOT_ORACLE_PYTHON', '/tmp/wot-native-boundaries-venv/bin/python'))
PY27 = Path(os.environ.get('WOT_PY27', str(Path.home() / '.pyenv/versions/2.7.18/bin/python2.7')))
ENGINE_FILES = ('lib/camera/cursor_camera.hpp', 'lib/camera/cursor_camera.cpp',
                'lib/camera/cursor_camera.ipp', 'lib/romp/water.hpp', 'lib/math/angle.hpp',
                'lib/math/angle.ipp', 'lib/math/mathdef.hpp', 'lib/math/matrix.ipp',
                'lib/math/vector3.ipp', 'client/script_app.cpp')
WATER_QUERY = '\tfloat stockGroundRayDistance(const Vector3& start, const Vector3& end) const;'


def load_patch():
    spec = importlib.util.spec_from_file_location('cursor_camera_patch',
                                                  ROOT / 'bigworld/patch_stock_cursor_camera.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fresh_engine(root, ground_effects=True):
    with tarfile.open(ARCHIVE) as original:
        prefix = original.getmembers()[0].name.split('/')[0] + '/programming/bigworld/'
        for name in ENGINE_FILES:
            target = root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(original.extractfile(prefix + name).read())
    if ground_effects:
        # The seam patch_stock_ground_effects.py installs before this patch.
        water = root / 'lib/romp/water.hpp'
        text = water.read_text(encoding='utf-8-sig')
        anchor = '\tbool visible() const { return visible_; }'
        water.write_text(text.replace(anchor, WATER_QUERY + '\n' + anchor, 1))
        script = root / 'client/script_app.cpp'
        text = script.read_text(encoding='utf-8-sig')
        text = text.replace('#include "pch.hpp"',
                            '#include "pch.hpp"\n#include "stock_ground_effects.hpp"', 1)
        text = text.replace('\tBigWorldClientScript::fini();',
                            '\tfiniStockGroundEffects();\n\tBigWorldClientScript::fini();', 1)
        script.write_text(text)


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


def requirements(test, oracle=True):
    if not ARCHIVE.exists():
        test.skipTest('Pinned public BigWorld archive required')
    if oracle and (not ORACLE_PYTHON.exists() or not (CLIENT / 'WorldOfTanks.exe').exists()):
        test.skipTest('Exact #1513 client and the Unicorn oracle environment required')


def run_oracle(script, *args):
    result = subprocess.run([str(ORACLE_PYTHON), '-c', script, str(TESTS), str(CLIENT / 'WorldOfTanks.exe')] +
                            list(args), capture_output=True, text=True,
                            env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return result.stdout


def compile_host(folder, source, name):
    cpp = folder / (name + '.cpp')
    binary = folder / name
    cpp.write_text(source)
    result = subprocess.run(['c++', '-std=c++14', '-O1', '-ffp-contract=off', '-Wall', '-Wno-unused-function',
                             '-Wno-unused-variable', '-I' + str(TESTS), str(cpp), '-o', str(binary)],
                            capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(result.stderr[-6000:])
    return binary


class CursorCameraPatchTests(unittest.TestCase):
    def test_patch_is_idempotent_and_publishes_the_adapter_header(self):
        requirements(self, oracle=False)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            fresh_engine(root)
            patch = load_patch()
            patch.patch(root)
            files = sorted(p for p in root.rglob('*') if p.is_file())
            before = {p: p.read_bytes() for p in files}
            patch.patch(root)
            self.assertEqual(before, {p: p.read_bytes() for p in files})
            self.assertEqual((root / 'lib/stock_cursor_camera.hpp').read_text(),
                             (ROOT / 'bigworld/stock_cursor_camera.hpp').read_text())
            source = (root / 'lib/camera/cursor_camera.cpp').read_text()
            header = (root / 'lib/camera/cursor_camera.hpp').read_text()
            self.assertEqual(source.count('void CursorCamera::updateStock( float deltaTime, bool stablePass )'), 1)
            self.assertNotIn('collidedWithScene', source)
            self.assertNotIn('isInsideBoundary', source)
            self.assertIn('PY_AUTO_METHOD_DECLARE( RETVOID, wg_applyParams, END )', header)
            self.assertIn('PY_AUTO_METHOD_DECLARE( RETOK, wg_setModelsToCollideWith,\n\t\tARG( PyObjectPtr, END ) )',
                          header)
            self.assertIn('PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( bool, sptHiding, sptHiding )', header)
            script = (root / 'client/script_app.cpp').read_text()
            self.assertIn('#include "stock_cursor_camera.hpp"', script)
            self.assertEqual(script.count('finiStockCursorCameraHiding();'), 1)
            self.assertLess(script.index('finiStockCursorCameraHiding();'),
                            script.index('BigWorldClientScript::fini();'))
            methods = function(source, 'PY_BEGIN_METHODS( CursorCamera )')
            # The original method table at 01a13678 lists exactly these three.
            self.assertEqual([line.strip() for line in methods.splitlines() if 'PY_METHOD(' in line],
                             ['PY_METHOD( shake )', 'PY_METHOD( wg_setModelsToCollideWith )',
                              'PY_METHOD( wg_applyParams )'])

    def test_every_changed_anchor_fails_closed_without_writing(self):
        requirements(self, oracle=False)
        patch = load_patch()
        mutations = (
            ('lib/camera/cursor_camera.cpp', '\t\tif (inaccuracyProvider_)', '\t\tif (inaccuracyProvider_ != NULL)'),
            ('lib/camera/cursor_camera.cpp', 'CursorCamera::~CursorCamera()\n{\n}', 'CursorCamera::~CursorCamera()\n{\n\t;\n}'),
            ('lib/camera/cursor_camera.cpp', '\tPY_METHOD( shake )\n', '\tPY_METHOD( shakeIt )\n'),
            ('lib/camera/cursor_camera.hpp', '\tVector3 cameraDirInWS_;\n\t//@}\n', '\tVector3 cameraDirInWS_;\n'),
            ('lib/romp/water.hpp', WATER_QUERY, ''),
            ('client/script_app.cpp', '\tfiniStockGroundEffects();', ''),
        )
        for relative, before, after in mutations:
            with self.subTest(anchor=before), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                fresh_engine(root)
                path = root / relative
                text = path.read_text(encoding='utf-8-sig')
                self.assertIn(before, text)
                path.write_text(text.replace(before, after, 1))
                files = sorted(p for p in root.rglob('*') if p.is_file())
                snapshot = {p: p.read_bytes() for p in files}
                with self.assertRaises(ValueError):
                    patch.patch(root)
                self.assertEqual(snapshot, {p: p.read_bytes() for p in sorted(root.rglob('*')) if p.is_file()})

    def test_constructor_defaults_match_the_original_factory(self):
        requirements(self)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            fresh_engine(root)
            load_patch().patch(root)
            source = (root / 'lib/camera/cursor_camera.cpp').read_text()
        constructor = function(source, 'CursorCamera::CursorCamera( PyTypeObject * pType ) :')
        self.assertIn('\tsptHiding_( false ),\n\tstableView_( Matrix::identity ),\n'
                      '\tstableInvView_( Matrix::identity )\n{', constructor)
        script = r'''
import sys
sys.path.insert(0, sys.argv[1])
import stock_cursor_camera_oracle as o
scene = o.CameraScene(o.OriginalCursorCamera(sys.argv[2]))
camera = scene.construct()
e = scene.emulator
print(' '.join('%s=%s' % (name, e.byte(camera + o.OFFSET[name]) if kind == 'b' else
                ','.join(repr(v) for v in e.floats(camera + o.OFFSET[name], count)))
               for name, kind, count in o.FIELDS))
print('stable=%s' % (e.floats(camera + o.OFFSET['stableView'], 16) == e.floats(camera + o.OFFSET['stableInvView'], 16)
                     == [1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0]))
print('pointers=%s' % [e.word(camera + o.OFFSET[n]) for n in ('target', 'source', 'inaccuracy', 'models', 'trees')])
'''
        lines = run_oracle(script).splitlines()
        state = dict(item.split('=') for item in lines[0].split())
        half_pi = repr(float(struct_round(math.pi / 2)))
        expected = {'pivot': '0.0,2.0,0.0', 'targetPivot': '0.0,2.0,0.0', 'maxDist': '3.5',
                    'targetMaxDist': '3.5', 'maxDistHalfLife': '1.5', 'minDist': '1.5',
                    'movementHalfLife': '0.10000000149011612', 'turningHalfLife': '0.10000000149011612',
                    'upright': '0.0,1.0,0.0', 'firstPerson': '0', 'reverseView': '0', 'inPosition': '1',
                    'limitVelocity': '0', 'maxVelocity': '2.0', 'shakeAmount': '0.0,0.0,0.0',
                    'shakeTime': '0.0', 'shakeLeft': '0.0', 'yaw': half_pi, 'pitch': '0.0', 'roll': '0.0',
                    'lastKnown': '0', 'lastYaw': half_pi, 'lastPitch': '0.0', 'lastRoll': '0.0',
                    'cameraPos': '0.0,0.0,0.0', 'cameraDir': '0.0,0.0,1.0', 'sptHiding': '0'}
        self.assertEqual(state, expected)
        self.assertEqual(lines[1:], ['stable=True', 'pointers=[0, 0, 0, 0, 0]'])
        # The public initialisers the patch keeps carry the same values.
        for text in ('pivotPosition_( 0.0f, 2.0f, 0.0f )', 'maxDistanceFromPivot_( 3.5f )',
                     'maxDistHalfLife_( 1.5f )', 'minDistanceFromPivot_( 1.5f )',
                     'movementHalfLife_( 0.1f )', 'turningHalfLife_( 0.1f )', 'inPosition_( true )',
                     'yaw_( MATH_PI / 2.0f )', 'maxVelocity_( 2.f )', 'lastDesiredKnown_( false )'):
            self.assertIn(text, constructor)


def struct_round(value):
    import ctypes
    return ctypes.c_float(value).value


UPDATE_HOST = r'''
#include "stock_cursor_camera_test_engine.hpp"
#include <string>

std::vector<std::string> traceLines;
static void hexes(std::string & line, const float * values, int count)
{
	char buffer[16];
	for (int i = 0; i < count; ++i)
	{
		uint32_t word;
		std::memcpy(&word, values + i, 4);
		std::snprintf(buffer, sizeof(buffer), " %08x", word);
		line += buffer;
	}
}
static void vec(std::string & line, const Vector3 & v) { hexes(line, &v.x, 3); }

struct Script { int result; Vector3 delta; };
Script modelsScript, spaceScript, boundsScript;
std::vector<float> depths;

struct Water
{
	int index;
	float stockGroundRayDistance( const Vector3 & start, const Vector3 & end ) const
	{
		std::string line = "W " + std::to_string(index);
		vec(line, start); vec(line, end);
		traceLines.push_back(line);
		return depths[index];
	}
};
struct Waters : std::vector<Water *>
{
	static Waters & instance() { static Waters value; return value; }
};

static void apply(Vector3 & position, const Script & script)
{
	position = Vector3(position.x + script.delta.x, position.y + script.delta.y, position.z + script.delta.z);
}

class BSPTree;
bool collideStockCursorCameraModels( const BW::vector< PyObjectPtr > &, const BW::vector< const BSPTree * > &,
	Vector3 & idealPosition, const Vector3 & direction, const Vector3 & up )
{
	std::string line = "M";
	vec(line, idealPosition); vec(line, direction); vec(line, up);
	traceLines.push_back(line);
	apply(idealPosition, modelsScript);
	return modelsScript.result != 0;
}
void publishStockCursorCameraHiding( MatrixProvider * target, bool enabled )
{
	traceLines.push_back("H " + std::to_string(target ? target->label : 0) + " " + std::to_string(int(enabled)));
}

struct BaseCamera
{
	Matrix view_, invView_;
	static bool sceneCheck( Vector3 & cameraPosInWS, const Vector3 & look, const Vector3 & direction,
		const Vector3 & up )
	{
		std::string line = "C";
		vec(line, cameraPosInWS); vec(line, look); vec(line, direction); vec(line, up);
		traceLines.push_back(line);
		apply(cameraPosInWS, spaceScript);
		apply(cameraPosInWS, boundsScript);
		return spaceScript.result != 0 || boundsScript.result != 0;
	}
};

class CursorCamera : public BaseCamera
{
public:
	const Vector3 &pivotPosition( void ) const;
	void pivotPosition( const Vector3 &newPosition );
	void setPivotPosition( const Vector3 &pos );
	void scalePivotPosition( const Vector3 &factor );
	float maxDistanceFromPivot( void ) const;
	float targetMaxDistanceFromPivot( void ) const;
	float minDistanceFromPivot( void ) const;
	void minDistanceFromPivot( float newDistance );
	float minDistanceFromTerrain( void ) const;
	void minDistanceFromTerrain( float newDistance );
	float movementHalfLife( void ) const;
	void movementHalfLife( float halfLifeInSeconds );
	float turningHalfLife( void ) const;
	void turningHalfLife( float halfLifeInSeconds );
	const Vector3 &uprightDirection( void ) const;
	void uprightDirection( const Vector3 &newDirection );
	void target( MatrixProviderPtr pMProv );
	void source( MatrixProviderPtr pMProv );
	const bool firstPersonPerspective( void ) const;
	void firstPersonPerspective( bool flag );
	bool reverseView( void ) const;
	void reverseView( bool flag );
	const Vector3 &dirInWS( void ) const;
	const Vector3 &posInWS( void ) const;
	const Angle &yaw( void ) const;
	void yaw( const Angle &yawInRadians );
	const Angle &pitch( void ) const;
	void pitch( const Angle &pitchInRadians );
	const Angle &roll( void ) const;
	void roll( const Angle &rollInRadians );
	void update( float deltaTime );
	void updateStock( float deltaTime, bool stablePass );
	void stockViewMatrix( Matrix & m, bool inverse ) const;
	void set( const Matrix & viewMatrix );

	Vector3 pivotPosition_, targetPivotPosition_;
	float maxDistanceFromPivot_, targetMaxDistanceFromPivot_, maxDistHalfLife_, minDistanceFromPivot_;
	float minDistanceFromTerrain_, movementHalfLife_, turningHalfLife_;
	Vector3 uprightDirection_;
	MatrixProviderPtr pTarget_, pSource_;
	bool firstPersonPerspective_, reverseView_, inPosition_, limitVelocity_;
	float maxVelocity_;
	Vector3 shakeAmount_;
	float shakeTime_, shakeLeft_;
	Vector4ProviderPtr inaccuracyProvider_;
	Angle yaw_, pitch_, roll_;
	bool lastDesiredKnown_;
	Angle lastDesiredYaw_, lastDesiredPitch_, lastDesiredRoll_;
	Vector3 cameraPosInWS_, cameraDirInWS_;
	bool sptHiding_;
	BW::vector< PyObjectPtr > stockModels_;
	BW::vector< const BSPTree * > stockModelTrees_;
	Matrix stableView_, stableInvView_;
};

@IPP@

@PRODUCTION@

static uint32_t readU() { unsigned long long v; if (!(std::cin >> v)) std::abort(); return uint32_t(v); }
static float readF() { uint32_t u = readU(); float f; std::memcpy(&f, &u, 4); return f; }
static Vector3 readV() { float x = readF(); float y = readF(); return Vector3(x, y, readF()); }
static Matrix readM() { Matrix m; for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) m.m[i][j] = readF(); return m; }
static Script readScript() { Script s; s.result = int(readU()); s.delta = readV(); return s; }

int main()
{
	Moo::rc().camera_ = Moo::Camera{ 0.5f, 800.f, 1.2f, 1.6f };
	CursorCamera c;
	c.pivotPosition_ = readV(); c.targetPivotPosition_ = readV();
	c.maxDistanceFromPivot_ = readF(); c.targetMaxDistanceFromPivot_ = readF(); c.maxDistHalfLife_ = readF();
	c.minDistanceFromPivot_ = readF(); c.movementHalfLife_ = readF(); c.turningHalfLife_ = readF();
	c.uprightDirection_ = readV();
	c.firstPersonPerspective_ = readU(); c.reverseView_ = readU(); c.inPosition_ = readU(); c.limitVelocity_ = readU();
	c.maxVelocity_ = readF(); c.shakeAmount_ = readV(); c.shakeTime_ = readF(); c.shakeLeft_ = readF();
	c.yaw_ = Angle(readF()); c.pitch_ = Angle(readF()); c.roll_ = Angle(readF());
	c.lastDesiredKnown_ = readU();
	c.lastDesiredYaw_ = Angle(readF()); c.lastDesiredPitch_ = Angle(readF()); c.lastDesiredRoll_ = Angle(readF());
	c.cameraPosInWS_ = readV(); c.cameraDirInWS_ = readV(); c.sptHiding_ = readU();
	c.view_ = c.invView_ = c.stableView_ = c.stableInvView_ = Matrix::identity;
	MatrixProvider target, source;
	target.label = 1;
	Vector4Provider inaccuracy;
	if (readU()) c.pTarget_ = MatrixProviderPtr(&target);
	if (readU()) c.pSource_ = MatrixProviderPtr(&source);
	if (readU()) c.inaccuracyProvider_ = Vector4ProviderPtr(&inaccuracy);
	const uint32_t frames = readU();
	for (uint32_t frame = 0; frame < frames; ++frame)
	{
		const float dt = readF();
		if (readU()) c.set(readM());
		target.value = readM(); source.value = readM();
		inaccuracy.value.x = readF(); inaccuracy.value.y = readF(); inaccuracy.value.z = readF(); inaccuracy.value.w = readF();
		oracle::randoms.clear();
		for (uint32_t n = readU(); n; --n) oracle::randoms.push_back(int(readU()));
		modelsScript = readScript(); spaceScript = readScript(); boundsScript = readScript();
		depths.clear();
		Waters::instance().clear();
		static std::vector<Water> waters(16);
		for (uint32_t n = readU(), i = 0; i < n; ++i)
		{
			depths.push_back(readF());
			waters[i].index = int(i);
			Waters::instance().push_back(&waters[i]);
		}
		std::vector<std::string> calls;
		traceLines.clear();
		c.update(dt);
		if (!oracle::randoms.empty()) return 7;
		// Group as the oracle reports: models, scene, water, hiding.
		for (const char * kind : { "M", "C", "W", "H" })
			for (const std::string & line : traceLines)
				if (line.compare(0, 1, kind) == 0) std::printf("%s\n", line.c_str());
		std::string s = "S";
		vec(s, c.cameraPosInWS_); vec(s, c.cameraDirInWS_);
		float angles[3] = { c.yaw_, c.pitch_, c.roll_ };
		hexes(s, angles, 3); vec(s, c.pivotPosition_); hexes(s, &c.maxDistanceFromPivot_, 1);
		s += " " + std::to_string(int(c.inPosition_)) + " " + std::to_string(int(c.limitVelocity_)) + " " +
			std::to_string(int(c.lastDesiredKnown_));
		float last[4] = { c.lastDesiredYaw_, c.lastDesiredPitch_, c.lastDesiredRoll_, c.shakeLeft_ };
		hexes(s, last, 4);
		std::printf("%s\n", s.c_str());
		const Matrix * matrices[4] = { &c.view_, &c.invView_, &c.stableView_, &c.stableInvView_ };
		const char * names[4] = { "view", "invView", "stableView", "stableInvView" };
		for (int i = 0; i < 4; ++i)
		{
			std::string line = names[i];
			hexes(line, &matrices[i]->m[0][0], 16);
			std::printf("%s\n", line.c_str());
		}
		Matrix view, inverse;
		c.stockViewMatrix(view, false);
		c.stockViewMatrix(inverse, true);
		std::string p = "P";
		hexes(p, &view.m[0][0], 16); hexes(p, &inverse.m[0][0], 16);
		std::printf("%s\n", p.c_str());
	}
	return 0;
}
'''


def update_host_source(root):
    """Assemble the host harness from production and pinned public sources."""
    engine = (TESTS / 'stock_cursor_camera_test_engine.hpp').read_text()
    mathdef = (root / 'lib/math/mathdef.hpp').read_text()
    decay = function(mathdef, 'inline float decay( float srcValue,')
    angle_hpp = (root / 'lib/math/angle.hpp').read_text()
    angle_class = angle_hpp[angle_hpp.index('class Angle\n'):]
    angle_class = angle_class[:angle_class.index('\n};\n') + 4]
    angle_ipp = (root / 'lib/math/angle.ipp').read_text()
    matrix_ipp = (root / 'lib/math/matrix.ipp').read_text()
    angles = '\n'.join(function(matrix_ipp, 'INLINE float Matrix::' + name + '() const')
                       .replace('INLINE float Matrix::' + name + '() const', 'float ' + name + '() const')
                       for name in ('yaw', 'pitch', 'roll'))
    engine = engine.replace('// @MATH_DECAY@', decay)
    engine = engine.replace('// @ANGLE@', '#define pow oracle::powf_\n' + angle_class + angle_ipp + '\n#undef pow\n')
    engine = engine.replace('// @MATRIX_ANGLES@', angles)
    vector_ipp = (root / 'lib/math/vector3.ipp').read_text()
    engine = engine.replace('// @VECTOR_ANGLES@', '\n'.join(
        function(vector_ipp, 'INLINE float Vector3::' + name + '() const')
        .replace('INLINE float Vector3::' + name + '() const', 'float ' + name + '() const')
        for name in ('pitch', 'yaw')))
    source = (root / 'lib/camera/cursor_camera.cpp').read_text()
    production = '\n'.join(function(source, signature) for signature in (
        'void CursorCamera::update( float deltaTime )',
        'void CursorCamera::updateStock( float deltaTime, bool stablePass )',
        'void CursorCamera::stockViewMatrix( Matrix & m, bool inverse ) const',
        'void CursorCamera::set( const Matrix & viewMatrix )'))
    ipp = (root / 'lib/camera/cursor_camera.ipp').read_text()
    host = UPDATE_HOST.replace('@IPP@', ipp).replace('@PRODUCTION@', production)
    return host.replace('#include "stock_cursor_camera_test_engine.hpp"', engine)


def rotation(yaw, pitch=0.0, roll=0.0, translation=(0.0, 0.0, 0.0)):
    from stock_cursor_camera_oracle import CameraMath
    m = CameraMath.yaw_pitch_roll(struct_round(yaw), struct_round(pitch), struct_round(roll))
    m[3] = [struct_round(translation[0]), struct_round(translation[1]), struct_round(translation[2]), 1.0]
    return tuple(tuple(row) for row in m)


def update_scenarios():
    sys.path.insert(0, str(TESTS))
    frames = lambda count, **kw: [dict(kw, dt=kw.get('dt', 0.033)) for unused in range(count)]
    moving = [dict(dt=0.033, target=rotation(0.0, translation=(10.0 + i, 2.0, 5.0 - 0.5 * i)),
                   source=rotation(0.2 * i, 0.1 * (i - 3))) for i in range(8)]
    shake = [dict(dt=0.05, target=rotation(0.0, translation=(3.0, 1.0, -2.0)), source=rotation(0.3, 0.2),
                  random=[(17 * i + 5) % 32768, (991 * i + 3) % 32768, (12345 * i) % 32768] if i < 3 else [])
             for i in range(5)]
    return {
        'apply_params': dict(camera=dict(maxDist=0.0, targetMaxDist=0.0, minDist=0.0, movementHalfLife=0.0,
                                         turningHalfLife=0.15, maxDistHalfLife=0.15, pivot=(0.0, 0.0, 0.0),
                                         targetPivot=(0.0, 0.0, 0.0)),
                             frames=[dict(dt=1.0, target=rotation(0.0, translation=(1.0, 2.0, 3.0)),
                                          source=rotation(0.7, 0.3))]),
        'moving': dict(frames=moving),
        'gliding': dict(camera=dict(inPosition=False, targetPivot=(0.5, 2.5, -0.5), targetMaxDist=6.0),
                        frames=moving),
        'shake': dict(camera=dict(shakeAmount=(0.4, 0.2, 0.1), shakeTime=0.12, shakeLeft=0.12,
                                  inPosition=False), frames=shake),
        'first_person': dict(camera=dict(firstPerson=True), frames=moving[:3]),
        'reverse_view': dict(camera=dict(reverseView=True, inPosition=False), frames=moving[:4]),
        'limited_velocity': dict(camera=dict(limitVelocity=True, maxVelocity=0.5, inPosition=False),
                                 frames=moving[:5]),
        'limited_snap': dict(camera=dict(limitVelocity=True, maxVelocity=5000.0, inPosition=False),
                             frames=moving[:3]),
        'settling': dict(camera=dict(inPosition=False),
                         frames=[dict(dt=0.033, target=rotation(0.0, translation=(4.0, 1.0, 2.0)),
                                      source=rotation(0.4, -0.1))] * 40),
        'first_person_glide': dict(camera=dict(firstPerson=True, inPosition=False), frames=moving[:2]),
        'set_then_update': dict(frames=[dict(f, set=rotation(0.3 * i - 0.4, 0.25 - 0.2 * i, 0.0,
                                                               (1.0 - i, -2.0, 0.5 * i)))
                                        for i, f in enumerate(moving[:4])]),
        'inaccuracy': dict(frames=[dict(f, inaccuracy=(0.01 * i, -0.02 * i, 0.5, 0.0))
                                   for i, f in enumerate(moving[:4])]),
        'collisions': dict(camera=dict(inPosition=False),
                           frames=[dict(f, models=(i % 2, (0.1 * i, -0.2, 0.05)),
                                        space=(int(i == 2), (0.0, 0.3, 0.0)),
                                        bounds=(int(i == 3), (-0.1, 0.0, 0.2)))
                                   for i, f in enumerate(moving[:5])]),
        'water': dict(camera=dict(inPosition=False),
                      frames=[dict(f, waters=[(-1.0, 0.0, 0.75)[i % 3], 0.25, 2.0][:1 + i % 3])
                              for i, f in enumerate(moving[:6])]),
        'near_zero_range': dict(camera=dict(inPosition=False, minDist=3.5, maxDist=3.5, targetMaxDist=3.5,
                                            cameraPos=(10.0, 4.0, 1.5)),
                                frames=moving[:3]),
        # Old distance 5 and new distance 3 over a minimum of 1 make the divisor exactly zero.
        'zero_divisor': dict(camera=dict(inPosition=False, minDist=1.0, maxDist=3.0, targetMaxDist=3.0,
                                         turningHalfLife=0.0, cameraPos=(13.0, 4.0, 1.0)),
                             frames=[dict(dt=0.033, target=rotation(0.0, translation=(10.0, 2.0, 5.0)))]),
        'hidden_trees': dict(camera=dict(sptHiding=True), frames=moving[:2]),
        'no_target': dict(target=False, camera=dict(cameraPos=(1.0, 2.0, 3.0)), frames=moving[:2]),
        'no_source': dict(source=False, frames=moving[:2]),
    }


class CursorCameraUpdateOracleTests(unittest.TestCase):
    def test_production_update_matches_original_instructions(self):
        requirements(self)
        script = r'''
import json, sys
sys.path.insert(0, sys.argv[1])
import stock_cursor_camera_oracle as o
import test_stock_cursor_camera as t
scene = o.CameraScene(o.OriginalCursorCamera(sys.argv[2]))
result = {}
for name, scenario in t.update_scenarios().items():
    result[name] = o.run_update(scene, scenario)
missing = sorted(o.UPDATE_BRANCHES[a] for a in set(o.UPDATE_BRANCHES) - scene.coverage)
print(json.dumps({'cases': result, 'missing': missing}))
'''
        import json
        output = json.loads(run_oracle(script))
        self.assertEqual(output['missing'], [])
        cases = output['cases']
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            fresh_engine(root)
            load_patch().patch(root)
            binary = compile_host(root, update_host_source(root), 'update_host')
            for name, (trace, tokens) in sorted(cases.items()):
                with self.subTest(scenario=name):
                    result = subprocess.run([str(binary)], input=tokens, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout.splitlines(), trace)


COLLISION_HOST = r'''
#include "stock_cursor_camera_test_engine.hpp"
#include <cfloat>
#include <string>

static void hexes(const float * values, int count)
{
	for (int i = 0; i < count; ++i)
	{
		uint32_t word;
		std::memcpy(&word, values + i, 4);
		std::printf(" %08x", word);
	}
}
static uint32_t readU() { unsigned long long v; if (!(std::cin >> v)) std::abort(); return uint32_t(v); }
static float readF() { uint32_t u = readU(); float f; std::memcpy(&f, &u, 4); return f; }
static Vector3 readV() { float x = readF(); float y = readF(); return Vector3(x, y, readF()); }

struct BoundingBox
{
	Vector3 min_ = Vector3( FLT_MAX, FLT_MAX, FLT_MAX ), max_ = Vector3( -FLT_MAX, -FLT_MAX, -FLT_MAX );
	const Vector3 & minBounds() const { return min_; }
	const Vector3 & maxBounds() const { return max_; }
};

struct PyModel : PyObject
{
	int index = 0;
	Matrix world;
	bool hasBounds = false;
	Vector3 low, high;
	const Matrix & worldTransform() const { std::printf("X %d\n", index); return world; }
	void localBoundingBox( BoundingBox & bb, bool skinny )
	{
		std::printf("B %d %d\n", index, int(skinny));
		if (hasBounds) { bb.min_ = low; bb.max_ = high; }
	}
};

class BSPTree
{
public:
	int index = 0;
	mutable int calls = 0;
	std::vector< std::vector< WorldTriangle > > hits;
	bool intersects( const WorldTriangle & triangle, const Vector3 & translation, CollisionVisitor * visitor ) const
	{
		const int call = calls++;
		std::printf("T %d %d %d", index, call, int(triangle.flags_));
		hexes(&triangle.v_[0].x, 9);
		hexes(&translation.x, 3);
		std::printf("\n");
		if (call < int(hits.size()))
			for (const WorldTriangle & hit : hits[call]) visitor->visit(hit, 0.f);
		return false;
	}
};

@PRODUCTION@

int main()
{
	Moo::rc().camera_ = Moo::Camera{ 0.5f, 800.f, 1.2f, 1.6f };
	Vector3 ideal = readV();
	const Vector3 direction = readV();
	const Vector3 up = readV();
	const uint32_t count = readU();
	std::vector<PyModel> models(count);
	std::vector<BSPTree> trees(count);
	BW::vector<PyObjectPtr> owners;
	BW::vector<const BSPTree *> treePointers;
	for (uint32_t i = 0; i < count; ++i)
	{
		models[i].index = int(i);
		trees[i].index = int(i);
		for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) models[i].world.m[r][c] = readF();
		models[i].hasBounds = readU() != 0;
		if (models[i].hasBounds) { models[i].low = readV(); models[i].high = readV(); }
		for (uint32_t n = readU(); n; --n)
		{
			const uint32_t call = readU();
			if (trees[i].hits.size() <= call) trees[i].hits.resize(call + 1);
			for (uint32_t t = readU(); t; --t)
			{
				const Vector3 a = readV(), b = readV(), c = readV();
				trees[i].hits[call].push_back(WorldTriangle(a, b, c));
			}
		}
		owners.push_back(PyObjectPtr(&models[i]));
		treePointers.push_back(&trees[i]);
	}
	const bool result = collideStockCursorCameraModels(owners, treePointers, ideal, direction, up);
	std::printf("R %d", int(result));
	hexes(&ideal.x, 3);
	std::printf("\n");
	return 0;
}
'''


def collision_host_source():
    engine = (TESTS / 'stock_cursor_camera_test_engine.hpp').read_text()
    source = (ROOT / 'bigworld/stock_cursor_camera.cpp').read_text()
    visitor = source[source.index('class StockModelHitDepth'):]
    visitor = visitor[:visitor.index('\n};\n') + 4]
    production = visitor + function(source, 'bool collideStockCursorCameraModels(')
    host = COLLISION_HOST.replace('@PRODUCTION@', production)
    return host.replace('#include "stock_cursor_camera_test_engine.hpp"', engine)


def collision_scenarios():
    sys.path.insert(0, str(TESTS))
    from stock_cursor_camera_oracle import CameraMath as m

    def place(model, ideal, direction, depths, offsets=((0.0, 0.0, 0.0), (0.3, 0.1, 0.0), (0.0, 0.4, 0.2))):
        # Scripted hit vertices at chosen depths along the model-space sweep start.
        inverse = m.inverse(model['world'])
        local = m.transform(ideal, inverse)[:3]
        axis = m.transform_normal(direction, inverse)
        start = tuple(m.sub(local[i], m.mul(axis[i], 20.0)) for i in range(3))
        return [tuple(tuple(struct_round(start[i] + axis[i] * depth + offset[i]) for i in range(3))
                      for offset in offsets) for depth in depths]

    ideal = (struct_round(3.0), struct_round(1.5), struct_round(-2.0))
    direction = tuple(struct_round(v) for v in (0.6, -0.28, 0.749))
    up = (0.0, 1.0, 0.0)
    near = dict(world=rotation(0.4, 0.0, 0.0, (2.0, 1.0, -1.0)), bounds=((-3.0, -1.0, -3.0), (3.0, 2.5, 3.0)))
    scaled = dict(world=tuple(tuple(struct_round(c * (1.5 if r < 3 else 1.0)) if c not in (0.0, 1.0) or r < 3 else c
                                    for c in row) for r, row in enumerate(rotation(-0.7, 0.2, 0.1, (4.0, 1.0, -3.0)))),
                  bounds=((-4.0, -2.0, -4.0), (4.0, 3.0, 4.0)))
    cases = {
        'empty': dict(models=[]),
        'far': dict(models=[dict(near, world=rotation(0.0, translation=(40.0, 0.0, 0.0)))]),
        'outside_bounds': dict(models=[dict(near, bounds=((10.0, 10.0, 10.0), (11.0, 11.0, 11.0)))]),
        'inside_out_bounds': dict(models=[dict(world=near['world'])]),
        'no_hit': dict(models=[near]),
        'before_ideal': dict(models=[dict(near, hits={0: place(near, ideal, direction, (8.0,))})]),
        'beyond_ideal': dict(models=[dict(near, hits={1: place(near, ideal, direction, (24.0,))})]),
        'nearest_of_many': dict(models=[dict(near, hits={0: place(near, ideal, direction, (14.0, 6.0)),
                                                         1: place(near, ideal, direction, (9.0, 3.5))})]),
        'second_model_sees_moved_ideal': dict(models=[
            dict(near, hits={0: place(near, ideal, direction, (12.0,))}),
            dict(scaled, hits={1: place(scaled, ideal, direction, (10.0,))})]),
        'scaled_model': dict(models=[dict(scaled, hits={0: place(scaled, ideal, direction, (5.0, 21.0))})]),
        'singular_world': dict(models=[dict(near, world=((1.0, 0.0, 0.0, 0.0), (2.0, 0.0, 0.0, 0.0),
                                                         (0.0, 0.0, 1.0, 0.0), (3.0, 1.5, -2.0, 1.0)),
                                            hits={0: [((0.1, 0.2, 0.3), (0.2, 0.1, 0.0), (1.0, 1.0, 1.0))]})]),
    }
    local = m.transform(ideal, m.inverse(near['world']))[:3]
    for name, low_x, high_x in (('margin_inside', local[0] + 0.45, local[0] + 3.0),
                                ('margin_outside', local[0] + 0.55, local[0] + 3.0),
                                ('upper_margin_inside', local[0] - 3.0, local[0] - 0.45),
                                ('upper_margin_outside', local[0] - 3.0, local[0] - 0.5)):
        bounds = ((struct_round(low_x), -1.0, -3.0), (struct_round(high_x), 2.5, 3.0))
        cases[name] = dict(models=[dict(near, bounds=bounds, hits={0: place(near, ideal, direction, (8.0,))})])
    for name, offset in (('reach_inside', 19.99), ('reach_outside', 20.01)):
        world = rotation(0.0, translation=(ideal[0] + offset, ideal[1], ideal[2]))
        model = dict(world=world, bounds=((-30.0, -5.0, -5.0), (5.0, 5.0, 5.0)))
        model['hits'] = {0: place(model, ideal, direction, (8.0,))}
        cases[name] = dict(models=[model])
    for case in cases.values():
        case.update(ideal=ideal, direction=direction, up=up)
    return cases


class CursorCameraModelCollisionOracleTests(unittest.TestCase):
    def test_production_model_collision_matches_original_instructions(self):
        requirements(self)
        script = r'''
import json, sys
sys.path.insert(0, sys.argv[1])
import stock_cursor_camera_oracle as o
import test_stock_cursor_camera as t
scene = o.ModelCollisionScene(o.OriginalCursorCamera(sys.argv[2]))
print(json.dumps({name: [scene.run(case), o.collision_tokens(case)]
                  for name, case in t.collision_scenarios().items()}))
'''
        import json
        cases = json.loads(run_oracle(script))
        moved = [name for name, (trace, unused) in cases.items() if trace[-1].startswith('R 1')]
        self.assertEqual(sorted(moved), ['before_ideal', 'margin_inside', 'nearest_of_many', 'reach_inside',
                                         'scaled_model', 'second_model_sees_moved_ideal',
                                         'upper_margin_inside'])
        with tempfile.TemporaryDirectory() as folder:
            binary = compile_host(Path(folder), collision_host_source(), 'collision_host')
            for name, (trace, tokens) in sorted(cases.items()):
                with self.subTest(case=name):
                    result = subprocess.run([str(binary)], input=tokens, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout.splitlines(), trace)


# Model names map to BSP ids; 0 means the top model has no BSP.
MODEL_TREES = {'a': 1, 'b': 2, 'c': 3, 'd': 0, 'e': 5}
MODEL_STEPS = (
    ('not_list', ['a']), ('none', None), ('list', ['a', 'b', 'a']), ('not_list', ['c']),
    ('list', ['c', 'd', 'e']), ('list', ['e', 'x', 'a']), ('list', []), ('list_subclass', ['b', 'b']),
)

MODEL_LIST_HOST = r'''
#include <Python.h>
#include <algorithm>
#include <vector>
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define BW_GUARD
namespace BW { template <class T> using vector = std::vector<T>; }

class BSPTree { public: long id; };
struct Model
{
	const BSPTree * tree;
	const BSPTree * decompose() const { return tree; }
};
struct ModelPtr
{
	Model * p;
	explicit operator bool() const { return p != NULL; }
	Model * operator->() const { return p; }
};
struct SuperModel
{
	int count;
	Model top;
	int nModels() const { return count; }
	ModelPtr topModel( int ) { return ModelPtr{ &top }; }
};
struct PyModel : PyObject
{
	SuperModel * super_;
	BSPTree tree_;
	SuperModel superModel_;
	SuperModel * pSuperModel() const { return super_; }
	static PyTypeObject s_type_;
};
// BigWorld SmartPointer<PyObject>: a strong reference.
class PyObjectPtr
{
public:
	PyObjectPtr( PyObject * p = NULL ) : p_( p ) { Py_XINCREF( p_ ); }
	PyObjectPtr( const PyObjectPtr & o ) : p_( o.p_ ) { Py_XINCREF( p_ ); }
	PyObjectPtr & operator=( const PyObjectPtr & o ) { PyObject * old = p_; p_ = o.p_; Py_XINCREF( p_ ); Py_XDECREF( old ); return *this; }
	~PyObjectPtr() { Py_XDECREF( p_ ); }
	PyObject * get() const { return p_; }
private:
	PyObject * p_;
};

@PRODUCTION@

static PyObject * newModel( PyTypeObject * type, PyObject * args, PyObject * )
{
	long tree = 0, hasSuper = 1, count = 1;
	if (!PyArg_ParseTuple( args, "|lll", &tree, &hasSuper, &count )) return NULL;
	PyModel * model = PyObject_New( PyModel, type );
	model->tree_.id = tree;
	model->superModel_.count = int( count );
	model->superModel_.top.tree = tree ? &model->tree_ : NULL;
	model->super_ = hasSuper ? &model->superModel_ : NULL;
	return model;
}
static void freeModel( PyObject * self ) { PyObject_Del( self ); }
PyTypeObject PyModel::s_type_ = { PyVarObject_HEAD_INIT( NULL, 0 ) "PyModel", sizeof( PyModel ) };

static std::vector< PyObjectPtr > owners[2];
static std::vector< const BSPTree * > trees[2];

static PyObject * setModels( PyObject *, PyObject * args )
{
	int camera;
	PyObject * models;
	if (!PyArg_ParseTuple( args, "iO", &camera, &models )) return NULL;
	// The PyObjectPtr argument converter maps None to NULL (pyscript/script.cpp).
	PyObjectPtr argument( models == Py_None ? NULL : models );
	if (!setStockCursorCameraModels( argument.get(), owners[camera], trees[camera] )) return NULL;
	Py_RETURN_NONE;
}
static PyObject * lists( PyObject *, PyObject * args )
{
	int camera;
	if (!PyArg_ParseTuple( args, "i", &camera )) return NULL;
	PyObject * models = PyList_New( 0 );
	PyObject * ids = PyList_New( 0 );
	for (const PyObjectPtr & owner : owners[camera]) PyList_Append( models, owner.get() );
	for (const BSPTree * tree : trees[camera])
	{
		PyObject * id = PyInt_FromLong( tree->id );
		PyList_Append( ids, id );
		Py_DECREF( id );
	}
	return Py_BuildValue( "(NN)", models, ids );
}
static PyMethodDef methods[] = { { "set_models", setModels, METH_VARARGS, NULL },
	{ "lists", lists, METH_VARARGS, NULL }, { NULL, NULL, 0, NULL } };
extern "C" void initmodel_list_probe()
{
	PyModel::s_type_.tp_flags = Py_TPFLAGS_DEFAULT;
	PyModel::s_type_.tp_new = newModel;
	PyModel::s_type_.tp_dealloc = freeModel;
	if (PyType_Ready( &PyModel::s_type_ ) < 0) return;
	PyObject * module = Py_InitModule( "model_list_probe", methods );
	Py_INCREF( &PyModel::s_type_ );
	PyModule_AddObject( module, "PyModel", (PyObject *)&PyModel::s_type_ );
}
'''

MODEL_LIST_DRIVER = r'''
import json, sys
sys.path.insert(0, sys.argv[1])
import model_list_probe as p
trees = json.loads(sys.argv[2])
steps = json.loads(sys.argv[3])
models = dict((name, p.PyModel(tree)) for name, tree in trees.items())
models['x'] = object()
names = dict((id(v), k) for k, v in models.items())
class Subclass(list):
    pass
baseline = dict((k, sys.getrefcount(v)) for k, v in models.items() if k != 'x')
result = []
for kind, items in steps:
    argument = None if items is None else [models[n] for n in items]
    if kind == 'not_list':
        argument = tuple(argument)
    elif kind == 'list_subclass':
        argument = Subclass(argument)
    error = []
    try:
        p.set_models(0, argument)
    except RuntimeError as exception:
        error = [[True, str(exception)]]
    del argument
    owners, ids = p.lists(0)
    # A Python 2 list comprehension would leak its variable and keep a reference.
    state = [list(map(lambda owner: names[id(owner)], owners)), ids]
    del owners
    result.append([error, state, dict((k, sys.getrefcount(v) - baseline[k]) for k, v in models.items() if k != 'x')])
# Contained original crashes: no super model, or a super model without models.
contained = [p.PyModel(7, 0), p.PyModel(8, 1, 0)]
p.set_models(1, contained)
print(json.dumps({'steps': result, 'contained': p.lists(1)[1]}))
'''


class CursorCameraModelListTests(unittest.TestCase):
    def test_production_model_list_matches_original_instructions(self):
        requirements(self)
        if sys.platform != 'darwin' or not PY27.exists():
            self.skipTest('Pinned Python 2.7 and the Darwin extension linker required')
        import json
        script = r'''
import json, sys
sys.path.insert(0, sys.argv[1])
import stock_cursor_camera_oracle as o
import test_stock_cursor_camera as t
scene = o.ModelListScene(o.OriginalCursorCamera(sys.argv[2]))
e = scene.emulator
models = dict((name, scene.model(1000 + tree if tree else 0)) for name, tree in t.MODEL_TREES.items())
models['x'] = scene.other()
names = dict((v, k) for k, v in models.items())
result = []
for kind, items in t.MODEL_STEPS:
    if items is None:
        argument = 0
    else:
        argument = scene.sequence([models[n] for n in items], is_list=kind != 'not_list')
    errors = scene.set_models(argument)
    owners, trees = scene.lists()
    state = [[names[o] for o in owners], [tree - 1000 for tree in trees]]
    result.append([[list(error) for error in errors], state,
                   dict((k, e.word(v) - 100) for k, v in models.items() if k != 'x')])
print(json.dumps(result))
'''
        original = json.loads(run_oracle(script))
        source = (ROOT / 'bigworld/stock_cursor_camera.cpp').read_text()
        production = '\n'.join(source[source.index(name):source.index(';', source.index(name)) + 1]
                               for name in ('const char * WRONG_MODELS =', 'const char * WRONG_MODEL ='))
        production += '\n' + function(source, 'bool setStockCursorCameraModels( PyObject * models,')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'probe.cpp').write_text(MODEL_LIST_HOST.replace('@PRODUCTION@', production))
            build = subprocess.run(['c++', '-std=c++14', '-Wno-deprecated-register', '-Wno-missing-field-initializers',
                                    '-fPIC', '-shared', '-undefined', 'dynamic_lookup',
                                    '-I' + str(PY27.parents[1] / 'include/python2.7'), str(root / 'probe.cpp'),
                                    '-o', str(root / 'model_list_probe.so')], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(PY27), '-c', MODEL_LIST_DRIVER, str(root), json.dumps(MODEL_TREES),
                                  json.dumps(MODEL_STEPS)], capture_output=True, text=True,
                                 env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            production_result = json.loads(run.stdout)
        # #1513 leaves its RuntimeError pending and returns None; production raises it.
        self.assertEqual(production_result['steps'], original)
        self.assertEqual(production_result['contained'], [])
        self.assertEqual([error for error, unused, unused in original],
                         [[[True, 'CursorCamera::wg_addModelsToCollideWith - wrong parameter type. '
                                  'Must be list of PyModel.']]] * 2 + [[]] +
                         [[[True, 'CursorCamera::wg_addModelsToCollideWith - wrong parameter type. '
                                  'Must be list of PyModel.']]] + [[]] +
                         [[[True, 'CursorCamera::wg_addModelsToCollideWith - wrong item type in the list. '
                                  'Must be PyModel instance.']]] + [[], []])


HIDING_HOST = r'''
#include <cstdio>
#include <utility>
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
struct MatrixProvider { int references = 1; int label = 0; };
// BigWorld SmartPointer<MatrixProvider>: a strong reference.
class MatrixProviderPtr
{
public:
	MatrixProviderPtr( MatrixProvider * p = NULL ) : p_( p ) { if (p_) ++p_->references; }
	MatrixProviderPtr( const MatrixProviderPtr & o ) : p_( o.p_ ) { if (p_) ++p_->references; }
	MatrixProviderPtr & operator=( const MatrixProviderPtr & o )
	{ MatrixProvider * old = p_; p_ = o.p_; if (p_) ++p_->references; if (old) --old->references; return *this; }
	~MatrixProviderPtr() { if (p_) --p_->references; }
	MatrixProvider * get() const { return p_; }
private:
	MatrixProvider * p_;
};
namespace wot { struct StockTreeHidingFrame {}; }
@STATE@
@PRODUCTION@
int main()
{
	MatrixProvider first, second;
	first.label = 1;
	second.label = 2;
	MatrixProvider * targets[3] = { &first, &second, NULL };
	int target, enabled;
	while (std::scanf("%d %d", &target, &enabled) == 2)
	{
		if (target < 0) finiStockCursorCameraHiding();
		else publishStockCursorCameraHiding( targets[target], enabled != 0 );
		MatrixProvider * tree = treeCameraTarget().target.get();
		std::printf("%d %d %d\n", tree ? tree->label : 0, first.references, second.references);
	}
	return 0;
}
'''


class CursorCameraExtensionDispatchTests(unittest.TestCase):
    def test_original_wrappers_destructor_and_renderer_slots(self):
        requirements(self)
        import json
        script = r'''
import json, sys
sys.path.insert(0, sys.argv[1])
import stock_cursor_camera_oracle as o
scene = o.ExtensionScene(o.OriginalCursorCamera(sys.argv[2]))
e = scene.emulator
camera = scene.camera
item = scene.provider() + 4
out = {}
out['apply'] = scene.call_wrapper(o.APPLY_PARAMS)
out['apply_extra'] = scene.call_wrapper(o.APPLY_PARAMS, item)
out['models'] = scene.call_wrapper(o.SET_MODELS_WRAPPER, item)
out['models_none'] = scene.call_wrapper(o.SET_MODELS_WRAPPER, o.PY_NONE)
out['models_missing'] = scene.call_wrapper(o.SET_MODELS_WRAPPER)
out['set_true'] = scene.set_hiding(0, 1)
out['set_false'] = scene.set_hiding(0, 0)
out['set_error'] = scene.set_hiding(-1, 1)
out['get'] = scene.get_hiding(1)
providers = [scene.provider() for unused in range(3)]
for name, provider in zip(('target', 'source', 'inaccuracy'), providers):
    e.put_word(camera + o.OFFSET[name], provider)
out['destruct'] = scene.destruct()
out['released'] = [e.word(p + 4) for p in providers]
first, second = scene.provider(), scene.provider()
labels = {0: 0, first: 1, second: 2}
steps = []
for deferred, target, enabled, owner in ((1, first, 1, 1), (1, first, 0, 1), (1, second, 1, 1), (0, first, 1, 1),
                                         (0, second, 0, 1), (1, first, 1, 0), (1, 0, 1, 1)):
    tree, field, focus = scene.renderer_slot(deferred, target, enabled, owner)
    steps.append([deferred, labels[target], enabled, owner, labels[tree], labels[field],
                  [labels[f] for f in focus]])
out['renderer'] = steps
out['camera'] = camera
out['item'] = item
out['none'] = o.PY_NONE
print(json.dumps(out))
'''
        out = json.loads(run_oracle(script))
        camera, item, none = out['camera'], out['item'], out['none']
        # 0079b8f0 dispatches update slot +8 with one second and returns None.
        self.assertEqual(out['apply'], [none, 1, [['update', camera, 1.0]]])
        self.assertEqual(out['apply_extra'], [0, 0, [['arg_count', 'wg_applyParams', 0, 0]]])
        # 0079b950 converts one PyObjectPtr (None becomes NULL) and returns None.
        self.assertEqual(out['models'], [none, 1, [['set_models', item]]])
        self.assertEqual(out['models_none'], [none, 1, [['set_models', 0]]])
        self.assertEqual(out['models_missing'], [0, 0, [['arg_count', 'wg_setModelsToCollideWith', 1, 1]]])
        # 0079df40 stores a converted bool; false clears the renderer targets.
        self.assertEqual(out['set_true'], [0, 1, [['set_bool', 0x7777]]])
        self.assertEqual(out['set_false'], [0, 0, [['set_bool', 0x7777], ['hiding', 0, 1]]])
        self.assertEqual(out['set_error'], [0xffffffff, 0, [['set_bool', 0x7777]]])
        self.assertEqual(out['get'], [0x4321, [['get_bool', 1]]])
        # 0079b640 clears the renderer targets before releasing its owners.
        self.assertEqual(out['destruct'], [['hiding', 0, 1], ['trees'], ['models'], ['base']])
        self.assertEqual(out['released'], [99, 99, 99])
        # Both slots store enabled ? target : NULL into 1d59f74 and the SRT
        # owner field; only the deferred slot writes the raw focus target.
        renderer = [step[4:6] + [step[6]] for step in out['renderer']]
        self.assertEqual(renderer, [[1, 1, [1, 1]], [0, 0, [1, 1]], [2, 2, [2, 2]], [1, 1, [2, 2]],
                                    [0, 0, [2, 2]], [1, 0, [1, 1]], [0, 0, [0, 0]]])

        # Production publishes exactly the 1d59f74 outcome to the old-ST4 owner.
        scene = (ROOT / 'bigworld/stock_tree_scene.cpp').read_text()
        state = scene[scene.index('struct TreeCameraTarget'):]
        state = state[:state.index('\n}\n', state.index('TreeCameraTarget & treeCameraTarget()')) + 3]
        source = (ROOT / 'bigworld/stock_cursor_camera.cpp').read_text()
        production = function(scene, 'void setStockTreeCameraTarget( MatrixProvider * target )') + '\n' + \
            '\n'.join(function(source, signature) for signature in (
                'void publishStockCursorCameraHiding( MatrixProvider * target, bool enabled )',
                'void finiStockCursorCameraHiding()'))
        with tempfile.TemporaryDirectory() as folder:
            binary = compile_host(Path(folder), HIDING_HOST.replace('@STATE@', state)
                                  .replace('@PRODUCTION@', production), 'hiding_host')
            steps = [step[1:3] for step in out['renderer']] + [(-1, 0), (-1, 0)]
            labels = {0: 3, 1: 1, 2: 2}
            result = subprocess.run([str(binary)], input=''.join('%d %d\n' % (-1 if t < 0 else labels[t] - 1, e)
                                                                 for t, e in steps),
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            rows = [list(map(int, line.split())) for line in result.stdout.splitlines()]
            self.assertEqual([row[0] for row in rows[:7]], [step[4] for step in out['renderer']])
            self.assertEqual(rows, [[1, 2, 1], [0, 1, 1], [2, 1, 2], [1, 2, 1], [0, 1, 1], [1, 2, 1], [0, 1, 1],
                                    [0, 1, 1], [0, 1, 1]])


PYC_AUDIT = r'''
import dis, json, marshal, sys, types, zipfile
archive = zipfile.ZipFile(sys.argv[1] + '/res/packages/scripts.pkg')
NAMES = ('wg_applyParams', 'wg_setModelsToCollideWith', 'sptHiding')

def codes(code, path):
    yield path + '/' + code.co_name, code
    for const in code.co_consts:
        if isinstance(const, types.CodeType):
            for item in codes(const, path + '/' + code.co_name):
                yield item

def instructions(code):
    raw, index, result = code.co_code, 0, []
    while index < len(raw):
        op = ord(raw[index])
        if op >= dis.HAVE_ARGUMENT:
            result.append((dis.opname[op], ord(raw[index + 1]) | ord(raw[index + 2]) << 8))
            index += 3
        else:
            result.append((dis.opname[op], None))
            index += 1
    return result

calls, cameras, other = [], {}, []
for name in archive.namelist():
    if not name.endswith('.pyc'):
        continue
    data = archive.read(name)
    if not any(n in data for n in NAMES + ('CursorCamera',)):
        continue
    for path, code in codes(marshal.loads(data[8:]), name):
        ops = instructions(code)
        for i, (op, arg) in enumerate(ops):
            if op in ('LOAD_ATTR', 'STORE_ATTR', 'LOAD_GLOBAL', 'LOAD_NAME') and code.co_names[arg] in NAMES:
                if code.co_names[arg] == 'wg_applyParams' and op == 'LOAD_ATTR':
                    receiver = code.co_names[ops[i - 1][1]] if ops[i - 1][0] == 'LOAD_ATTR' else ops[i - 1][0]
                    calls.append([path, receiver, ops[i + 1][0], ops[i + 1][1]])
                else:
                    other.append([path, op, code.co_names[arg]])
            # self.<attr> = BigWorld.CursorCamera()
            if (op == 'LOAD_ATTR' and code.co_names[arg] == 'CursorCamera' and ops[i + 1] == ('CALL_FUNCTION', 0)
                    and ops[i + 2][0] == 'LOAD_FAST' and ops[i + 3][0] == 'STORE_ATTR'):
                cameras.setdefault(name, []).append(code.co_names[ops[i + 3][1]])
print(json.dumps({'calls': sorted(calls), 'cameras': cameras, 'other': other}))
'''


class CursorCameraOriginalConsumerTests(unittest.TestCase):
    def test_original_python_consumers(self):
        if not PY27.exists() or not (CLIENT / 'res/packages/scripts.pkg').exists():
            self.skipTest('Pinned Python 2.7 and the exact client scripts required')
        import json
        result = subprocess.run([str(PY27), '-c', PYC_AUDIT, str(CLIENT)], capture_output=True, text=True,
                                env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        self.assertEqual(result.returncode, 0, result.stderr)
        audit = json.loads(result.stdout)
        # Every #1513 call takes no arguments, on an attribute created by BigWorld.CursorCamera().
        self.assertEqual(audit['calls'], [
            ['scripts/client/AvatarInputHandler/DynamicCameras/ArtyCamera.pyc/<module>/ArtyCamera/enable',
             '_ArtyCamera__cam', 'CALL_FUNCTION', 0],
            ['scripts/client/AvatarInputHandler/DynamicCameras/StrategicCamera.pyc/<module>/StrategicCamera/enable',
             '_StrategicCamera__cam', 'CALL_FUNCTION', 0],
            ['scripts/client/AvatarInputHandler/StrategicCamerasInterpolator.pyc/<module>/'
             'StrategicCamerasInterpolator/__setupCamera', '_StrategicCamerasInterpolator__cam', 'CALL_FUNCTION', 0],
            ['scripts/client/gui/ClientHangarSpace.pyc/<module>/ClientHangarSpace/__setupCamera',
             '_ClientHangarSpace__cam', 'CALL_FUNCTION', 0],
            ['scripts/client/new_year/customization_camera.pyc/<module>/CustomizationCamera/activate',
             '_CustomizationCamera__cam', 'CALL_FUNCTION', 0],
            ['scripts/client/new_year/customization_camera.pyc/<module>/CustomizationCamera/init',
             '_CustomizationCamera__cam', 'CALL_FUNCTION', 0]])
        for path, receiver, unused, unused in audit['calls']:
            self.assertIn(receiver, audit['cameras'][path.split('.pyc/')[0] + '.pyc'])
        # No #1513 script reads or writes the model list or tree hiding.
        self.assertEqual(audit['other'], [])


if __name__ == '__main__':
    unittest.main()
