"""Compare native cursor/history laws with the pinned gameplay implementation."""
import ast
import importlib.util
import math
import os
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCE_ROOT=os.environ.get('WOT_FORK_0101_SOURCE')
SOURCE=Path(SOURCE_ROOT)/'src/res/scripts/client/gui/mods/offline_lan_0922' if SOURCE_ROOT else None

def load(name):
    spec=importlib.util.spec_from_file_location(name,SOURCE/(name+'.py'))
    module=importlib.util.module_from_spec(spec);sys.modules[name]=module;spec.loader.exec_module(module);return module

def number(value):
    return repr(float(value))

def point(values):
    return '{'+','.join(map(number,values))+'}'

@unittest.skipUnless(SOURCE_ROOT, 'fixed gameplay source is required')
class NativeProjectileCursor(unittest.TestCase):
    def test_cursor_and_history(self):
        compiler=shutil.which('c++')
        if not compiler:self.skipTest('C++ compiler unavailable')
        load('projectile_runtime');manager=load('projectile_manager')
        tree=ast.parse((SOURCE/'battle_runtime.py').read_text())
        owner=next(node for node in tree.body if isinstance(node,ast.ClassDef) and node.name=='BattleRuntime')
        names={'_projectile_historic_pose_uncached','_projectile_pose_interval_travel','_projectile_pose_sweep_fractions'}
        methods=[]
        for node in owner.body:
            if isinstance(node,ast.FunctionDef) and node.name in names:
                node.decorator_list=[x for x in node.decorator_list if isinstance(x,ast.Name) and x.id=='staticmethod'];methods.append(node)
        cls=ast.ClassDef(name='Oracle',bases=[],keywords=[],body=methods,decorator_list=[])
        ns=dict(math=math,PROJECTILE_POSE_MAX_SWEEP_STEPS=16,PROJECTILE_POSE_MAX_ANGLE_STEP=math.pi/180.,_angle_delta=lambda a,b:(b-a+math.pi)%(2*math.pi)-math.pi)
        exec(compile(ast.fix_missing_locations(ast.Module(body=[cls],type_ignores=[])),'pinned_history','exec'),ns)
        oracle=ns['Oracle']();oracle._projectile_historic_pose=oracle._projectile_historic_pose_uncached
        rng=random.Random(144619)
        generated=['#include "stock_native_projectile_cursor.hpp"','#include "stock_native_projectile_history.hpp"','#include <cassert>','#include <cmath>','using namespace stock_native_projectile;','struct S:CursorSink{std::optional<Terminal> chord(const Cursor&,const Chord&)override{return {};} void terminal(const Cursor&,const Terminal&)override{}};','void near(double a,double b){assert(std::abs(a-b)<=1e-8*(1+std::abs(b)));}','int main(){']
        for index in range(120):
            origin=[rng.uniform(-100,100) for _ in range(3)];velocity=[rng.uniform(-1000,1000) for _ in range(3)];gravity=[0,-rng.uniform(0,2000),0]
            lifetime=rng.uniform(.1,20);distance=rng.uniform(1,20000);launch=rng.uniform(0,5);now=launch+rng.uniform(0,min(2,lifetime*.9));budget=rng.randrange(0,40)
            original=manager.InFlightProjectiles(initial_time=launch)
            self.assertTrue(original.launch('x',origin,velocity,gravity,launch,lifetime,distance))
            original.advance(now,lambda *args:None,lambda *args:None,maximum_chords=budget)
            generated+=['{Cursors m(128,%s);Cursor s;s.key={"x",0};s.origin=%s;s.velocity=%s;s.gravity=%s;s.launch_time=%s;s.max_time=%s;s.max_distance=%s;assert(m.launch(s));S sink;assert(m.advance(%s,sink,%d));'%(number(launch),point(origin),point(velocity),point(gravity),number(launch),number(lifetime),number(distance),number(now),budget)]
            expected=original.get('x')
            if expected:
                generated+=['auto v=m.get({"x",0});assert(v);near(v->cursor_time,%s);near(v->distance,%s);'%(number(expected['cursor_time']),number(expected['distance']))]
                generated+=['near(v->position[%d],%s);'%(i,number(expected['position'][i])) for i in range(3)]
            else:generated+=['assert(!m.get({"x",0}));']
            metrics=original.last_advance_metrics();generated+=['assert(m.metrics().chords==%d);assert(m.metrics().terminals==%d);near(m.metrics().debt_after,%s);}'%(metrics['chords'],metrics['terminals'],number(metrics['debt_after']))]
        for index in range(80):
            history=[];generated+=['{History h;']
            for at in (0,.1,.4,1.):
                pose={name:rng.uniform(-3,3) for name in ('x','y','z','yaw','pitch','roll','turret_yaw','gun_pitch')};pose['siege_state']=rng.choice((0,2));history.append((at,{'x':pose}))
                generated+=['{Pose p;p.position=%s;p.yaw=%s;p.pitch=%s;p.roll=%s;p.turret_yaw=%s;p.gun_pitch=%s;p.siege=%d;h.sample(%s,{{"x",p}});}'%(point([pose[x] for x in ('x','y','z')]),number(pose['yaw']),number(pose['pitch']),number(pose['roll']),number(pose['turret_yaw']),number(pose['gun_pitch']),pose['siege_state'],number(at))]
            oracle._projectile_position_history=history
            low=rng.uniform(0,.4);high=rng.uniform(.4,1)
            expected=oracle._projectile_pose_sweep_fractions('x',low,high)
            generated+=['auto sweep=h.sweep("x",%s,%s);assert(sweep&&sweep->size()==%d);'%(number(low),number(high),len(expected))]
            generated+=['near((*sweep)[%d],%s);'%(i,number(value)) for i,value in enumerate(expected)];generated+=['}']
        generated+=['}']
        with tempfile.TemporaryDirectory(prefix='native-projectile-cursor-') as tmp:
            tmp=Path(tmp);source=tmp/'oracle.cpp';source.write_text('\n'.join(generated))
            sources=[ROOT/'bigworld/stock_native_projectile_cursor.cpp',ROOT/'bigworld/stock_native_projectile_history.cpp']
            for name,fixture in [('lifecycle',ROOT/'tests/test_stock_native_projectile_cursor.cpp'),('differential',source)]:
                binary=tmp/name
                subprocess.run([compiler,'-std=c++17','-O0','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'bigworld'),str(fixture)]+[str(x) for x in sources]+['-o',str(binary)],check=True)
                subprocess.run([str(binary)],check=True)

if __name__=='__main__':unittest.main()
