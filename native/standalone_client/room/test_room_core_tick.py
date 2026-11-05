"""Portable Core publication checks; no native engine or TCP is exercised."""
import argparse
import copy
import json
import math
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', required=True)
    parser.add_argument('--resources', required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'wire'))
    from schema import SCHEMA

    def run(operations):
        request = dict(config=dict(schema=1, team1_size=1, team2_size=1),
                       operations=operations)
        result = subprocess.run([args.probe, args.resources],
                                input=json.dumps(request) + '\n', text=True,
                                capture_output=True, check=True)
        output = json.loads(result.stdout)
        for row in output:
            for delivery in row['deliveries']:
                message = copy.deepcopy(delivery['message'])
                message.pop('_room_sections', None)
                SCHEMA.encode(message)
        return output

    cold = dict(strategy=dict(marker='configured'), arena_bounds=[-1, -2, 3, 4])
    for root_shared in (False, True):
        configured = run([dict(type='configure', resources=cold, root_shared=root_shared)])[-1]
        assert configured['strategy'] == cold['strategy']
        assert configured['arena_bounds'] == cold['arena_bounds']

    def advance(tick):
        return dict(type='advance', clock_us=round(tick * 1000000 / 30))

    rows = run([advance(i) for i in range(1, 91)])
    assert [r['tick'] for r in rows] == list(range(1, 91))
    assert [r['server_time_ms'] for r in rows] == [round(i * 1000 / 30)
                                                   for i in range(1, 91)]
    assert all(any(d['peer'] == 0 for d in r['deliveries']) for r in rows)
    assert all(any(d['peer'] == 42 for d in r['deliveries']) ==
               (r['tick'] <= 1 or r['tick'] % 2 == 0) for r in rows)
    rows = run([dict(type='patch', barrier=True), advance(1), advance(2)])
    assert [r['tick'] for r in rows] == [0, 0, 1]

    orders = [dict(id=1, shell_index=0, route_attempt={'large': 'diagnostic'})]
    replacement = [dict(id=1, shell_index=1, route_attempt={'different': True})]
    rows = run([dict(type='patch', orders=orders), dict(type='publish'),
                dict(type='publish'), dict(type='patch', orders=replacement),
                dict(type='publish')])
    assert rows[1]['manifest_reused'] and rows[2]['manifest_reused']
    assert rows[2]['orders_reused']
    assert not rows[3]['orders_reused']
    assert rows[4]['snapshot']['bot_orders'] == [dict(id=1, shell_index=1)]
    assert rows[4]['snapshot']['bot_order_revision'] == 0

    event = dict(kind='detection', observer_kind='player', observer_id=1,
                 target_kind='bot', target_id=2)
    rows = run([dict(type='patch', tick=3, events=[event] * 600),
                dict(type='publish'), dict(type='patch', events=[event]),
                dict(type='publish')])
    messages = [d['message'] for d in rows[1]['deliveries'] if d['peer'] == 42]
    assert [len(m['events']) for m in messages if m['type'] == 'events'] == [256, 256, 88]
    assert messages[-1]['type'] == 'snapshot'
    identities = [e['event_id'] for m in messages if m['type'] == 'events'
                  for e in m['events']]
    final = [d['message'] for d in rows[3]['deliveries']
             if d['peer'] == 42 and d['message']['type'] == 'events'][0]
    assert final['events'][0]['event_id'] == '1:3:600'
    assert len(set(identities)) == 600

    rows = run([dict(type='patch', writable=False), dict(type='publish'),
                dict(type='publish'), dict(type='patch', events=[event]),
                dict(type='publish'), dict(type='publish'),
                dict(type='patch', writable=True), advance(1)])
    assert rows[1]['pending'] == rows[2]['pending'] == 1
    assert rows[4]['pending'] == rows[5]['pending'] == 3
    delivered = [d['message']['type'] for d in rows[-1]['deliveries'] if d['peer'] == 42]
    assert delivered[:3] == ['snapshot', 'events', 'snapshot']
    rows = run([dict(type='patch', phase='loading'), dict(type='publish')])
    assert rows[-1]['deliveries'][0]['message']['client_build']
    assert 'timing' not in rows[-1]['deliveries'][0]['message']
    rows = run([dict(type='stop'), advance(1)])
    assert rows[-1]['tick'] == 0 and not rows[-1]['deliveries']
    rows = run([dict(type='patch', players={'1': dict(forward=.7, turn=-.4)}),
                dict(type='advance', dt=.2, clock_us=200000)])
    actor = rows[-1]['actors']['1']
    yaw = -.4 * .85 * .2
    assert actor['yaw'] == yaw
    assert actor['x'] == math.sin(yaw) * 14 * .7 * .2
    assert actor['z'] == math.cos(yaw) * 14 * .7 * .2
    rows = run([dict(type='patch', players={'1': dict(client_position=True,
                forward=1., turn=1., x=12., z=34.)}), advance(1)])
    assert rows[-1]['actors']['1']['x'] == 12.
    assert rows[-1]['actors']['1']['z'] == 34.

    rows = run([dict(type='patch', players={'1': {}}), dict(type='finish'),
                dict(type='publish'), dict(type='publish')])
    messages = [d['message']['type'] for d in rows[2]['deliveries'] if d['peer'] == 42]
    assert messages == ['battle_receipt', 'events', 'snapshot']
    assert not any(d['message']['type'] == 'battle_receipt' for d in rows[3]['deliveries'])
    rows = run([dict(type='patch', players={'1': {}}, round=7), dict(type='finish'),
                dict(type='patch', round=8, result=None), dict(type='finish'),
                dict(type='publish')])
    receipts = [d['message'] for d in rows[-1]['deliveries'] if d['message']['type'] == 'battle_receipt']
    assert len(receipts) == 1 and receipts[0]['round_id'] == 8
    rows = run([dict(type='patch', players={'1': {}}, tick=20),
                dict(type='disconnect', peer=0)])
    assert rows[-1]['snapshot']['battle_result']['reason'] == 'worker_disconnected'
    assert not any(d['message']['type'] == 'battle_receipt' for d in rows[-1]['deliveries'])
    assert rows[-1]['epoch'] == 1
    assert any(event['kind']=='authority' for delivery in rows[-1]['deliveries']
               for event in delivery['message'].get('events',[]))
    print('Portable Core tick/publication checks passed: 10 groups')


if __name__ == '__main__':
    main()
