"""Execute #1513 a023c0's shader-selection setter; hook only string assignment."""
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_ECX
from stock_ballistics_oracle import Original, DATA


class Setter(Original):
    def __init__(self, executable):
        super().__init__(executable)
        self.owner = DATA + 0x10000
        self.hooks[0x613c20] = self.assign
        self.u.hook_add(unicorn.UC_HOOK_CODE, lambda u, a, s, d: self.assign(),
                        begin=0x613c20, end=0x613c20)
        self.selected = None

    def string(self, address, value, storage):
        self.u.mem_write(address, b'\0' * 24)
        raw = value.encode()
        if len(raw) >= 16:
            self.u.mem_write(storage, raw + b'\0')
            self.put(address, '<I', storage)
        else:
            self.u.mem_write(address, raw + b'\0')
        self.put(address + 16, '<II', len(raw), max(15, len(raw)))

    def assign(self):
        assert self.u.reg_read(UC_X86_REG_ECX) == self.owner + 0x1ac
        self.selected = bytes(self.u.mem_read(self.arg(1), self.arg(2))).decode()
        self.ret(self.owner + 0x1ac, 8)

    def run(self, mode, default, shimmer):
        self.string(0x1d45c24, default, DATA + 0x20000)
        self.string(0x1d45c44, shimmer, DATA + 0x21000)
        self.u.mem_write(self.owner, b'\0' * 0x300)
        self.call(0xa023c0, self.owner, [mode & 0xffffffff])
        assert struct.unpack('<I', self.u.mem_read(self.owner + 0xc, 4))[0] == mode & 0xffffffff
        assert bytes(self.u.mem_read(self.owner + 0xf0, 1)) == b'\1'
        return self.selected

    def draw_reload(self, dirty):
        self.calls = 0
        context, table, reload = DATA + 0x30000, DATA + 0x31000, DATA + 0x32000
        self.put(self.owner, '<I', table)
        self.put(table + 0x48, '<I', reload)
        self.u.mem_write(self.owner + 0xf0, bytes([dirty]))
        def updated():
            self.calls += 1
            self.ret(0, 0)
        self.hooks[reload] = updated
        self.u.hook_add(unicorn.UC_HOOK_CODE, lambda u,a,z,d: updated(), begin=reload, end=reload)
        self.u.hook_add(unicorn.UC_HOOK_CODE, lambda u,a,z,d: u.emu_stop(), begin=0xa0147a, end=0xa0147a)
        self.call(0xa01440, self.owner, [context, 0, 0, 0, 0, 0, 0])
        return self.calls


def audit(executable):
    owner = Setter(executable)
    count = 0
    for default, shimmer in [('default', 'shimmer'),
                              ('configured/custom/default.fx', 'configured/custom/shimmer.fx')]:
        for mode in (0, 1, 2, 3, 4, 5, 6, 7, 8, -1, 6, 6, 2):
            assert owner.run(mode, default, shimmer) == (shimmer if mode == 6 else default)
            count += 1
    # The true draw prefix dispatches dirty materials to vtable+0x48, whose
    # actual #1513 entry is updateMaterial (a039f0), not property-only updates.
    assert struct.unpack('<I', owner.u.mem_read(0x1501ed0 + 0x48, 4))[0] == 0xa039f0
    for dirty in (0, 1):
        probe = Setter(executable)
        assert probe.draw_reload(dirty) == dirty
        count += 1
    return count
