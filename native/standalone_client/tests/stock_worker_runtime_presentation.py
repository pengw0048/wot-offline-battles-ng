"""Embedded CPython 2.7 driver: adapted pinned WorkerPresentation over the production native windows."""
from __future__ import print_function

import imp
import os
import sys

import fake_desktop
import offline_instance_guard_native as native

module = imp.load_source('worker_presentation', os.environ['WORKER_PRESENTATION_MODULE'])
WORKER = os.environ.get('OFFLINE_LAN_0922_CLIENT_MODE') == 'simulation_worker'


class Wwise(object):
    def __init__(self):
        self.volumes = []

    def WW_setMasterVolume(self, volume):
        self.volumes.append(volume)


class SoundGroups(object):
    _SoundGroups__masterVolume = 0.65


def report(name, value):
    print('%s %r' % (name, value))
    sys.stdout.flush()


def presentation():
    return module.WorkerPresentation(runtime=(native, Wwise(), SoundGroups()), environ={})


def activate(name, owner):
    try:
        report(name, owner.activate())
    except module.WorkerPresentationError as error:
        report(name, 'refused: %s' % error)


report('confirm-without-window', native.confirm_hidden_worker_window())
# Another process's visible and marked windows never count as this worker's.
fake_desktop.create(0, 1, 0)
fake_desktop.create(0, 0, 1)
report('confirm-foreign-only', native.confirm_hidden_worker_window())
# BWWinMain's never-shown window, marked with its own handle.
main = fake_desktop.create(1, 0, 1)
report('hide-never-shown', native.hide_process_windows())
report('confirm-never-shown', native.confirm_hidden_worker_window())
report('confirm-repeated', native.confirm_hidden_worker_window())

owner = presentation()
activate('activate-never-shown', owner)
report('window-hidden-flag', owner._window_hidden)
activate('activate-again', owner)
report('deactivate-restore', owner.deactivate())
report('main-visible-after-restore', fake_desktop.visible(main))
report('shown-after-restore', fake_desktop.shown())

if WORKER:
    owner = presentation()
    activate('activate-for-exit', owner)
    report('deactivate-exit', owner.deactivate(restore=False))
    # A worker window that became visible is hidden by the normal path and
    # stays hidden through Python restoration.
    fake_desktop.set_visible(main, 1)
    report('confirm-visible', native.confirm_hidden_worker_window())
    owner = presentation()
    activate('activate-visible', owner)
    report('window-hidden-flag-visible', owner._window_hidden)
    report('deactivate-visible', owner.deactivate())
    report('main-visible-after-visible-restore', fake_desktop.visible(main))
    report('shown-after-visible-restore', fake_desktop.shown())
    fake_desktop.lock(1, 0)
    report('confirm-busy', native.confirm_hidden_worker_window())
    # A native hide error is never replaced by the hidden-window proof.
    activate('activate-hide-busy', presentation())
    fake_desktop.lock(0, 0)
    fake_desktop.destroy(main)
    report('confirm-destroyed', native.confirm_hidden_worker_window())
    activate('activate-destroyed', presentation())
    # Only BWWinMain's self-identifying mark on exactly one window counts.
    stray = fake_desktop.create(1, 0, 2)
    report('confirm-with-foreign-mark-value', native.confirm_hidden_worker_window())
    fake_desktop.destroy(stray)
    fake_desktop.create(1, 0, 1)
    fake_desktop.create(1, 0, 1)
    report('confirm-two-marked', native.confirm_hidden_worker_window())
    fake_desktop.lock(0, 1)
    report('confirm-closed', native.confirm_hidden_worker_window())
else:
    # The player's ordinary visible window keeps the original hide/show contract.
    fake_desktop.set_visible(main, 1)
    owner = presentation()
    activate('activate-visible', owner)
    report('main-visible-after-hide', fake_desktop.visible(main))
    report('deactivate-visible', owner.deactivate())
    report('main-visible-after-visible-restore', fake_desktop.visible(main))
    report('shown-after-visible-restore', fake_desktop.shown())
