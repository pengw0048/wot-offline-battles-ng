"""Drive the actual patched native classes and exact stock light lifecycle."""
from __future__ import print_function
import functools
import gc
import marshal
import os
import string
import types
import weakref
import zipfile
import BigWorld
import _effect_lights_host as H


class Provider(object):
    def __init__(self, value=(0., 0., 0., 0.)):
        self.value = value


H.setProviderType(Provider)
for factory in (BigWorld.PyOmniLight, BigWorld.PyChunkLight):
    light = factory()
    assert type(light).__mro__[-2].__name__ == 'PyObjectPlus'
    assert light.colorAnimator is None and light.multiplierAnimator is None
    assert light.castShadows is False
    light.position = (1., 2., 3.)
    light.visible = True
    for value, flag in ((True, 1), (False, 0), (7, 1), (0, 0), ('TrUe', 1), ('false', 0), (-3, 1)):
        light.castShadows = value
        assert light.castShadows is bool(flag) and H.shadowFlag() == flag
    for value in ('yes', object(), (), 1.5, None):
        try: light.castShadows = value
        except TypeError: pass
        else: raise AssertionError(('accepted invalid shadow flag', value))
        assert light.castShadows is True
    payloads = [Provider((3., 5., 7., 11.)), Provider((13., 17., 19., 23.))]
    providers = [H.provider(value) for value in payloads]
    refs = [weakref.ref(value) for value in payloads]
    del value
    light.colorAnimator, light.multiplierAnimator = providers
    assert light.colorAnimator is providers[0] and light.multiplierAnimator is providers[1]
    light.colour = (.1, .2, .3, 1.)
    before = light.colour
    H.tick()
    # Exact #1513 compiled-space tick does not consume these two fields.
    assert light.colour == before
    for name in ('colorAnimator', 'multiplierAnimator'):
        previous = getattr(light, name)
        for invalid in (object(), 1, (1., 2., 3.), [1., 2., 3., 4.], ('bad', 2., 3., 4.)):
            try: setattr(light, name, invalid)
            except TypeError: pass
            else: raise AssertionError(('accepted non-provider', name))
            assert getattr(light, name) is previous
        del previous
    del providers,payloads
    gc.collect()
    assert all(ref() is not None for ref in refs)
    light.colorAnimator = None
    gc.collect()
    assert refs[0]() is None and refs[1]() is not None
    light.shader = H.provider(Provider((255., 127.5, 63.75, 255.)))
    H.tick()
    assert all(abs(a-b) < 1e-6 for a, b in zip(light.colour, (1., .5, .25, 1.)))
    del light
    gc.collect()
    assert refs[1]() is None and H.state()[0] == 0
    assert H.state()[1] == H.state()[2]
    assert H.providerCount()==0
    # The real SDK Vector4 tuple parser, Vector4Provider::coerce and native
    # Vector4Basic constructor/output execute, rather than a type-check fake.
    light=factory()
    light.colorAnimator=(1., 2., 3., 4.)
    constant=light.colorAnimator
    assert type(constant).__name__=='Vector4Basic' and constant.value==(1., 2., 3., 4.)
    light.multiplierAnimator=(1., 2., 3., 4.)
    assert light.multiplierAnimator is not constant
    constant.value=(9., 8., 7., 6.)
    assert light.colorAnimator is constant and light.multiplierAnimator.value==(1., 2., 3., 4.)
    light.shader=(255., 127.5, 63.75, 255.)
    H.tick()
    assert all(abs(a-b)<1e-6 for a,b in zip(light.colour,(1., .5, .25, 1.)))
    light.colorAnimator=None
    assert constant.value==(9., 8., 7., 6.)
    del constant,light
    gc.collect()
    assert H.providerCount()==0
print('Effect light owner and provider lifetime passed')

client = os.environ.get('WOT_0922_CLIENT')
if client:
    def codes(code):
        yield code
        for item in code.co_consts:
            if isinstance(item, types.CodeType):
                for child in codes(item): yield child

    archive = zipfile.ZipFile(os.path.join(client, 'res/packages/scripts.pkg'))
    root = marshal.loads(archive.read('scripts/client/helpers/EffectsList.pyc')[8:])
    descriptor = next(code for code in codes(root) if code.co_name == '_LightEffectDesc')
    pending = {}
    def callback(delay, function):
        assert delay == .01
        ident = len(pending) + 1
        pending[ident] = function
        return ident
    BigWorld.callback = callback
    BigWorld.cancelCallback = lambda ident: pending.pop(ident)
    math = types.ModuleType('Math')
    math.Vector4Animation = lambda: H.provider(Provider())
    class Node(object):
        actualNode = Provider((10., 20., 30., 0.))
    node = Node()
    env = {'__builtins__': __builtins__, 'BigWorld': BigWorld, 'Math': math,
           '_ALLOW_DYNAMIC_LIGHTS': True, 'partial': functools.partial, 'string': string,
           '_findTargetNode': lambda *unused: node}
    value = type('OriginalLightDescriptor', (object,), {})()
    for name in ('create', '_callbackCreate', 'delete'):
        code = next(code for code in codes(descriptor) if code.co_name == name)
        setattr(value, name, types.MethodType(types.FunctionType(code, env), value))
    value._nodeName = []
    value.duration = .5
    value._colorAnimation = [(0., (255., 0., 0., 255.)), (1., (0., 0., 0., 0.))]
    value._multiplierAnimation = [(0., (1., 1., 1., 1.)), (1., (0., 0., 0., 0.))]
    value._innerRadius, value._outerRadius, value._castShadows = 2., 5., True
    entries = []
    value.create(object(), entries, {})
    assert len(pending) == 1 and entries[0]['light'] is None
    pending.pop(entries[0]['callback'])()
    light = entries[0]['light']
    H.tick()
    assert light.position == (10., 20., 30.) and light.visible
    assert light.castShadows and H.shadowFlag() == 1
    assert light.colorAnimator.keyframes == [(0., (255., 0., 0., 255.)), (.5, (0., 0., 0., 0.))]
    assert light.multiplierAnimator.duration == 10000000.
    refs = (weakref.ref(H.payload(light.colorAnimator)), weakref.ref(H.payload(light.multiplierAnimator)))
    assert value.delete(entries[0], {}) is True
    assert not light.visible and light.source is None and entries[0]['light'] is None
    del light
    gc.collect()
    assert all(ref() is None for ref in refs) and H.state()[0] == 0
    entries = []
    value.create(object(), entries, {})
    assert len(pending) == 1
    assert value.delete(entries[0], {}) is True and not pending
    assert H.state()[0] == 0
    assert H.providerCount()==0
    print('Original EffectsList light lifecycle passed')
