# Python 2.7, run inside stock_runtime_services_host: argv = [this, JSON plan]
# Replays the oracle's frame-pacing plan through the production bindings and the
# patched App methods; prints the App frame time and reduced flag after each call.
import json
import sys

import _runtime_host as host

RATES = {0: 0, 8: 120, 16: 60}  # timeGetTime milliseconds -> renderer/*FrameRate
assert host.initScripts() == 13
import BigWorld

CALLS = {'set': BigWorld.wg_setSpecialFPSMode, 'disable': BigWorld.wg_disableSpecialFPSMode,
         'reduced': BigWorld.wg_setReducedFpsMode}
report = []
for label, normal, special, operations in json.loads(sys.argv[1]):
    config = dict((key, RATES[value]) for key, value in (('renderer/maxFrameRate', normal),
                                                         ('renderer/reducedFrameRate', special)) if value)
    host.configure(config, 1000.0)
    for operation in operations:
        assert CALLS[operation[0]](*operation[1:]) is None
        state = host.state()
        report.append([label, [operation[0]] + [repr(value) for value in operation[1:]],
                       [state['minFrameTime'], int(state['reduced'])]])
print(json.dumps(report))
