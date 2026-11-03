# CPython 2.7: execute original #1513 consumer code objects without UI or OS writes.
from __future__ import print_function
import calendar
import marshal
import sys
import types
import zipfile

archive = zipfile.ZipFile(sys.argv[1])


def method(member, path, namespace):
    code = marshal.loads(archive.read(member)[8:])
    for name in path:
        code = next(value for value in code.co_consts
                    if isinstance(value, types.CodeType) and value.co_name == name)
    namespace.setdefault('__builtins__', __builtins__)
    return types.FunctionType(code, namespace)


class Bag(object):
    def __init__(self, **values):
        self.__dict__.update(values)


calls = []
def copy(text):
    calls.append(text)
    assert isinstance(text, unicode)
    return True

utils = Bag(copyToClipboard=method('scripts/client/gui/shared/utils/__init__.pyc',
    ['copyToClipboard'], {'BigWorld': Bag(wg_copyToClipboard=copy),
                         'LOG_DEBUG': lambda *args: None}))
callback = method('scripts/client/gui/Scaleform/daapi/view/lobby/user_cm_handlers.pyc',
    ['BaseUserCMHandler', 'copyToClipboard'], {'utils': utils})
for text in ('Player', '\xe7\x8e\xa9\xe5\xae\xb6', '', 'bad\xffname'):
    callback(Bag(userName=text))
    assert calls[-1] == unicode(text, 'utf-8', errors='ignore')

native_days = []
def first_day():
    native_days.append(1)
    return 6

namespace = {'BigWorld': Bag(wg_firstDayOfWeek=first_day), 'calendar': calendar,
    'MENU': Bag(DATETIME_WEEKDAYS_FULL_ENUM=list('MTWTFSS'),
                DATETIME_WEEKDAYS_SHORT_ENUM=list('mtwtfss')),
    'i18n': Bag(makeString=lambda key: key)}
first = method('scripts/client/gui/Scaleform/managers/UtilsManager.pyc',
    ['UtilsManager', 'getFirstDayOfWeek'], namespace)
assert first(Bag()) == 7 and len(native_days) == 1
weekdays = method('scripts/client/gui/Scaleform/managers/UtilsManager.pyc',
    ['UtilsManager', 'getWeekDayNames'], namespace)
owner = Bag(changeStringCasing=lambda text, upper: text.upper() if upper else text.lower())
assert weekdays(owner, False, False, False, True) == list('smtwtfs')
assert len(native_days) == 2
# RankedBattlesCalendarPopover explicitly opts out of regional settings.
assert weekdays(owner, True, False, False, False) == list('MTWTFSS')
assert len(native_days) == 2
print('Exact clipboard and calendar consumer contracts passed')
