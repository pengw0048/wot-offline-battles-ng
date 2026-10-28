# Python 2.7, run inside stock_debug_queries_host: argv = [this, JSON plan]
# Replays the oracle's frame times and connection cases through the production
# DebugApp sampling and statistics task; prints getFPS and statPing per step.
import json
import sys

import _debug_host as host

plan = json.loads(sys.argv[1])
assert host.initScripts() == 4
import BigWorld

frames = []
for dt in plan['frames']:
    host.tickDebugApp(dt)
    frames.append(list(BigWorld.getFPS()))
ping = []
for server, online, latency, loading in plan['ping']:
    host.setConnection(1, server, online, latency)
    host.setLoadingTimer(loading)
    host.tickStatistics()
    ping.append(BigWorld.statPing())
print(json.dumps({'frames': frames, 'ping': ping}))
