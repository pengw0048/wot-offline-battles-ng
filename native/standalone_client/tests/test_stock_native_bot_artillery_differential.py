"""Compare native artillery queue decisions with fixed gameplay Python."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SOURCE=os.environ.get('WOT_GAMEPLAY_SOURCE')
PROBE=r'''
#include "stock_native_bot_artillery.hpp"
#include <iostream>
using namespace wot::offline_bot_step;
struct World:ArcWorld {bool blocked=false;ArcQuery segment(const Point&a,const Point&b,Point& hit) override {
 if(!blocked)return ArcQuery::clear; for(int i=0;i<3;++i)hit[i]=(a[i]+b[i])*.5;return ArcQuery::hit;}};
int main(){ArcQueue q(1,2.5,.25,60.,false);World w;ArcKey a;a.actor=1;ArcKey b;b.actor=2;
 ArcCandidate c;c.arc="low";c.path={{0.,0.,0.},{10.,0.,0.},{20.,0.,0.}};
 q.request(a,{c},{20.,0.,0.},0.);q.request(b,{c},{20.,0.,0.},0.);
 for(int i=0;i<4;++i){auto used=q.advance(0.,1,w);std::cout<<used<<' '<<q.result(a,0.).ready<<' '<<q.result(b,0.).ready<<'\n';}
 ArcQueue x(1,.35,.25,40.,true);c.arc="exact_launch";c.path={{0.,0.,0.},{30.,0.,0.},{40.,0.,0.}};
 x.request(a,{c},{40.,0.,0.},0.);x.advance(0.,1,w);w.blocked=true;x.advance(0.,1,w);
 auto r=x.result(a,0.);std::cout<<r.ready<<' '<<r.candidate.has_value()<<' '<<r.candidate->path.back()[0]<<'\n';
}
'''
@unittest.skipUnless(SOURCE,'set WOT_GAMEPLAY_SOURCE to the fixed gameplay source')
class ArtilleryDifferential(unittest.TestCase):
    def test_fairness_and_remote_impact(self):
        path=Path(SOURCE)/'src/res/scripts/client/gui/mods/offline_lan_0922/artillery_arc_queue.py'
        spec=importlib.util.spec_from_file_location('fixed_artillery_arc_queue',path)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder);(p/'probe.cpp').write_text(PROBE)
            sources=['stock_native_bot_step_state.cpp','stock_native_bot_weapon.cpp','stock_native_bot_artillery.cpp']
            command=['c++','-std=c++17','-I',str(ROOT/'bigworld'),'-I',str(Path(SOURCE)/'native')]
            command += [str(ROOT/'bigworld'/s) for s in sources]
            command += [str(Path(SOURCE)/'native/offline_simulation_weapons.cpp'),str(p/'probe.cpp'),'-o',str(p/'probe')]
            subprocess.run(command,check=True)
            output=subprocess.check_output([str(p/'probe')],text=True).splitlines()
        q=module.ArcProbeQueue(max_jobs=1,success_ttl=2.5,failure_ttl=.25,max_job_age=60.)
        candidate={'arc':'low','path':((0.,0.,0.),(10.,0.,0.),(20.,0.,0.))}
        q.request(1,(candidate,),(20.,0.,0.),0.);q.request(2,(candidate,),(20.,0.,0.),0.)
        expected=[]
        for _ in range(4): expected.append((q.advance(0.,1,lambda a,b:None),q.result(1,0.)[0],q.result(2,0.)[0]))
        self.assertEqual([[int(x) for x in row.split()] for row in output[:4]], [list(map(int,row)) for row in expected])
        q=module.ArcProbeQueue(max_jobs=1,success_ttl=.35,failure_ttl=.25,max_job_age=40.,allow_remote_impact=True)
        candidate={'arc':'exact_launch','path':((0.,0.,0.),(30.,0.,0.),(40.,0.,0.))}
        q.request(1,(candidate,),(40.,0.,0.),0.);q.advance(0.,1,lambda a,b:None)
        q.advance(0.,1,lambda a,b:tuple((a[i]+b[i])*.5 for i in range(3)))
        ready,receipt=q.result(1,0.)
        self.assertEqual([float(x) for x in output[4].split()],[float(ready),float(receipt is not None),receipt['path'][-1][0]])

if __name__=='__main__': unittest.main()
