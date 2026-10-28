"""Run the #1513 compiled-space light tick and its shadow virtual dispatch."""
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_ECX
import stock_ballistics_oracle as base


def audit(executable):
    original = base.Original(executable)
    parent, owner, light = base.DATA + 0x10000, base.DATA + 0x10100, base.DATA + 0x10200
    output = base.SENTINEL + 0x100
    providers = [base.DATA + 0x11000 + 0x100 * index for index in range(4)]
    sampled = []
    values = ((8., 3., 0., 0.), (255., 127.5, 63.75, 255.),
              (11., 13., 17., 19.), (23., 29., 31., 37.))
    def sample():
        index = providers.index(original.u.reg_read(UC_X86_REG_ECX))
        sampled.append(index)
        original.put(original.arg(1), '<4f', *values[index])
        original.ret(0, 4)
    original.u.hook_add(unicorn.UC_HOOK_CODE, lambda *unused: sample(), begin=output, end=output)
    for provider in providers:
        original.put(provider, '<I', provider + 32)
        original.put(provider + 40, '<I', output)
    original.put(parent + 0xc, '<I', 0x1504620)
    original.put(parent + 0x14, '<5I', *providers, owner)
    original.put(owner, '<I', 0x14bc83c)
    original.put(owner + 0xc, '<2I', parent, light)
    original.call(0x97b7d0, light, [])
    assert bytes(original.u.mem_read(light + 8, 1)) == b'\0'
    original.put(light + 0x38, '<4f', .1, .2, .3, 1.)
    original.call(0x6cad60, owner + 4, [base.bits(.1)])
    assert sampled == [0, 1], sampled
    colour = original.floats(light + 0x38, 4)
    assert all(abs(a-b) < 1e-6 for a, b in zip(colour, (1., .5, .25, 1.))), colour
    original.put(parent + 0x14, '<2I', 0, 0)
    sampled[:] = []
    original.call(0x6cad60, owner + 4, [base.bits(.2)])
    assert not sampled and original.floats(light + 0x38, 4) == colour
    # These are the actual PyOmniLight forwarding bodies, then the original
    # embodiment's native flag setter/getter, not a substitute light owner.
    flags = []
    for value in (0, 1, 0, 1):
        original.call(0xa1c140, parent + 0xc, [value])
        flags.append(original.call(0xa1c150, parent + 0xc, []) & 255)
        assert bytes(original.u.mem_read(light + 8, 1)) == bytes([value])
    assert flags == [0, 1, 0, 1]
    return {'sampled_fields': ('bounds', 'shader'), 'retained_fields': ('colorAnimator', 'multiplierAnimator'),
            'shadow_flags': flags, 'colour': colour}
