"""Compare actual FileEmitter birth instructions with the production producer.

Native allocation/resource calls and external D3DX/CRT operations are supplied
by the harness. All birth/shape/random/constructor instructions execute from
the original image; this does not prove Visual resource loading or GPU output.
"""
import pefile, struct, math, random, zipfile, subprocess, tempfile
from pathlib import Path
from unicorn import *
from unicorn.x86_const import *

def audit(client, probe, destination=None):
    p = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
    base = p.OPTIONAL_HEADER.ImageBase
    image = p.get_memory_mapped_image()
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(base, (len(image) + 4095) // 4096 * 4096)
    u.mem_write(base, image)
    u.mem_map(0, 4096)
    u.mem_map(805306368, 1048576)
    source, heap, stack, end, round_stub, circles = (805306368, 805371904, 805765120, 805830656, 805834752, 805838848)
    r = random.Random(1513)
    f = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]
    wi = lambda a, v: u.mem_write(a, struct.pack('<I', v))
    wf = lambda a, v: u.mem_write(a, struct.pack('<f', v))
    ri = lambda a: struct.unpack('<I', u.mem_read(a, 4))[0]
    rf = lambda a: struct.unpack('<f', u.mem_read(a, 4))[0]
    values = [f(r.randrange(32768) * f(1 / 32767)) for _ in range(4096)]
    circle = [[0.0, 0.0] for _ in range(80)]
    angle = 0.0
    for i in range(72):
        circle[i] = [f(math.cos(angle)), f(math.sin(angle))]
        angle = f(angle + f(0.08726646006))
    u.mem_write(34879136, struct.pack('<4096f', *values))
    u.mem_write(circles, b''.join((struct.pack('<2f', *v) for v in circle)))
    wi(27639652, circles)
    wi(27639648, 72)
    round_iat = next((s.address for d in p.DIRECTORY_ENTRY_IMPORT for s in d.imports if s.name == b'roundf'))
    wi(round_iat, round_stub)
    u.mem_write(round_stub, b'\xd9D$\x04\xc3')
    next_heap = heap
    made = []

    def ret(value=0, pop=0):
        esp = u.reg_read(UC_X86_REG_ESP)
        u.reg_write(UC_X86_REG_EAX, value & 4294967295)
        u.reg_write(UC_X86_REG_EIP, ri(esp))
        u.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def hook(uc, a, size, data):
        nonlocal next_heap
        esp = u.reg_read(UC_X86_REG_ESP)
        if a == 20807657:
            size = ri(esp + 4)
            ptr = next_heap
            next_heap += (size + 15) // 16 * 16
            u.mem_write(ptr, b'\x00' * size)
            if size == 376:
                made.append(ptr)
            ret(ptr)
        elif a == 20818973:
            target, value, size = struct.unpack('<III', u.mem_read(esp + 4, 12))
            u.mem_write(target, bytes([value & 255]) * size)
            ret(target)
        elif a == 16439984:
            ptr = u.reg_read(UC_X86_REG_ECX)
            wi(ptr + 304, 1)
            mat = [0.0] * 16
            for i in (0, 5, 10):
                mat[i] = 0.001
            mat[15] = 1.0
            u.mem_write(ptr + 308, struct.pack('<16f', *mat))
            ret(1, 4)
        elif a == 18627518:
            dst, src = struct.unpack('<II', u.mem_read(esp + 4, 8))
            x, y, z = struct.unpack('<3f', u.mem_read(src, 12))
            length = f(math.sqrt(f(f(x * x) + f(y * y) + f(z * z))))
            u.mem_write(dst, struct.pack('<3f', f(x / length), f(y / length), f(z / length)))
            ret(dst, 8)
        elif a == 18627548:
            dst, src, mat = struct.unpack('<III', u.mem_read(esp + 4, 12))
            v = struct.unpack('<3f', u.mem_read(src, 12))
            m = struct.unpack('<16f', u.mem_read(mat, 64))
            out = []
            for c in range(4):
                out.append(f(f(f(f(v[0] * m[c]) + f(v[1] * m[4 + c])) + f(v[2] * m[8 + c])) + m[12 + c]))
            u.mem_write(dst, struct.pack('<4f', *out))
            ret(dst, 12)
        elif a == round_stub:
            v = rf(esp + 4)
            wf(esp + 4, math.floor(v + 0.5) if v >= 0 else math.ceil(v - 0.5))
        elif a == 20807838:
            ret()
    for a in (20807657, 20818973, 16439984, 18627518, 18627548, round_stub, 20807838):
        u.hook_add(UC_HOOK_CODE, hook, begin=a, end=a)
    w = lambda d, o: struct.unpack_from('<I', d, o)[0]
    resources = []
    with zipfile.ZipFile(Path(client) / 'res/packages/particles.pkg') as z:
        for n in z.namelist():
            if not n.endswith('.bin') or not ('content_forward/PFX/' in n or 'content_deferred/PFX/' in n):
                continue
            d = z.read(n)
            pos = 80
            for _ in range(w(d, 12)):
                count = w(d, pos + 12)
                pos += 160
                for _ in range(count):
                    e = d[pos + 12:pos + 1520]
                    if w(e, 172) & 2048 and e[276:404].split(b'\x00')[0] == b'none':
                        resources.append(e)
                    pos += w(d, pos + 4)
    inputs = bytearray(struct.pack('<4096f', *values) + b''.join((struct.pack('<2f', *v) for v in circle)))
    expected = []
    for idx in range(4000):
        desc = bytearray(r.choice(resources))
        flags = w(desc, 8) & ~256
        if idx % 3 == 0:
            flags |= 256
            struct.pack_into('<f', desc, 116, float(idx % 6))
        struct.pack_into('<I', desc, 8, flags)
        struct.pack_into('<I', desc, 444, w(desc, 444) & ~4 | (4 if idx % 5 == 0 else 0))
        struct.pack_into('<I', desc, 120, idx % 7)
        struct.pack_into('<3f', desc, 104, *[f(r.uniform(-5, 5)) for _ in range(3)])
        cursor = r.randrange(4096)
        position = [f(r.uniform(-20, 20)) for _ in range(3)]
        direction = [f(r.uniform(-2, 2)) for _ in range(3)]
        u.mem_write(source, b'\x00' * 2600)
        u.mem_write(source + 232, bytes(desc))
        wi(source, 24438268)
        u.mem_write(source + 72, struct.pack('<3f', *position))
        u.mem_write(source + 96, bytes(desc[104:116]))
        u.mem_write(source + 1756, struct.pack('<3f', *direction))
        wi(source + 1768, w(desc, 628))
        u.mem_write(source + 1908, bytes(desc[412:420]))
        wi(source + 1916, w(desc, 404))
        wf(source + 1920, f(struct.unpack_from('<f', desc, 408)[0] - struct.unpack_from('<f', desc, 404)[0]))
        u.mem_write(source + 1924, bytes(desc[244:252]))
        explicit = idx % 2
        u.mem_write(source + 1964, struct.pack('<16f', 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1))
        wi(source + 2028, explicit)
        wi(34895520, cursor)
        wi(34900232, 0)
        wi(27639656, 4294967295)
        wi(34878064, 0)
        next_heap = heap
        made = []
        u.mem_write(stack, struct.pack('<I', end))
        u.reg_write(UC_X86_REG_ESP, stack)
        u.reg_write(UC_X86_REG_ECX, source)
        try:
            u.emu_start(16381456, end, count=100000)
        except Exception as e:
            print('FAIL', idx, hex(u.reg_read(UC_X86_REG_EIP)), e)
            raise
        state = [len(made)]
        for ptr in made:
            state += [ri(ptr + 32), ri(ptr + 244)]
            state += [ri(ptr + o) for o in (232, 236, 240, 248, 252, 256, 124, 112, 72, 76, 80, 120, 212, 216, 220)]
            state += [ri(ptr + 224) if ri(ptr + 32) & 2097152 else 0]
            state += [ri(ptr + o) for o in (56, 60, 64, 140, 108)]
        state += [ri(34895520), ri(34900232), struct.unpack('<i', u.mem_read(27639656, 4))[0]]
        expected.append(' '.join(map(str, state)))
        inputs += struct.pack('<I', cursor) + desc + struct.pack('<6fI', *position, *direction, explicit)
    destination = Path(destination) if destination else Path(tempfile.mkdtemp(prefix='wot-tps-birth-'))
    destination.mkdir(parents=True, exist_ok=True)
    (destination / 'birth-inputs.bin').write_bytes(inputs)
    (destination / 'birth-expected.txt').write_text('\n'.join(expected) + '\n')
    actual = subprocess.run([str(probe), str(destination / 'birth-inputs.bin')], text=True, capture_output=True, check=True).stdout.splitlines()
    errors = [(i, a, b) for i, (a, b) in enumerate(zip(actual, expected)) if a.rstrip() != b]
    if len(actual) != len(expected) or errors:
        raise AssertionError('Original FileEmitter birth mismatch: %r' % errors[:3])
    return {'originalFunctions': ['00f9f610', '00f9e010', '00fad7c0'], 'cases': len(actual), 'bitwiseMismatches': 0, 'status': 'PASS', 'externalBoundaries': ['Visual load', 'allocation', 'D3DXVec3Transform', 'D3DXVec3Normalize', 'roundf']}
if __name__ == '__main__':
    import argparse, json
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('client', type=Path)
    parser.add_argument('probe', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.client, args.probe, args.output)))
