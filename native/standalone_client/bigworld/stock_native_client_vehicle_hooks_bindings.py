"""Replace per-attribute mod closures at the existing reversible install edge."""


def compat(source):
    before = '        self._avatar_get_speeds_wrapper = avatar_get_speeds\n'
    after = '''        import offline_client_vehicle_hooks_native
        vehicle_getattribute = offline_client_vehicle_hooks_native.create(self, 0)
        vehicle_setattr = offline_client_vehicle_hooks_native.create(self, 1)
        vehicle_get_speed = offline_client_vehicle_hooks_native.create(self, 2)
        avatar_get_speeds = offline_client_vehicle_hooks_native.create(self, 3)
''' + before
    if source.count(before) != 1:
        raise ValueError('The pinned Vehicle descriptor install boundary changed')
    return source.replace(before, after, 1)
