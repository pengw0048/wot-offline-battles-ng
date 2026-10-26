"""Isolated real-client personality for compound properties and attachments.

The source is the pinned T1 Cunningham chassis, not generated geometry. Original
model_assembler.prepareCompoundAssembler uses CompoundAssembler.addRootPart;
loadResourceListBG publishes the resulting compound through ResourceRefs. This
probe exercises native component contracts, not visible gameplay acceptance.
"""
from __future__ import print_function
import json
import math
import os
import sys
import time
import traceback

MODEL = 'vehicles/american/A01_T1_Cunningham/normal/lod0/Chassis.model'
ASSEMBLER_NAME = 'native-compound-properties-diagnostic'
OUTPUT = os.path.join(os.getcwd(), 'compound-properties-result.json')
REPORT = {'format': 1, 'status': 'running', 'python': sys.version,
          'executable': sys.executable, 'resource': MODEL, 'checks': [],
          'lifecycle': [], 'original_game_init_called': False,
          'render_acceptance': False}
STATE = {'phase': 'not-initialized', 'callbacks': 0}


def save():
    temporary = OUTPUT + '.tmp'
    with open(temporary, 'wb') as stream:
        stream.write(json.dumps(REPORT, indent=2))
    if os.path.exists(OUTPUT):
        os.remove(OUTPUT)
    os.rename(temporary, OUTPUT)


def matrix_values(provider):
    import Math
    matrix = Math.Matrix(provider)
    return tuple(matrix.get(row, column) for row in range(4) for column in range(4))


def close(first, second):
    assert len(first) == len(second)
    assert all(abs(a - b) <= 0.00001 * max(1, abs(a), abs(b))
               for a, b in zip(first, second)), (first, second)


def invalid(call, expected=TypeError):
    try:
        call()
    except expected:
        return
    raise AssertionError('Invalid native input was accepted')


def cleanup():
    import BigWorld
    failures = []
    compound = STATE.get('compound')
    operations = []
    if STATE.get('global_added'):
        operations.append(('delModel', lambda: BigWorld.delModel(compound)))
    if STATE.get('sticker') is not None:
        operations.append(('sticker.clear', STATE['sticker'].clear))
    if compound is not None:
        operations.append(('compound.reset', compound.reset))
    if STATE.get('space') is not None:
        operations.append(('clearLocalSpace', lambda: BigWorld.clearLocalSpace(STATE['space'])))
    for name, operation in operations:
        try:
            operation()
            REPORT['lifecycle'].append(name)
        except Exception:
            failures.append(name + ': ' + traceback.format_exc())
    STATE.clear()
    return failures


def finish(error=None):
    import BigWorld
    if STATE.get('finished'):
        return
    STATE['finished'] = True
    REPORT['callbacks'] = STATE.get('callbacks', 0)
    failures = cleanup()
    REPORT['status'] = 'failed' if error or failures else 'passed'
    if error:
        REPORT['error'] = error
    if failures:
        REPORT['cleanup_errors'] = failures
    save()
    print('COMPOUND_PROPERTIES_RUNTIME_' + REPORT['status'].upper())
    BigWorld.quit()


def loaded(resources):
    try:
        import BigWorld
        import Math
        REPORT['failed_resource_ids'] = list(resources.failedIDs)
        REPORT['resource_keys'] = list(resources.keys())
        assert not resources.failedIDs, ('Real chassis compound load failed', REPORT['failed_resource_ids'])
        assert resources.has_key(ASSEMBLER_NAME)
        compound = resources[ASSEMBLER_NAME]
        STATE.update(resources=resources, compound=compound)
        REPORT['compound_type'] = type(compound).__name__
        root = compound.root
        node = compound.node('diagnostic_node')
        assert root is compound.node('') and root is compound.root
        assert node is compound.node('diagnostic_alias')
        assert not root.isDangling and not node.isDangling
        assert not compound.isInWorld
        assert compound.visible and not compound.stipple and not compound.skipColorPass
        assert not compound.isHighPriorityReflection
        bounds = compound.getBoundsForRoot()
        baseline = matrix_values(bounds)
        assert baseline == matrix_values(compound.getBoundsForPart(0))
        assert all(not math.isnan(v) and not math.isinf(v) for v in baseline)
        assert all(baseline[i] > 0 for i in (0, 5, 10)), baseline
        REPORT['real_root_bounds'] = baseline
        invalid(lambda: compound.getBoundsForPart(-1), ValueError)
        invalid(lambda: compound.getBoundsForPart(1), ValueError)
        invalid(lambda: setattr(compound, 'isInWorld', True))
        invalid(lambda: setattr(node, 'position', (0, 0, 0)))
        assert compound.getSound('diagnostic/no-event') is None
        assert compound.playSound('diagnostic/no-event') is None
        invalid(lambda: compound.getSound(object()))
        invalid(lambda: compound.playSound())
        REPORT['checks'].append('real asynchronous model product, cached node aliases, authored bounds and native conversion guards')

        compound.visible = False
        assert compound.visible is False
        compound.visible = True
        assert compound.visible is True
        for name in ('stipple', 'skipColorPass', 'isHighPriorityReflection'):
            setattr(compound, name, True)
            assert getattr(compound, name) is True
            setattr(compound, name, False)
            assert getattr(compound, name) is False
        old_source = compound.matrix
        old_source.setScale((2, 3, 4))
        old_source.translation = (50, 60, 70)
        compound.position = (11, 22, 33)
        assert compound.matrix is not old_source
        identity = list(matrix_values(Math.Matrix()))
        identity[12:15] = [11, 22, 33]
        assert matrix_values(compound.matrix) == tuple(identity)
        node_source = Math.Matrix()
        node_source.translation = (3, 4, 5)
        node.local = node_source
        assert node.local is node_source
        sticker = BigWorld.WGStickerModel()
        STATE['sticker'] = sticker
        sticker.setLODDistance(100.0)
        link = compound.getPartGeometryLink(0)
        STATE['geometry_link'] = link
        sticker.setupSuperModel(link, Math.Matrix())
        node.attach(sticker)
        assert compound.containsAttachment(sticker)
        invalid(lambda: root.attach(sticker))
        root.detach(sticker)
        assert not compound.containsAttachment(sticker)
        root.detach(sticker)
        sticker.clear()
        sticker.setupSuperModel(link, Math.Matrix())
        node.attach(sticker, Math.Matrix(), True)
        assert compound.containsAttachment(sticker)
        STATE.update(root=root, node=node, node_source=node_source, bounds=bounds,
                     baseline=baseline, phase='await-pose')
        BigWorld.addModel(compound, STATE['space'])
        STATE['global_added'] = True
        assert compound.isInWorld
        REPORT['checks'].append('real sticker geometry link, attach/detach/rebind, duplicate ownership guard and local-space entry')
        save()
    except Exception:
        STATE['failure'] = traceback.format_exc()


def init(*args):
    REPORT['lifecycle'].append('init')
    try:
        import BigWorld
        import Math
        import ResMgr
        assert ResMgr.openSection(MODEL) is not None, 'Pinned chassis model is absent from the prepared resource path'
        BigWorld.wg_initCustomSettings()
        REPORT['checks'].append('real wg_initCustomSettings acquired original compound stipple resources')
        STATE['space'] = BigWorld.createLocalSpace()
        assembler = BigWorld.CompoundAssembler()
        assembler.assemblerName = ASSEMBLER_NAME
        assembler.spaceID = STATE['space']
        assembler.addRootPart(MODEL, 'chassis')
        local = Math.Matrix()
        local.translation = (2, 3, 4)
        assembler.addNode('diagnostic_node', 'chassis', local)
        assembler.addNodeAlias('diagnostic_node', 'diagnostic_alias')
        STATE.update(assembler=assembler, phase='loading')
        BigWorld.loadResourceListBG((assembler,), loaded)
        save()
    except Exception:
        STATE['failure'] = traceback.format_exc()


def check_tick():
    import BigWorld
    import Math
    try:
        STATE['callbacks'] += 1
        if STATE.get('failure'):
            finish(STATE['failure'])
            return
        assert time.time() < STATE['deadline'], ('Native compound transition timed out', STATE['phase'])
        phase = STATE['phase']
        if phase == 'await-pose':
            compound, node = STATE['compound'], STATE['node']
            if tuple(compound.position) == (11, 22, 33) and tuple(node.position) == (14, 26, 38):
                current = matrix_values(STATE['bounds'])
                expected = list(STATE['baseline'])
                expected[12:15] = [expected[12] + 11, expected[13] + 22, expected[14] + 33]
                close(current, expected)
                STATE['node_snapshot'] = matrix_values(node.localMatrix)
                node.local = None
                STATE['node_source'].translation = (100, 200, 300)
                STATE['phase'] = 'source-cleared'
        elif phase == 'source-cleared':
            compound, node = STATE['compound'], STATE['node']
            assert node.local is None
            assert matrix_values(node.localMatrix) == STATE['node_snapshot']
            close(tuple(node.position), (14, 26, 38))
            source = Math.Matrix()
            source.translation = (-3, -4, -5)
            node.local = source
            assert node.local is source
            compound.stipple = True
            compound.skipColorPass = False
            compound.isHighPriorityReflection = True
            STATE['phase'] = 'source-replaced'
        elif phase == 'source-replaced':
            compound, node = STATE['compound'], STATE['node']
            if tuple(node.position) == (8, 18, 28):
                assert compound.stipple and compound.isHighPriorityReflection
                compound.visible = False
                assert not compound.visible
                STATE['phase'] = 'hidden'
        elif phase == 'hidden':
            compound = STATE['compound']
            assert not compound.visible and compound.isInWorld
            compound.visible = True
            compound.skipColorPass = True
            assert compound.skipColorPass
            STATE['phase'] = 'skip-color'
        elif phase == 'skip-color':
            compound, node = STATE['compound'], STATE['node']
            assert compound.visible and compound.skipColorPass
            compound.skipColorPass = False
            retained_bounds = matrix_values(STATE['bounds'])
            BigWorld.delModel(compound)
            STATE['global_added'] = False
            assert not compound.isInWorld
            compound.reset()
            compound.reset()
            assert not compound.isInWorld and not compound.visible
            assert not compound.isHighPriorityReflection
            assert STATE['root'].isDangling and node.isDangling
            assert STATE['root'].local is None and node.local is None
            assert tuple(compound.position) == (0, 0, 0)
            assert tuple(node.position) == (0, 0, 0)
            assert matrix_values(node) == matrix_values(Math.Matrix())
            assert matrix_values(node.localMatrix) == matrix_values(Math.Matrix())
            assert matrix_values(STATE['bounds']) == retained_bounds
            assert not compound.containsAttachment(STATE['sticker'])
            invalid(lambda: compound.getBoundsForRoot(), ValueError)
            node.detach(STATE['sticker'])
            STATE['sticker'].clear()
            REPORT['checks'].append('real native frame updates, live root/node sources, nullable source retention and bounds movement')
            REPORT['checks'].append('live visibility/stipple/skip-color/priority properties and idempotent reset with retained native adapters')
            finish()
            return
        BigWorld.callback(0.0, check_tick)
    except Exception:
        finish(traceback.format_exc())


def start():
    import BigWorld
    REPORT['lifecycle'].append('start')
    STATE['deadline'] = time.time() + 60
    BigWorld.callback(0.0, check_tick)


def onInit(isReload):
    REPORT['lifecycle'].append('onInit')


def onChangeEnvironments(environment):
    REPORT.setdefault('environment_changes', []).append(environment)
    save()


def fini():
    REPORT['lifecycle'].append('fini')
    save()


def onFini():
    REPORT['lifecycle'].append('onFini')
    save()
