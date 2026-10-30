"""Primitive replay IPC projection for owned snapshot static value versions.

The original recorder freezes bytes synchronously, then queues only bytes.
Keep that boundary and its restricted reader. Never pickle immutable dict/list
subclasses as globals, or recursively copy every dynamic snapshot value.
"""
try:
    import cPickle as _pickle
except ImportError:
    import pickle as _pickle

from gui.mods.offline_lan_0922 import stock_snapshot_static as _static


def _message(value):
    result = value
    for name in ('players', 'bots', 'bot_manifest'):
        rows = value.get(name)
        if not isinstance(rows, (list, tuple)):
            continue
        projected = rows
        for index, row in enumerate(rows):
            if not isinstance(row, dict):
                continue
            copied = row
            for field in _static.STATIC_FIELDS:
                item = row.get(field)
                if type(item) not in (_static._StaticDict, _static._StaticList):
                    continue
                if copied is row:
                    copied = dict(row)
                copied[field] = item._replay_plain()
            if copied is not row:
                if projected is rows:
                    projected = list(rows)
                projected[index] = copied
        if projected is not rows:
            if result is value:
                result = dict(value)
            result[name] = projected
    return result


def dumps(value, protocol):
    # The pinned ProcessRecorder creates exactly these primitive envelopes.
    # Unknown objects still reach the unchanged restricted reader and fail.
    projected = value
    if value.get('op') == 'start' and isinstance(value.get('header'), dict):
        header = value['header']
        copied = header
        for name, section in header.items():
            if not isinstance(section, dict):
                continue
            message = _message(section)
            if message is not section:
                if copied is header:
                    copied = dict(header)
                copied[name] = message
        if copied is not header:
            projected = dict(value)
            projected['header'] = copied
    elif (value.get('op') == 'record' and value.get('kind') == 'wire' and
          isinstance(value.get('data'), dict)):
        message = _message(value['data'])
        if message is not value['data']:
            projected = dict(value)
            projected['data'] = message
    return _pickle.dumps(projected, protocol)
