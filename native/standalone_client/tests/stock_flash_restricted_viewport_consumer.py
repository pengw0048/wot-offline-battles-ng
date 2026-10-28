# Execute the exact #1513 MarkersManager producer over a real GUI.Flash host.
import marshal
import sys
import types
import zipfile
import GUI
import _Scaleform

def component():
    return GUI.Flash(_Scaleform.MovieDef('gui/flash/VehicleMarkersManager.swf').createInstance())

if sys.argv[1]=='patched':
    flash=component()
    assert flash.drawWithRestrictedViewPort is True
    assert 'drawWithRestrictedViewPort' not in type(flash).__dict__
    for value,expected in ((False,False),(True,True),(0,False),(2,True),('false',False),('TrUe',True)):
        flash.drawWithRestrictedViewPort=value
        assert flash.drawWithRestrictedViewPort is expected
    for value in (None,[],1.5,u'false'):
        try:flash.drawWithRestrictedViewPort=value
        except TypeError:pass
        else:raise AssertionError('Accepted invalid native bool input')

with zipfile.ZipFile(sys.argv[2])as archive:
    code=marshal.loads(archive.read('scripts/client/gui/Scaleform/daapi/view/battle/shared/markers2d/manager.pyc')[8:])
for name in ('MarkersManager','__init__'):
    code=next(c for c in code.co_consts if isinstance(c,types.CodeType)and c.co_name==name)

class Object(object):pass
class Parent(object):
    def __init__(self,settings):
        self.component=component()
        self.movie=Object()
class MarkersManager(Parent):pass

aliases=Object();aliases.MARKERS_2D='markers2D'
settings=Object();settings.MARKERS_MANAGER_SWF='VehicleMarkersManager.swf'
constants=Object();constants.BATTLE_VEHICLE_MARKERS_REGISTER_CALLBACK='registerMarker'
environment={'__builtins__':__builtins__,'MarkersManager':MarkersManager,
    'ExternalFlashSettings':lambda *args:args,'BATTLE_VIEW_ALIASES':aliases,
    'settings':settings,'ROOT_SWF_CONSTANTS':constants,'DEPTH_OF_VehicleMarker':.5}
MarkersManager.__init__=types.FunctionType(code,environment)
if sys.argv[1]=='pristine':
    try:MarkersManager()
    except AttributeError:
        trace=sys.exc_info()[2]
        while trace.tb_next:trace=trace.tb_next
        assert trace.tb_lineno==37
        assert trace.tb_frame.f_code.co_filename.endswith('markers2d/manager.py')
    else:raise AssertionError('Original producer unexpectedly passed without native property')
    print('Negative control: original MarkersManager stopped at missing property line 37')
    sys.exit(0)
manager=MarkersManager()
assert manager.component.drawWithRestrictedViewPort is False
assert manager.component.wg_inputKeyMode==2
assert manager.movie.backgroundAlpha==0
assert manager._MarkersManager__plugins is None
assert manager._MarkersManager__canvas is None
assert manager._MarkersManager__ids==set()
assert component().drawWithRestrictedViewPort is True
print('Original MarkersManager constructor completed through inherited real bool property')
