"""Compare the production projection basis with original x86 and real D3DX."""
import argparse
import json
from pathlib import Path
import random
import struct


def fixtures(destination):
    rng = random.Random(1513)
    rows = []
    for case in range(4000):
        start = [rng.uniform(-30, 30) for unused in range(3)]
        end = [value + rng.uniform(-15, 15) for value in start]
        size = [rng.uniform(.01, 12) for unused in range(2)]
        up = [rng.uniform(-1, 1) for unused in range(3)]
        if case % 37 == 0:
            end = start[:]
        if case % 43 == 0:
            up = [0., 1., 0.]
        rows.append(struct.pack('<11f', *(start + end + size + up)))
    Path(destination).write_bytes(b''.join(rows))


def audit(client, dll_path, inputs, dispatch, actual, output):
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_EIP
    game = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
    dll = pefile.PE(str(dll_path))
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    for image in (game, dll):
        data = image.get_memory_mapped_image()
        base = image.OPTIONAL_HEADER.ImageBase
        uc.mem_map(base, (len(data) + 4095) // 4096 * 4096)
        uc.mem_write(base, data)
    uc.mem_map(0x30000000, 0x20000)
    source, world, local, to_world, to_local, stack, finish = (
        0x30000000, 0x30001000, 0x30002000, 0x30003000, 0x30004000, 0x30008000, 0x30010000)
    exports = {symbol.name: dll.OPTIONAL_HEADER.ImageBase + symbol.address
               for symbol in dll.DIRECTORY_ENTRY_EXPORT.symbols if symbol.name}
    for library in game.DIRECTORY_ENTRY_IMPORT:
        for symbol in library.imports:
            if symbol.name in exports:
                uc.mem_write(symbol.address, struct.pack('<I', exports[symbol.name]))
    uc.mem_write(dll.OPTIONAL_HEADER.ImageBase + 0x1ca000, Path(dispatch).read_bytes())

    def security_cookie(uc, address, size, data):
        esp = uc.reg_read(UC_X86_REG_ESP)
        target = struct.unpack('<I', uc.mem_read(esp, 4))[0]
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, target)
    uc.hook_add(UC_HOOK_CODE, security_cookie, begin=0x13d809e, end=0x13d809e)
    # D3DX normalisation uses RSQRT hardware approximations whose mantissas
    # Unicorn does not reproduce. Record each production call on real Win32,
    # then require the original instructions to supply identical input bytes
    # at every boundary before using that actual native result.
    tape = Path(str(dispatch) + '.boundaries').read_bytes()
    native = {}
    cursor = 0
    while cursor < len(tape):
        kind, input_size, output_size = struct.unpack_from('<3I', tape, cursor)
        cursor += 12
        key = kind, tape[cursor:cursor + input_size]
        cursor += input_size
        value = tape[cursor:cursor + output_size]
        cursor += output_size
        assert key not in native or native[key] == value
        native[key] = value

    def math_boundary(uc, address, size, data):
        esp = uc.reg_read(UC_X86_REG_ESP)
        if address == 0x11c3bbe:
            target, output_pointer, first = struct.unpack('<3I', uc.mem_read(esp, 12))
            key = 0, bytes(uc.mem_read(first, 12))
            count = 2
        else:
            target, output_pointer, first, second = struct.unpack('<4I', uc.mem_read(esp, 16))
            key = (1, bytes(uc.mem_read(second, 64))) if address == 0x11c3bb8 else (
                2, bytes(uc.mem_read(first, 12)) + bytes(uc.mem_read(second, 64)))
            count = 3
        if key not in native:
            raise AssertionError('Original D3DX input differs from production row=' + str(current_case[0]) + ' ' + hex(address) + ' ' + key[1].hex())
        uc.mem_write(output_pointer, native[key])
        uc.reg_write(UC_X86_REG_EAX, output_pointer)
        uc.reg_write(UC_X86_REG_ESP, esp + (count + 1) * 4)
        uc.reg_write(UC_X86_REG_EIP, target)
    for address in (0x11c3bbe, 0x11c3bb8, 0x11c3bdc):
        uc.hook_add(UC_HOOK_CODE, math_boundary, begin=address, end=address)
    current_case = [0]
    expected = []
    blob = Path(inputs).read_bytes()
    for offset in range(0, len(blob), 44):
        current_case[0] = offset // 44
        uc.mem_write(source, blob[offset:offset + 44])
        uc.mem_write(stack, struct.pack('<9I', finish, source, source + 12, source + 24,
            source + 32, world, local, to_world, to_local))
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.emu_start(0x95a190, finish, count=100000)
        data = bytes(uc.mem_read(world, 96)) + bytes(uc.mem_read(local, 96))
        data += bytes(uc.mem_read(to_world, 64)) + bytes(uc.mem_read(to_local, 64))
        expected.append(list(struct.unpack('<80I', data)))
    if output:
        Path(output).with_suffix('.expected.bin').write_bytes(b''.join(struct.pack('<80I', *row) for row in expected))
    observed = [list(map(int, row.split())) for row in Path(actual).read_text().splitlines()]
    mismatches = [(index, [(slot, a, b) for slot, (a, b) in enumerate(zip(left, right)) if a != b][:5])
                  for index, (left, right) in enumerate(zip(observed, expected)) if left != right]
    if len(observed) != len(expected) or mismatches:
        raise AssertionError('Original sticker projection basis mismatch: ' + repr(mismatches[:5]))
    result = {'status': 'PASS', 'original': '0095a190', 'sdk': 'Actual Win32 D3DX9_43 boundaries with byte-identical original inputs',
              'cases': len(expected), 'floatFields': 80, 'bitMismatches': 0}
    if output:
        Path(output).write_text(json.dumps(result, indent=2) + '\n')
    return result



def segment_fixtures(destination):
    rng = random.Random(0x959e70)
    rows = []
    for case in range(4000):
        a,b,c = ([rng.uniform(-20,20) for axis in range(3)] for unused in range(3))
        start,end = ([rng.uniform(-30,30) for axis in range(3)] for unused in range(2))
        if case % 3 != 0:
            center = [(a[axis]+b[axis]+c[axis])/3 for axis in range(3)]
            offset = [rng.uniform(-10,10) for axis in range(3)]
            start = [center[axis]+offset[axis] for axis in range(3)]
            end = [center[axis]-offset[axis] for axis in range(3)]
        if case % 37 == 0: c = a[:]
        if case % 43 == 0: start = a[:]
        rows.append(struct.pack('<15f', *(start+end+a+b+c)))
    Path(destination).write_bytes(b''.join(rows))


def segment_audit(client, inputs, actual, output):
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
    game = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    data = game.get_memory_mapped_image()
    uc.mem_map(game.OPTIONAL_HEADER.ImageBase, (len(data) + 4095) // 4096 * 4096)
    uc.mem_write(game.OPTIONAL_HEADER.ImageBase, data)
    uc.mem_map(0x30000000, 0x20000)
    source, point, stack, finish = 0x30000000,0x30001000,0x30008000,0x30010000
    blob = Path(inputs).read_bytes()
    expected = []
    for offset in range(0,len(blob),60):
        uc.mem_write(source,blob[offset:offset+60])
        uc.mem_write(point,bytes(12))
        uc.mem_write(stack,struct.pack('<7I',finish,source,source+12,source+24,source+36,source+48,point))
        uc.reg_write(UC_X86_REG_ESP,stack)
        uc.emu_start(0x959e70,finish,count=10000)
        expected.append([uc.reg_read(UC_X86_REG_EAX)&255]+list(struct.unpack('<3I',uc.mem_read(point,12))))
    observed = [list(map(int,row.split())) for row in Path(actual).read_text().splitlines()]
    mismatches = [(index,left,right) for index,(left,right) in enumerate(zip(observed,expected)) if left != right]
    if len(observed) != len(expected) or mismatches:
        raise AssertionError('Original segment-triangle mismatch: '+repr(mismatches[:5]))
    result = {'status':'PASS','original':'00959e70','cases':len(expected),
              'hits':sum(row[0] for row in expected),'floatFields':3,'bitMismatches':0}
    if output: Path(output).write_text(json.dumps(result,indent=2)+'\n')
    return result


def atlas_audit(client, output):
    """Execute the original placement callback through its allocator and writer."""
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP
    game = pefile.PE(str(Path(client) / 'WorldOfTanks.exe'))
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    data = game.get_memory_mapped_image()
    uc.mem_map(game.OPTIONAL_HEADER.ImageBase, (len(data) + 4095) // 4096 * 4096)
    uc.mem_write(game.OPTIONAL_HEADER.ImageBase, data)
    uc.mem_map(0x30000000, 0x100000)
    owner, allocator, node, request, rect, stack, finish = (
        0x30000000, 0x30000100, 0x30000200, 0x30000300, 0x30000400, 0x30008000, 0x30010000)
    heap = [0x30020000]
    def allocate(uc, address, size, unused):
        esp = uc.reg_read(UC_X86_REG_ESP)
        target, count = struct.unpack('<2I', uc.mem_read(esp, 8))
        pointer = heap[0]
        heap[0] += (count + 15) & ~15
        assert heap[0] < 0x30100000
        uc.reg_write(UC_X86_REG_EAX, pointer)
        uc.reg_write(UC_X86_REG_ESP, esp + 4)
        uc.reg_write(UC_X86_REG_EIP, target)
    uc.hook_add(UC_HOOK_CODE, allocate, begin=0x13d7fe9, end=0x13d7fe9)
    rng = random.Random(1513)
    for case in range(4000):
        width, height = rng.choice((64,128,256,512)), rng.choice((64,128,256,512))
        skip = case % 3
        atlas_width, atlas_height = 2048 >> skip, 1024 >> skip
        # Offset the root to cover nonzero atlas origins as well as splitting.
        left, top = rng.randrange(0, 32), rng.randrange(0, 32)
        heap[0] = 0x30020000
        uc.mem_write(owner + 4, struct.pack('<I', allocator))
        uc.mem_write(allocator, struct.pack('<I', node))
        uc.mem_write(node, struct.pack('<2I4HI', 0,0,left,top,left+atlas_width-1,top+atlas_height-1,0))
        uc.mem_write(request + 8, struct.pack('<I', rect))
        uc.mem_write(rect, bytes(32))
        uc.mem_write(stack, struct.pack('<6I', finish, request, skip, width, height, 1))
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, owner)
        uc.emu_start(0x13f7a40, finish, count=100000)
        assert uc.reg_read(UC_X86_REG_EAX) & 255 == 1
        x0,y0,x1,y1 = struct.unpack('<4H', uc.mem_read(rect+10,8))
        assert (x0,y0) == (left,top)
        assert (x1-x0+1,y1-y0+1) == (width >> skip,height >> skip)
    result = {'status':'PASS','original':'013f7a40 -> 013f7bb0 -> SubTexture writer',
              'cases':4000,'rectConvention':'inclusive', 'sourceBordersAdded':0}
    if output: Path(output).write_text(json.dumps(result, indent=2)+'\n')
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    fixture = sub.add_parser('fixtures')
    fixture.add_argument('destination', type=Path)
    segment_fixture = sub.add_parser('segment-fixtures')
    segment_fixture.add_argument('destination', type=Path)
    segment = sub.add_parser('segment')
    for name in ('client', 'inputs', 'actual'): segment.add_argument(name, type=Path)
    segment.add_argument('--output', type=Path)
    atlas = sub.add_parser('atlas')
    atlas.add_argument('client', type=Path)
    atlas.add_argument('--output', type=Path)
    check = sub.add_parser('audit')
    for name in ('client', 'dll', 'inputs', 'dispatch', 'actual'):
        check.add_argument(name, type=Path)
    check.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.mode == 'fixtures':
        fixtures(args.destination)
    elif args.mode == 'segment-fixtures':
        segment_fixtures(args.destination)
    elif args.mode == 'segment':
        print(json.dumps(segment_audit(args.client,args.inputs,args.actual,args.output)))
    elif args.mode == 'atlas':
        print(json.dumps(atlas_audit(args.client, args.output)))
    else:
        print(json.dumps(audit(args.client, args.dll, args.inputs, args.dispatch, args.actual, args.output)))
