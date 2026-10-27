# Python 2.7 driver: replay stock_flash_input_mode_oracle steps through the production binding.
import json
import sys

import GUI
import _Scaleform
import _flashhost as host

KEY_A = 30
data = json.load(open(sys.argv[1]))


def value(kind, item):
    return {'int': int, 'bool': bool, 'long': long, 'float': float, 'str': str,
            'none': lambda unused: None}[kind](item)


class Script(object):
    def __init__(self, answer):
        self.answer = answer

    def handleKeyEvent(self, event):
        return self.answer


def component(mode):
    movie = _Scaleform.MovieDef('gui/flash/trace.swf').createInstance()
    flash = GUI.Flash(movie)
    flash.wg_inputKeyMode = long(mode)
    return flash, movie


out = []
for step in data['steps']:
    if step['kind'] == 'set':
        flash, movie = component(step['initial'])
        try:
            flash.wg_inputKeyMode = value(*step['value'])
            error = None
        except TypeError as exception:
            error = str(exception)
        stored = flash.wg_inputKeyMode
        out.append([error, stored, 'int' if type(stored) is int else 'long'])
    else:
        flash, movie = component(step['mode'])
        host.consumes(movie, step['movie'])
        if step['base']:
            child = host.Component()
            child.focus = True
            child.script = Script(True)
            host.addChild(flash, child)
        if step['script']:
            # Without focus SimpleGUIComponent does not ask the script; its presence alone matters.
            flash.script = Script(True)
        host.events()
        result = host.componentKey(flash, KEY_A)
        out.append([result, 'trace.swf:key:30:1' in host.events()])
    del flash, movie
    host.events()
sys.stdout.write(json.dumps(out))
