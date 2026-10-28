"""Legal CPython 2.7 callables must keep the profiler out of their error path."""
import functools
import os
import sys
import StringIO
from callback_probe import Native, ask, component, profile

native = Native()
assert native.activate.__module__ is None
if os.environ.get('WOT_CALLBACK_METADATA_NEGATIVE'):
    # The production owner calls native methods through Script::call. The old
    # resolver emits the same bare error once per activation/deactivation.
    component(native)
    sys.exit(0)

assert profile(native.activate) == 'activate'
assert ask(native.activate) is None
assert component(native) is None
calls = []
def callback(): calls.append('called');return 42
callback.__module__ = None
assert profile(callback) == 'callback'
assert ask(callback) == 42 and calls == ['called']
class Bound(object):
    pass
# The bound-method resolver recurses into its function; use a proper self.
def method(self):return 43
method.__module__ = None
Bound.callback = method
bound = Bound().callback
assert profile(bound) == 'method' and ask(bound) == 43

class Callable(object):
    __module__ = None
    __name__ = None
    def __call__(self):return 44
assert profile(Callable()) == '' and ask(Callable()) == 44
for metadata in (None, 7, object(), u'unicode metadata'):
    value = Callable();value.__module__ = metadata;value.__name__ = metadata
    assert profile(value) == '' and ask(value) == 44

class Dynamic(object):
    @property
    def __module__(self):return ''.join(['dynamic','_module'])
    @property
    def __name__(self):return ''.join(['dynamic','_call'])
    def __call__(self):return 45
assert profile(Dynamic()) == 'dynamic_module.dynamic_call'
assert ask(Dynamic()) == 45
assert ask(functools.partial(callback)) == 42
class OldStyle:
    def __call__(self):return 46
assert ask(OldStyle()) == 46
assert isinstance(ask(Callable), Callable)
for fn in (native.activate, callback, Dynamic(), functools.partial(callback)):
    before = sys.getrefcount(fn)
    for unused in xrange(50):profile(fn);ask(fn)
    assert sys.getrefcount(fn) == before

def failure():raise RuntimeError('real callback failure')
failure.__module__ = None
try:ask(failure)
except RuntimeError as error:
    assert str(error) == 'real callback failure' and sys.exc_info()[2] is not None
else:raise AssertionError('Real callback failure was swallowed')
stream = StringIO.StringIO();previous = sys.stderr;sys.stderr = stream
try:
    assert ask(failure, 1) is False
finally:sys.stderr = previous
assert 'real callback failure' in stream.getvalue()
assert 'expected string or Unicode object' not in stream.getvalue()
print('Real SDK callback profiler and Svarog native-method lifecycle passed')
