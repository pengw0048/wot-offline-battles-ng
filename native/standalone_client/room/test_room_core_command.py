"""Compare room command admission with actual pinned BattleState methods."""
import argparse
import copy
import importlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--probe', required=True)
    parser.add_argument('--resources', required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'wire'))
    import stage_server
    folder = tempfile.TemporaryDirectory(prefix='room-command-oracle-')
    staged = Path(folder.name) / 'server'
    stage_server.stage(str(args.oracle / 'server'), str(staged))
    sys.path[:0] = [str(staged), str(args.oracle / 'src/res/scripts/client')]
    source = importlib.import_module('lan_battle_server')
    from schema import SCHEMA

    initial = dict(type='patch', connections={'43': 2, '44': 3},
                   players={'1': dict(team=1), '2': dict(team=1),
                            '3': dict(team=2)})

    def command(message, peer=42):
        return dict(type='command', peer=peer, message=message)

    def chat(sequence, text='队伍消息 🚀', **extra):
        message = dict(type='team_chat', round_id=1, chat_seq=sequence, text=text)
        message.update(extra)
        return command(message)

    def oracle(operations):
        state = source.BattleState(team1_size=1, team2_size=1)
        state.phase = 'battle'
        state.round_id = 1
        state.authority_epoch = 0
        worker = source.SimulationWorker(None, ('127.0.0.1', 0))
        state.simulation_worker = worker
        state.bot_authority_id = -1
        peers = {0: worker}
        identities = {42: 1}
        deliveries = []
        output = []
        handler = object.__new__(source.ClientHandler)
        server = type('Server', (), dict(state=state))()
        for op in operations:
            deliveries.clear()
            if op['type'] == 'patch':
                identities.update({int(k): v for k, v in op.get('connections', {}).items()})
                for key, changes in op.get('players', {}).items():
                    identity = int(key)
                    if identity not in state.players:
                        state.players[identity] = source.Player(identity, None, ('127.0.0.1', 0))
                    for field, value in changes.items():
                        setattr(state.players[identity], field, value)
                for peer, identity in identities.items():
                    if identity in state.players:
                        peers[peer] = state.players[identity]
                for peer, endpoint in peers.items():
                    endpoint.offer_reliable = lambda message, peer=peer: deliveries.append(dict(peer=peer, message=copy.deepcopy(message))) or True
                    endpoint.send = endpoint.offer_reliable
                if 'phase' in op:
                    state.phase = op['phase']
            elif op['type'] == 'command':
                message = op['message']
                peer = op.get('peer', 42)
                endpoint = peers[peer]
                kind = message['type']
                if kind in source.ROUND_SCOPED_MESSAGE_TYPES and not state._message_round_matches(message):
                    output.append([])
                    continue
                if peer == 0:
                    handler._dispatch_simulation_worker_message(server, worker, message)
                elif kind == 'team_chat':
                    ack = state.submit_team_chat(endpoint.player_id, message)
                    if ack is not None:
                        if ack.get('accepted'):
                            state.publish_team_chat(endpoint.player_id, ack)
                        endpoint.send(ack)
                elif kind == 'worker_ping':
                    state.request_worker_ping(endpoint, message)
                elif kind not in source.MODERN_VISIBLE_MESSAGE_TYPES:
                    pass
            output.append(copy.deepcopy(deliveries))
        return output

    def compare(operations):
        operations = [initial] + operations
        request = dict(config=dict(schema=1, team1_size=1, team2_size=1), operations=operations)
        process = subprocess.run([args.probe, args.resources], input=json.dumps(request) + '\n',
                                 capture_output=True, text=True, check=True)
        actual = json.loads(process.stdout)
        expected = oracle(operations)
        for index, (row, wanted) in enumerate(zip(actual, expected)):
            got = [dict(peer=d['peer'], message=d['message']) for d in row['deliveries']]
            if got != wanted:
                raise AssertionError(dict(operation=index, request=operations[index], got=got, wanted=wanted))
            for delivery in got:
                SCHEMA.encode(delivery['message'])

    compare([chat(1), chat(1), chat(1, 'different'), chat(0), chat(True), chat(1.),
             chat(2, ''), chat(2, '\u2003\u00a0\t'), chat(2, '🚀' * 71),
             chat(2, '🚀' * 70), chat(3, 'literal\x00\ntext'),
             chat(4, 'wrong round', round_id=2), chat(4, extra=True), chat(4)])
    compare([chat(i, 'message %d' % i) for i in range(1, 71)] + [chat(1), chat(70, 'message 70')])
    compare([dict(type='patch', players={'1': dict(alive=False)}), chat(1),
             dict(type='patch', players={'1': dict(participating=False)}), chat(2),
             dict(type='patch', phase='waiting'), chat(3)])
    ping = dict(type='worker_ping', seq=1, client_time=123.5)
    pong = dict(type='worker_pong', player_id=1, seq=1, round_id=1,
                authority_epoch=0, client_time=123.5, frame_ms=16.5)
    compare([command(ping), command(dict(pong, seq=2), 0), command(pong, 0),
             command(pong, 0), command(dict(ping, seq=2)),
             command(dict(pong, seq=2, frame_ms=False), 0),
             command(dict(ping, seq=True)), command(dict(ping, client_time=0.)),
             command(dict(ping, seq=3)), command(dict(pong, seq=3, authority_epoch=1), 0),
             command(dict(pong, seq=3), 42), command(dict(pong, seq=3), 0)])
    compare([command(ping), command(dict(pong, seq=True, round_id=True), 0),
             command(dict(type='team_chat', round_id=1, chat_seq=1, text='worker rejected'), 0),
             command(dict(type='bot_observation', round_id=1, contacts=[]), 42)])

    def native(operations):
        request = dict(config=dict(schema=1, team1_size=1, team2_size=1), operations=operations)
        process = subprocess.run([args.probe, args.resources], input=json.dumps(request) + '\n',
                                 capture_output=True, text=True, check=True)
        result = json.loads(process.stdout)
        for row in result:
            for delivery in row['deliveries']:
                message = copy.deepcopy(delivery['message'])
                message.pop('_room_sections', None)
                SCHEMA.encode(message)
        return result

    loading = dict(type='patch', phase='loading', roster=[], connections={'43': 2},
                   players={'1': dict(team=1), '2': dict(team=2)})
    ready = dict(type='battle_ready', round_id=1)
    profiles = [dict(id=i, vehicle='ussr:R11_MS-1', mass=1000., shape=[1., 2., 0., 2.],
                     ram_profile=dict(spall_coefficient=1., ramming_bonus=0.)) for i in (1, 2)]
    operations = [loading, command(ready), command(ready, 43), command(ready, 0),
                  command(dict(type='bot_manifest', round_id=1, bots=[], player_collision_profiles=[]), 0),
                  command(dict(type='bot_manifest', round_id=1, bots=[], player_collision_profiles=profiles), 0)]
    rows = native(operations)
    assert all(row['phase'] == 'loading' for row in rows[:-1])
    assert rows[-1]['phase'] == 'battle'
    assert not any(d['message']['type'] == 'battle_live' for row in rows[:-1] for d in row['deliveries'])
    assert [d['message']['type'] for d in rows[-1]['deliveries']].count('battle_live') == 3
    cases = [None, {}, {'1': [[0, 0]]},
             {'1': [[3000., -3000.], [1., 2.], [3., 4.], [5., 6.], [7., 8.]], '2': [[9., 10.]]},
             {'1': [dict(x='1.12345', z=False, radius=12.5)], '2': [dict(x=0., z=0., radius=-1.)]},
             {'1': [dict(x=0., z=0., radius='nan'), [1., 2.]], '2': [[-3., 4.]]},
             {'1': [[True, False], ['inf', 0.]], '2': [dict(x='-2', z='3', radius='5')]}]
    for bases in cases:
        rows = native([loading, command(dict(ready, bases=bases), 0)])
        expected = json.loads(json.dumps(source.BattleState._sanitize_capture_bases(bases)))
        assert (rows[-1]['capture_bases'] or {}) == expected, (bases, rows[-1]['capture_bases'], expected)
    compare([command(ping), command(dict(ping, seq=2, client_time=456.75)),
             command(pong, 0), command(dict(pong, seq=2, client_time=456.75), 0),
             command(pong, 0), command(dict(pong, authority_epoch=1), 0),
             command(dict(pong, round_id=2), 0), command(dict(pong, player_id=True), 0)])
    folder.cleanup()
    print('Pinned staged Core command differential passed: 8 groups')


if __name__ == '__main__':
    main()
