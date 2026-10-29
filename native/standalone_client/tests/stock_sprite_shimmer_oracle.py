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


class MaterialProperties(Original):
    def __init__(self, executable):
        super().__init__(executable)
        self.owner, self.material, self.table = DATA + 0x10000, DATA + 0x11000, DATA + 0x12000
        for address, kind in ((0x96de30, 'texture'), (0x96dec0, 'float'), (0x96de90, 'int')):
            self.u.hook_add(unicorn.UC_HOOK_CODE,
                            lambda u, a, s, d, k=kind: self.property(k), begin=address, end=address)

    def property(self, kind):
        assert self.u.reg_read(UC_X86_REG_ECX) == self.material and self.arg(3) == 1
        data, length = struct.unpack('<II', self.u.mem_read(self.arg(1), 8))
        name = bytes(self.u.mem_read(data, length)).decode()
        value = self.farg(2) if kind == 'float' else self.arg(2)
        if kind == 'texture': value = struct.unpack('<I', self.u.mem_read(value, 4))[0]
        self.writes.append((name, value))
        self.ret(1, 12)

    def run(self, mode, dirty, force, texture, material):
        self.u.mem_write(self.owner, b'\0' * 0x300)
        self.put(self.owner, '<I', self.table)
        self.put(self.table + 0x54, '<I', 0xa03570)
        self.put(self.owner + 0xc, '<I', mode)
        self.put(self.owner + 0xf0, '<B', dirty)
        self.put(self.owner + 0x1c4, '<I', 123 if texture else 0)
        self.put(self.owner + 0x21c, '<I', self.material if material else 0)
        self.put(self.owner + 0x220, '<6f', *range(1, 7))
        self.writes = []
        self.call(0xa03730, self.owner, [force])
        return self.writes


def audit_properties(executable):
    owner = MaterialProperties(executable)
    count = 0
    fields = ('softDepthRange', 'softFalloffPower', 'softDepthOffset',
              'nearFadeCutoff', 'nearFadeStart', 'nearFadeFalloffPower')
    for mode in range(8):
        for dirty in (0, 1):
            for force in (0, 1):
                for texture in (0, 1):
                    for material in (0, 1):
                        expected = []
                        if material and (dirty or force):
                            if texture: expected.append(('diffuseMap', 123))
                            if mode != 6:
                                if dirty:
                                    expected += [('srcBlend', (5, 2, 5, 3, 3, 2, 0, 2)[mode]),
                                                 ('destBlend', (2, 6, 6, 4, 4, 1, 0, 1)[mode])]
                                    if mode == 7: expected.append(('alphaReference', 128))
                                expected += list(zip(fields, range(1, 7)))
                        assert owner.run(mode, dirty, force, texture, material) == expected
                        count += 1
    return count


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
