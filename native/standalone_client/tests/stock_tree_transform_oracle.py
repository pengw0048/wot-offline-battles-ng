"""Capture original tree decomposeMatrix inputs at its genuine D3DX boundary.

No replacement quaternion algorithm is used. The native Win32 probe calls the
actual D3DX DLL on this captured matrix and compares BW BlendTransform outputs.
"""
import argparse
from pathlib import Path
import struct
import sys
import zipfile


def original_transform_records(executable, matrices):
    from unicorn import UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_ESP
    from stock_tree_lod_oracle import OriginalTreeInstructionOracle
    oracle = OriginalTreeInstructionOracle(executable)
    emulator = oracle.emulator
    world, quaternion, position, scale = [oracle.allocate(size) for size in (64, 16, 12, 12)]
    normalised = None

    def d3dx_boundary(uc, address, size, context):
        nonlocal normalised
        stack = uc.reg_read(UC_X86_REG_ESP)
        assert oracle.word(stack+4) == quaternion
        normalised = bytes(uc.mem_read(oracle.word(stack+8), 64))
        uc.emu_stop()

    emulator.hook_add(UC_HOOK_CODE, d3dx_boundary, begin=0x11c3c4e, end=0x11c3c4e)
    result = bytearray()
    for matrix in matrices:
        assert len(matrix) == 64
        emulator.mem_write(world, matrix)
        normalised = None
        oracle.call(0x11bdcd0, (world, quaternion, position, scale))
        assert normalised is not None
        result.extend(matrix+normalised+bytes(emulator.mem_read(position, 12))+bytes(emulator.mem_read(scale, 12)))
    return bytes(result)


def prepare(client, destination):
    root = Path(__file__).resolve().parents[3]
    sys.path.insert(0, str(root / 'tools'))
    from space_bin_0922 import CompiledSpace
    matrices = []
    maps = 0
    for package in sorted((client / 'res/packages').glob('*.pkg')):
        with zipfile.ZipFile(package) as archive:
            for name in archive.namelist():
                if not name.endswith('/space.bin'):
                    continue
                data = CompiledSpace(archive.read(name)).section_data('SpTr')
                stride, count = struct.unpack_from('<II', data)
                assert stride == 80 and len(data) == 36+count*80
                matrices.extend(data[8+i*80:72+i*80] for i in range(count))
                maps += 1
    assert matrices
    actual = len(matrices)
    # Reflected X/Y/Z inputs discriminate the actual flip-Z contract from
    # D3DXMatrixDecompose, including signed zero and negative scale components.
    for x in (-1., 1.):
        for y in (-2., 2.):
            for z in (-.5, .5):
                matrices.append(struct.pack('<16f', x, 0, 0, 0, 0, y, 0, 0,
                                            0, 0, z, 0, -1000.5, 37.25, 700.5, 1))
    records = original_transform_records(client / 'WorldOfTanks.exe', matrices)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(records)
    print('Original decomposeMatrix instruction fixture: %d maps, %d actual transforms, %d reflection edges, %d bytes' %
          (maps, actual, len(matrices)-actual, len(records)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('client', type=Path)
    parser.add_argument('output', type=Path)
    arguments = parser.parse_args()
    prepare(arguments.client, arguments.output)
