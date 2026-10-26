"""Replay oracle steps through the production binding (CPython 2.7 host)."""
import json
import os

import BigWorldIPC
import Host

traces = []
for step in json.load(open(os.environ['IPC_SCENARIOS'])):
    k = step['kernel']
    Host.setResults(k['created'], k['wait'], k['lastError'], k['released'], k['closed'], int(k['formatted']))
    args = [str(a) if isinstance(a, unicode) else a for a in step['args']]
    error, result, handle = None, None, 0
    if step['method'] == 'new':
        try:
            mutex = BigWorldIPC.Mutex(*args)
            result, handle = 'object', Host.handle(mutex)
        except Exception as e:
            error, result = [type(e).__name__, str(e)], 'NULL'
    else:
        mutex = BigWorldIPC.Mutex()
        Host.setHandle(mutex, step['handle'])
        if step['method'] == 'dealloc':
            del mutex
            result = 'void'
        else:
            try:
                value = getattr(mutex, step['method'])(*args)
                result = {None: 'None', True: 'True', False: 'False'}[value]
            except Exception as e:
                error, result = [type(e).__name__, str(e)], 'NULL'
            handle = Host.handle(mutex)
            Host.setHandle(mutex, 0)
    traces.append({'calls': [[c[0]] + [int(v) for v in c[1]] + list(c[2:]) for c in Host.calls()],
                   'error': error, 'result': result, 'handle': handle})
json.dump(traces, open(os.environ['IPC_TRACES'], 'w'))
print('BigWorldIPC production trace: %d steps' % len(traces))
