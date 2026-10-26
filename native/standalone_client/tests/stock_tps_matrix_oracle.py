"""Execute original Visual matrix and original x86 D3DX math instructions.

The supplied dispatch snapshot comes from the generated actual Win32 probe
using the same DLL. Only memset and the game security-cookie return are
supplied by the harness; matrix arithmetic and trig are never reimplemented.
"""
import pefile, struct, json
from pathlib import Path
from unicorn import *
from unicorn.x86_const import *

def audit(client, d3dx, inputs_path, dispatch_path, actual_path, destination=None):
    game = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
    dll = pefile.PE(str(d3dx))
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    for p in (game, dll):
        data = p.get_memory_mapped_image()
        base = p.OPTIONAL_HEADER.ImageBase
        u.mem_map(base, (len(data) + 4095) // 4096 * 4096)
        u.mem_write(base, data)
    u.mem_map(0, 4096)
    u.mem_map(805306368, 1048576)
    owner, world, stack, end = (805306368, 805310464, 805765120, 805830656)
    exports = {s.name.decode(): dll.OPTIONAL_HEADER.ImageBase + s.address for s in dll.DIRECTORY_ENTRY_EXPORT.symbols if s.name}
    dispatch = json.loads(Path(dispatch_path).read_text())
    u.mem_write(dll.OPTIONAL_HEADER.ImageBase + 1875968, Path(str(dispatch_path) + '.bin').read_bytes())
    for n, o in [('D3DXMatrixMultiply', 1876084), ('D3DXMatrixRotationYawPitchRoll', 1876236), ('D3DXMatrixScaling', 1876208)]:
        u.mem_write(dll.OPTIONAL_HEADER.ImageBase + o, struct.pack('<I', dll.OPTIONAL_HEADER.ImageBase + dispatch[n]))
    for d in game.DIRECTORY_ENTRY_IMPORT:
        for s in d.imports:
            if s.name and s.name.decode() in dispatch:
                u.mem_write(s.address, struct.pack('<I', exports[s.name.decode()]))

    def cookie(uc, a, size, data):
        if a == 20818973:
            sp = uc.reg_read(UC_X86_REG_ESP)
            dst, val, n = struct.unpack('<III', uc.mem_read(sp + 4, 12))
            uc.mem_write(dst, bytes([val & 255]) * n)
            uc.reg_write(UC_X86_REG_EAX, dst)
        sp = uc.reg_read(UC_X86_REG_ESP)
        target = struct.unpack('<I', uc.mem_read(sp, 4))[0]
        uc.reg_write(UC_X86_REG_ESP, sp + 4)
        uc.reg_write(UC_X86_REG_EIP, target)
    u.hook_add(UC_HOOK_CODE, cookie, begin=20807838, end=20807838)
    u.hook_add(UC_HOOK_CODE, cookie, begin=20818973, end=20818973)
    rows = []
    data = Path(inputs_path).read_bytes()
    for i in range(0, len(data), 80):
        record = data[i:i + 80]
        u.mem_write(owner, b'\x00' * 376)
        u.mem_write(owner + 140, record[12:16])
        u.mem_write(owner + 300, struct.pack('<I', world))
        u.mem_write(world, record[16:80])
        u.mem_write(stack, struct.pack('<I', end) + record[:12])
        u.reg_write(UC_X86_REG_ESP, stack)
        u.reg_write(UC_X86_REG_ECX, owner)
        try:
            u.emu_start(16438688, end, count=1000000)
        except Exception as error:
            raise RuntimeError('Original matrix instruction failed at row %d EIP %x' % (i // 80, u.reg_read(UC_X86_REG_EIP))) from error
        rows.append(' '.join(map(str, struct.unpack('<16I', u.mem_read(owner + 308, 64)))))
    if destination:
        destination = Path(destination)
        destination.mkdir(parents=True, exist_ok=True)
        (destination / 'matrix-expected.txt').write_text('\n'.join(rows) + '\n')
    a = Path(actual_path).read_text().splitlines()
    bad = [(i, x, y) for i, (x, y) in enumerate(zip(a, rows)) if x.split() != y.split()]
    if len(a) != len(rows) or bad:
        raise AssertionError('Original Visual matrix mismatch: %r' % bad[:3])
    return {'originalFunctions': ['00fad5a0', '00faa2e0'], 'sdk': 'Original x86 D3DX9_43 math instructions', 'cases': len(a), 'bitwiseMismatches': 0, 'status': 'PASS'}
if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('client', type=Path)
    parser.add_argument('d3dx', type=Path)
    parser.add_argument('inputs', type=Path)
    parser.add_argument('dispatch', type=Path)
    parser.add_argument('actual', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.client, args.d3dx, args.inputs, args.dispatch, args.actual, args.output)))
