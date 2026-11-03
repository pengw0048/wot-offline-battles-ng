"""Exercise the shipped AS layout arithmetic with the stock scaled-stage contract."""
import hashlib
import json
import math
from pathlib import Path
import re
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]
OVERLAY = ROOT / 'bigworld/offline_entry'


class Point:
    def __init__(self, x=0, y=0):
        self.x, self.y = x, y


class Node:
    def __init__(self, parent=None, x=0, y=0, scale=1, width=0, height=0):
        self.parent, self.x, self.y = parent, x, y
        self.scale, self.width, self.height = scale, width, height

    def localToGlobal(self, point):
        result = Point(self.x + self.scale * point.x, self.y + self.scale * point.y)
        return self.parent.localToGlobal(result) if self.parent else result

    def globalToLocal(self, point):
        point = self.parent.globalToLocal(point) if self.parent else point
        return Point((point.x - self.x) / self.scale, (point.y - self.y) / self.scale)

    def getBounds(self, target):
        first = target.globalToLocal(self.localToGlobal(Point()))
        last = target.globalToLocal(self.localToGlobal(Point(self.width, self.height)))
        return SimpleNamespace(x=first.x, y=first.y, width=last.x-first.x,
                               height=last.y-first.y, bottom=last.y, top=first.y)


def layout_program(source):
    # Execute the production assignments, rather than a Python copy of the
    # positioning formula. Only AS type declarations and expression syntax
    # differ here; the matrix operations are exercised by the nodes above.
    body = source.split('private function placeRoom():void {', 1)[1]
    body = body[body.index('var top:Number'):body.index(';', body.index('window.y =')) + 1]
    body = re.sub(r'//[^\n]*', '', body)
    body = re.sub(r'var (\w+):\w+\s*=', r'\1 =', body)
    body = re.sub(r'new (\w+)\(', r'\1(', body).replace('null', 'None')
    statements = body.replace(';', ';\n').splitlines()
    code, indent = [], 0
    pending = ''
    for line in statements:
        line = line.strip()
        if not line:
            continue
        if line == '}':
            indent -= 1
            continue
        if line.startswith('if (') and line.endswith('{'):
            code.append('    ' * indent + line[:-1].rstrip() + ':')
            indent += 1
            continue
        pending += ' ' + line
        if not line.endswith(';'):
            continue
        line, pending = pending.strip()[:-1], ''
        if '?' in line:
            lhs, expression = line.split('=', 1)
            condition, alternatives = expression.split('?', 1)
            yes, no = alternatives.split(':', 1)
            line = '%s = (%s if %s else %s)' % (lhs.strip(), yes.strip(), condition.strip(), no.strip())
        code.append('    ' * indent + line)
    if pending or indent:
        raise AssertionError('The room layout syntax changed')
    return compile('\n'.join(code), 'OfflineWaitingRoom.as::placeRoom', 'exec')


class WaitingRoomLayoutTests(unittest.TestCase):
    def test_scaled_stage_and_transformed_parent_keep_frame_centered(self):
        program = layout_program((OVERLAY / 'OfflineWaitingRoom.as').read_text())
        for width, height, scale in ((1920,1080,1), (2560,1440,1.25), (3840,2160,2)):
            for daily in (False, True):
                for with_queue in (False, True):
                    with self.subTest(size=(width,height), scale=scale, daily=daily, queue=with_queue):
                        stage = Node(scale=scale)
                        stage.stageWidth, stage.stageHeight = width, height
                        app = SimpleNamespace(appWidth=int(width/scale), appHeight=int(height/scale))
                        parent = Node(stage, 31, 47, 1.1)
                        window = Node(parent, 150, 120)
                        frame = Node(window, -12, -33, width=685, height=352)
                        window.getBackground = lambda: frame
                        queue = SimpleNamespace(
                            modeTitle=Node(stage, 700, 310, width=240, height=45),
                            exitButton=Node(stage, 900, 800, width=100, height=30)) if with_queue else None
                        values = dict(App=app, stage=stage, daily=daily, window=window, queue=queue,
                                      Point=Point, DisplayObject=lambda x:x,
                                      Math=SimpleNamespace(round=lambda x:math.floor(x+.5)))
                        for unused in range(8):
                            exec(program, values)
                            bounds = frame.getBounds(stage)
                            self.assertAlmostEqual(bounds.x+bounds.width/2, app.appWidth/2, delta=.56)
                            if queue:
                                top, bottom = 355, 800
                            else:
                                top = 0 if daily else app.appHeight*.24
                                bottom = app.appHeight if daily else app.appHeight*.80
                            self.assertAlmostEqual(bounds.y+bounds.height/2, (top+bottom)/2, delta=.56)

    def test_room_artifact_matches_owned_source_and_configure_dependencies(self):
        proof = json.loads((OVERLAY / 'offlineWaitingRoom.swf.json').read_text())
        for key, name in (('actionscript_sha256','OfflineWaitingRoom.as'),
                          ('output_sha256','offlineWaitingRoom.swf')):
            self.assertEqual(proof[key], hashlib.sha256((OVERLAY / name).read_bytes()).hexdigest())
        cmake = (ROOT / 'bigworld/offline_fork.cmake').read_text()
        for suffix in ('*.as', '*.swf', '*.swf.json'):
            self.assertIn('/offline_entry/' + suffix, cmake)


if __name__ == '__main__':
    unittest.main()
