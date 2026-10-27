# Python 2.7 driver: replay stock_lobby_input_oracle steps through the production binding.
import json
import sys

import BigWorld
import _input

data = json.load(open(sys.argv[1]))
for locale, name in data['locales'].items():
    _input.setLocaleName(int(locale), name)


def value(kind, item):
    if kind == 0:
        return item - (1 << 32) if item >= 1 << 31 else item
    return str(item) if kind == 1 else None


def call(function, args):
    try:
        return function(*[value(kind, item) for kind, item in args]), None
    except TypeError as error:
        return 'NULL', str(error)


out = []
for step in data['steps']:
    for key in (0x14, 0x90, 0x91):
        _input.setKeyState(key, step.get('keys', {}).get(str(key), 0))
    _input.calls()
    if step['kind'] == 'mode':
        result, error = call(BigWorld.wg_setRedefineKeysMode, step['args'])
        mode = int(_input.translate(0x30, 0, 0)[2] == 66)
        out.append([str(result), error, mode])
    elif step['kind'] == 'key':
        out.append(list(_input.translate(step['key'], step['state'], step['modifiers'])) + [_input.calls()])
    elif step['kind'] == 'lock':
        getter = {'caps': BigWorld.wg_isCapsLockOn, 'num': BigWorld.wg_isNumLockOn,
                  'scroll': BigWorld.wg_isScrollLockOn}[step['name']]
        result, error = call(getter, step['args'])
        out.append([result, error, _input.calls()])
    else:
        _input.setLayout(step['layout'])
        _input.calls()
        result, error = call(BigWorld.wg_getLangCode, step['args'])
        out.append([[ord(c) for c in result] if isinstance(result, str) and error is None else result, error,
                    _input.calls()])
sys.stdout.write(json.dumps(out))
