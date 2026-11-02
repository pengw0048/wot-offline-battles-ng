"""Run the actual staged account/compat installation on CPython 2.7."""
from __future__ import print_function
import imp
import sys
import types


class O(object):
    def __init__(self, **values):
        self.__dict__.update(values)


def stock(*args, **kwargs):
    return 'stock'


def kind(name, methods):
    return type(name, (object,), dict((method, stock) for method in methods.split()))


def runtime():
    account = kind('Account', '__init__ onBecomePlayer onBecomeNonPlayer')
    avatar = kind('Avatar', '''__init__ onBecomePlayer onBecomeNonPlayer onEnterWorld
        onLeaveWorld vehicle_onEnterWorld _PlayerAvatar__startVehicleVisual
        onPrereqsLoaded getOwnVehicleSpeeds autoAim
        _PlayerAvatar__onSetOwnVehicleAuxPhysicsData''')
    vehicle = kind('Vehicle', '''getSpeed onEnterWorld startVisual onLeaveWorld
        _Vehicle__startWGPhysics set_gunAnglesPacked collideSegment collideSegmentExt''')
    compound = kind('Compound', '''deactivate _CompoundAppearance__onModelsRefresh
        _CompoundAppearance__updateEffectsLOD''')
    rotator = kind('Rotator', 'getAvatarOwnVehicleStabilisedMatrix predictLockedTargetShotPoint')
    rotator.showServerMarker = property(stock, stock)
    control = kind('Control', 'handleKeyEvent')
    return O(account_module=O(PlayerAccount=account), avatar_module=O(PlayerAvatar=avatar),
        vehicle_module=O(Vehicle=vehicle), compound_appearance_module=O(CompoundAppearance=compound),
        ammo_controller_type=kind('Ammo', 'changeSetting'), avatar_getter=O(),
        avatar_input_handler=O(AvatarInputHandler=kind('Handler', 'onControlModeChanged')),
        control_modes=O(ArcadeControlMode=control, SniperControlMode=kind('SniperControl', 'handleKeyEvent')),
        avatar_position_control=O(ConsistentMatrices=kind('Matrices', '_ConsistentMatrices__linkOwnVehicle')),
        steady_vehicle_matrix=O(SteadyVehicleMatrixCalculator=kind('Steady', 'relinkSources')),
        vehicle_gun_rotator=O(VehicleGunRotator=rotator),
        gun_marker_ctrl=O(useClientGunMarker=stock, useServerGunMarker=stock),
        projectile_mover_module=O(segmentMayHitEntity=stock, getCollidableEntities=stock),
        acceleration_smoother_type=kind('Smoother', 'update'),
        arcade_camera_type=kind('ArcadeCamera', '_ArcadeCamera__calcCurOscillatorAcceleration'),
        sniper_camera_type=kind('SniperCamera', '_SniperCamera__calcCurOscillatorAcceleration'),
        strategic_camera_type=kind('StrategicCamera', '_StrategicCamera__cameraUpdate'),
        crashed_tracks_controller_type=kind('Tracks', '_CrashedTrackController__setupTrackAssembler _CrashedTrackController__onModelLoaded'),
        custom_effect_manager_type=kind('Effects', 'update'),
        vehicle_marker_plugin_type=kind('Marker', 'start stop'),
        bigworld=O(connect=stock, disconnect=stock, serverTime=stock, target=stock),
        debug_panel_type=kind('Debug', 'updateDebugInfo'),
        consumables_panel_type=kind('Consumables', '_ConsumablesPanel__makeShellTooltip'),
        postmortem_panel_type=kind('Postmortem', '_populate'),
        player_messages_type=kind('Messages', 'showMessage'),
        arena_info_settings=O(SQUAD_RANGE_TO_SHOW=(2, 3)),
        predefined_hosts=O(_hosts=[], _makeHostItem=lambda *args: O(url=args[0])),
        offline_map_creator=O(SetActive=lambda active: None),
        chat_commands_type=kind('Commands', 'handleShortcutChatCommand'),
        radial_menu_type=kind('Radial', 'show'))


for name in ('gui', 'gui.mods', 'gui.mods.offline_lan_0922'):
    package = types.ModuleType(name)
    package.__path__ = []
    sys.modules[name] = package
module = imp.load_source('staged_compat', sys.argv[1])
tactical = imp.load_source('gui.mods.offline_lan_0922.tactical_radio', sys.argv[2])
sys.modules['BigWorld'] = O(target=stock)
created = []
bridge = types.ModuleType('offline_client_vehicle_hooks_native')
fields = ('_original_vehicle_getattribute', '_original_vehicle_setattr',
          '_original_vehicle_get_speed', '_original_avatar_get_speeds',
          '_original_avatar_getattribute', '_original_compound_getattribute')


def create(owner, index):
    created.append(index)
    original = getattr(owner, fields[index])
    def native_descriptor(*args):
        return original(*args)
    return native_descriptor


bridge.create = create
sys.modules[bridge.__name__] = bridge
for failure in (False, True):
    r = runtime()
    c = module.OfflineCompatibility(r)
    c._account_state = O()
    originals = (r.avatar_module.PlayerAvatar.autoAim,
                 r.vehicle_module.Vehicle.startVisual,
                 r.projectile_mover_module.getCollidableEntities,
                 r.custom_effect_manager_type.update,
                 r.postmortem_panel_type._populate)
    connect = r.bigworld.connect
    created[:] = []
    original_install_host = c._install_host
    if failure:
        def fail():
            original_install_host()
            raise RuntimeError('host installation failed')
        c._install_host = fail
    try:
        c.install()
    except RuntimeError:
        if not failure:
            raise
    else:
        assert not failure
        assert c._installed
        assert set(created) == set(range(6))
        assert c._original_projectile_segment_may_hit is stock
        assert c._projectile_segment_may_hit_code is stock.func_code
        assert c._original_effect_update.im_func is stock
        assert len(c._team_command_readers) == 2
        for owner, name, original, patched in c._team_command_readers:
            assert patched.func_code is original.func_code
            picker = patched.func_globals['BigWorld'].target
            assert picker._target_getter() is None
            picker._target_getter = lambda: O(id=92)
            assert picker._target_getter().id == 92
        # Both account and Avatar retirement remain token fenced, independently
        # of the deleted battle owner, including a duplicate stock callback.
        avatar = object.__new__(r.avatar_module.PlayerAvatar)
        avatar._offlineLANInitComplete = True
        avatar._offlineLANRetirePending = True
        assert avatar.onBecomeNonPlayer() == 'stock'
        assert avatar.onBecomeNonPlayer() is None
        assert not avatar._offlineLANRetirePending
        c._rollback_install()
    assert not c._installed
    assert not r.predefined_hosts._hosts
    assert r.bigworld.connect is connect
    assert originals == (r.avatar_module.PlayerAvatar.autoAim,
                 r.vehicle_module.Vehicle.startVisual,
                 r.projectile_mover_module.getCollidableEntities,
                 r.custom_effect_manager_type.update,
                 r.postmortem_panel_type._populate)
    c._rollback_install()
print('Staged CPython 2.7 install, captures, native descriptors, retirement and rollback passed')
