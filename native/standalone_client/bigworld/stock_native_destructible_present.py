"""Original #1513 effect resources at the native destruction presentation seam.

This module owns no destruction, collision, body simulation or admission law.
"""
from __future__ import print_function

import random
import AreaDestructibles
import BigWorld
import Math
import WWISE
from helpers import isPlayerAccount


def emit(resource, category, position, direction, scale, havok):
    player = BigWorld.player()
    if player is None or isPlayerAccount():
        return
    if isinstance(resource, basestring):
        if not resource or resource == 'none':
            return
        variants = AreaDestructibles.g_cache._getEffect(resource, category, False)
    else:
        variants = resource
    if variants is None:
        return
    effect = random.choice(variants)
    keywords = dict(dir=Math.Vector3(*direction), scale=scale)
    if havok is not None:
        keywords['havokEnabled'] = havok
    player.terrainEffects.addNew(Math.Vector3(*position), effect.effectsList,
                                 effect.keyPoints, None, **keywords)


def stop_lifetime(space, chunk, item, module):
    manager = AreaDestructibles.g_destructiblesManager
    if manager is None or manager.getSpaceID() != space:
        return
    code = AreaDestructibles._encodeModule(chunk, item, module)
    effects = manager._DestructiblesManager__lifetimeEffects
    effect = effects.pop(code, None)
    player = BigWorld.player()
    if effect is not None and player is not None and not isPlayerAccount():
        player.terrainEffects.stop(effect)


def remove_light_sound(space, chunk, item):
    WWISE.WG_lightSoundRemove(space, chunk, item)
