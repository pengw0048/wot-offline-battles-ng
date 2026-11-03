"""Keep stock music leases bounded by their battle and results owners."""

_patches = []


def install(music_module, results_type):
    if _patches:
        return
    controller_type = music_module.MusicController
    original_leave = controller_type.onLeaveArena
    original_results_stop = results_type.stop

    def leave_arena(controller):
        try:
            return original_leave(controller)
        finally:
            # Stock onLeaveArena only unsubscribes from arena periods. NG's
            # native playing IDs remain live until their owner stops them.
            controller.stop()

    def stop_results(environment):
        try:
            event = environment.getMusicEvent()
            controller = music_module.g_musicController
            # EmptySound also reaches this path after natural completion.
            # Match the ID before stopping, so a stale results window cannot
            # stop a newer battle, lobby track, or different results track.
            if controller is not None and not event.isEmpty():
                event_id = event._soundEventID
                active = controller._getSoundEventById(event_id)
                if active.getEventId() == event_id:
                    active.destroy()
        finally:
            # BattleResultsEnv.stop clears _music before its base cleanup;
            # release the playing lease above while its identity still exists.
            original_results_stop(environment)

    for owner, name, replacement in (
            (controller_type, 'onLeaveArena', leave_arena),
            (results_type, 'stop', stop_results)):
        _patches.append((owner, name, getattr(owner, name), replacement))
        setattr(owner, name, replacement)


def uninstall():
    for owner, name, original, replacement in reversed(_patches):
        if getattr(owner, name) is replacement:
            setattr(owner, name, original)
    del _patches[:]
