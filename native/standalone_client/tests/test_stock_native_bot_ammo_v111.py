"""Compare native initial Bot ammunition with the v1.1.1 policy."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = os.environ.get('WOT_FORK_SOURCE')
sys.path.insert(0, str(ROOT/'bigworld'))
from prepare_offline_fork import stage_bot_ammo_data

@unittest.skipUnless(SOURCE, 'set WOT_FORK_SOURCE to v1.1.1 source')
class AmmoV111Test(unittest.TestCase):
    def test_allocations_and_order(self):
        source = Path(SOURCE)
        sys.path.insert(0, str(source/'src/res/scripts/client'))
        try:
            from gui.mods.offline_lan_0922 import ammo_policy, default_loadouts
            guns = default_loadouts.GUN_AMMUNITION
            gun, names = next((key, names) for key, names in guns.items() if len(names)==3)
            nation, gun_name = gun.split(':', 1)
            def shell(i, kind, name): return dict(index=i, kind=kind, name=name)
            standard = [shell(0, 'ARMOR_PIERCING', 'ap'), shell(1, 'ARMOR_PIERCING_CR', 'apcr'), shell(2, 'HIGH_EXPLOSIVE', 'he')]
            examples = [
                (dict(vehicle_name='test:tank', gun_name='ordinary', class_tag='heavyTank', shells=standard, clip_size=1), 47),
                (dict(vehicle_name='test:tank', gun_name='ordinary', class_tag='heavyTank', shells=standard, clip_size=3), 47),
                (dict(vehicle_name='test:tank', gun_name='ordinary', class_tag='heavyTank', shells=standard, clip_size=3, ammo_ratios=[0,1,3], ammo_limit=22), 47),
                (dict(vehicle_name='test:tank', gun_name='ordinary', class_tag='SPG', shells=[shell(0,'HIGH_EXPLOSIVE','h1'), shell(1,'HIGH_EXPLOSIVE','h2'), shell(2,'ARMOR_PIERCING','ap')], clip_size=1), 19),
                (dict(vehicle_name='test:tank', gun_name='ordinary', class_tag='heavyTank', shells=[shell(0,'HIGH_EXPLOSIVE','h1'),shell(1,'HIGH_EXPLOSIVE','h2'),shell(2,'HOLLOW_CHARGE','hc')], clip_size=1), 3),
                (dict(vehicle_name=nation+':tank',gun_name=gun_name,class_tag='heavyTank',shells=[shell(i, 'HIGH_EXPLOSIVE' if i==2 else 'ARMOR_PIERCING', name) for i,name in enumerate(names)],clip_size=1), 51),
            ]
            lines=['#include "stock_native_bot_ammo.hpp"', '#include <iostream>', 'using namespace stock_native_bot_ammo;', 'int main(){']
            expected=[]
            quote=json.dumps
            for p, capacity in examples:
                lines += ['{ Profile p;', 'p.vehicle='+quote(p['vehicle_name'])+';p.gun='+quote(p['gun_name'])+';p.class_tag='+quote(p['class_tag'])+';', 'p.clip_size='+str(p['clip_size'])+';']
                for s in p['shells']:
                    lines.append('p.shells.push_back({%d,%s,%s});' % (s['index'],quote(s['kind']),quote(s['name'])))
                if 'ammo_ratios' in p:
                    lines.append('p.manual=true;p.ratios={%s};' % ','.join(map(str,p['ammo_ratios'])))
                if 'ammo_limit' in p: lines.append('p.ammo_limit=%d;' % p['ammo_limit'])
                lines.append('auto a=allocate(p,%d,%d);for(auto x:a.remaining)std::cout<<x<<" ";std::cout<<"| ";for(auto x:a.order)std::cout<<x<<" ";std::cout<<"\\n";}' % (capacity,len(p['shells'])))
                expected.append((ammo_policy.allocation(p,capacity,len(p['shells'])),[s['index'] for s in ammo_policy.stages(p)]))
            lines.append('}')
        finally:
            sys.path.pop(0)
        with tempfile.TemporaryDirectory(prefix='bot-ammo-v111-') as directory:
            folder=Path(directory);stage_bot_ammo_data(source,folder)
            (folder/'probe.cpp').write_text('\n'.join(lines))
            subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(folder),'-I'+str(ROOT/'bigworld'),str(folder/'probe.cpp'),'-o',str(folder/'probe')],check=True)
            output=subprocess.check_output([str(folder/'probe')],text=True).splitlines()
        actual=[]
        for row in output:
            counts,order=row.split('|');actual.append(([int(v) for v in counts.split()],[int(v) for v in order.split()]))
        self.assertEqual(actual,expected)

if __name__=='__main__':unittest.main()
