"""Exercise real CPython sets and exact ordinary battle PYC producers."""
from __future__ import print_function
import gc
import __builtin__
import json
import marshal
import sys
import types
import weakref
import zipfile
from _flash_collections import convert


def find(code, names):
    for name in names:
        code = next(c for c in code.co_consts if isinstance(c, types.CodeType) and c.co_name == name)
    return code


def contracts():
    assert convert(set()) == ('array', [])
    assert sorted(convert(set([3, 1, 2]))[1]) == [('number', 1.), ('number', 2.), ('number', 3.)]
    assert convert({'tags': set(), 'nested': [set([7])]}) == (
        'object', {'tags': ('array', []), 'nested': ('array', [('array', [('number', 7.)])])})
    class Child(set):
        pass
    assert convert(Child([9])) == ('array', [('number', 9.)])
    assert convert((1, [2], {'three': 3})) == ('array', [
        ('number', 1.), ('array', [('number', 2.)]), ('object', {'three': ('number', 3.)})])
    class Iterable(object):
        def __iter__(self):
            return iter([1])
    for unsupported in (frozenset([1]), iter([1]), (v for v in [1]), Iterable(), object()):
        assert convert(unsupported) == ('undefined',), type(unsupported)
    # The original takes the subclass iterator, rather than silently reading its base table.
    class Ordered(set):
        def __iter__(self):
            return iter([4, 2, 8])
    assert convert(Ordered()) == ('array', [('number', 4.), ('number', 2.), ('number', 8.)])
    class Item(object):
        pass
    item = Item()
    baseline = sys.getrefcount(item)
    values = Ordered()
    class RetainingIterator(object):
        def __init__(self):
            self.pending = True
        def __iter__(self):
            return self
        def next(self):
            if not self.pending:
                raise StopIteration
            self.pending = False
            return item
    references = []
    class Lifetime(set):
        def __iter__(self):
            it = RetainingIterator()
            references.append(weakref.ref(it))
            return it
    for unused in range(100):
        assert convert(Lifetime()) == ('array', [('undefined',)])
    gc.collect()
    assert sys.getrefcount(item) == baseline
    assert all(ref() is None for ref in references)
    class Broken(set):
        def __iter__(self):
            yield item
            raise ValueError('iteration failed')
    class BrokenStart(set):
        def __iter__(self):
            raise RuntimeError('cannot iterate')
    for kind, error, message in ((Broken, ValueError, 'iteration failed'),
                                 (BrokenStart, RuntimeError, 'cannot iterate')):
        try:
            convert(kind())
        except error as caught:
            assert str(caught) == message
        else:
            raise AssertionError('Iterator error was swallowed')
    gc.collect()
    assert sys.getrefcount(item) == baseline
    print('CPython collection conversion: positive, negative, recursive and lifetime contracts passed')


def consumers(client):
    package = zipfile.ZipFile(client + '/res/packages/scripts.pkg')
    def code(member, names):
        return find(marshal.loads(package.read('scripts/client/' + member + '.pyc')[8:]), names)
    class Page(object):
        def as_setComponentsVisibilityS(self, visible, hidden):
            self.values = convert(visible), convert(hidden)
    page = Page()
    update = types.FunctionType(code('gui/Scaleform/daapi/view/battle/shared/page',
        ('SharedPage', '_setComponentsVisibility')), {'LOG_DEBUG': lambda *a: None, '__builtins__': __builtin__}, None, (None, None))
    update(page, hidden=set(['playersPanel', 'minimap']))
    assert page.values[0] == ('array', [])
    assert sorted(page.values[1][1]) == [('string', 'minimap'), ('string', 'playersPanel')]
    update(page, visible=set(['minimap']))
    assert page.values == (('array', [('string', 'minimap')]), ('array', []))
    class IGR(object):
        BASE, PREMIUM = 1, 2
    class Tags(object):
        IGR_BASE, IGR_PREMIUM = 'igrBase', 'igrPremium'
    class Storage(object):
        def getUser(self, accountDBID):
            return None
    class Context(object):
        usersStorage = Storage()
    context = Context()
    g = {'IGR_TYPE': IGR, 'USER_TAG': Tags, '__builtins__': __builtin__}
    context.addTagByIGRType = types.FunctionType(code(
        'gui/Scaleform/daapi/view/battle/shared/stats_exchage/broker',
        ('ExchangeCtx', 'addTagByIGRType')), g)
    tags = types.FunctionType(code('gui/Scaleform/daapi/view/battle/shared/stats_exchage/broker',
        ('ExchangeCtx', 'getUserTags')), g)
    for igr, expected in ((0, []), (1, [('string', 'igrBase')]), (2, [('string', 'igrPremium')])):
        assert convert(tags(context, 42, igr)) == ('array', expected)
    print('Original #1513 SharedPage and stats ExchangeCtx PYC collection producers passed')


contracts()
if len(sys.argv) > 1:
    consumers(sys.argv[1])
