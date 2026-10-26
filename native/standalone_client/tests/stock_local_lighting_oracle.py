"""Execute the unchanged original volume-classification instruction ranges.

Inputs are the actual caller's scalar locals and light/cache fields. The spot
bounds are explicit inputs, with its native dirty bit clear; this isolates the
camera/cone classification from D3DX bounds construction and GPU submission.
"""
import struct


def original_light_groups(executable, cases):
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_EBP, UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_ESP
    from stock_tree_lod_oracle import OriginalTreeInstructionOracle
    oracle = OriginalTreeInstructionOracle(executable)
    emulator = oracle.emulator
    manager, cache, light, table = [oracle.allocate(size) for size in (256, 64, 512, 4)]
    frame = oracle.stack
    emulator.mem_write(manager + 0xa0, struct.pack('<I', cache))
    emulator.mem_write(table, struct.pack('<I', light))
    result = []
    for kind, position, direction, outer, cosine, sine, minimum, maximum, camera, near, instanced, shadow, enabled, force in cases:
        emulator.mem_write(light, bytes(512))
        emulator.mem_write(cache + 0x2d, bytes([enabled]))
        emulator.mem_write(frame - 0xb0, struct.pack('<3f', *camera))
        emulator.mem_write(frame - 0x7c, struct.pack('<f', near))
        if kind == 0:
            emulator.mem_write(light + 0x4c, struct.pack('<3f', *position))
            emulator.mem_write(light + 0x34, struct.pack('<f', outer))
            emulator.mem_write(light + 8, bytes([shadow, instanced]))
            emulator.mem_write(frame - 0x80, struct.pack('<I', table))
            emulator.mem_write(0x1d38599, bytes([force]))
            start, stop = 0x9658ba, 0x965953
        else:
            emulator.mem_write(light + 0x60, struct.pack('<3f', *position))
            emulator.mem_write(light + 0x6c, struct.pack('<3f', *direction))
            emulator.mem_write(light + 0x28, struct.pack('<f', outer))
            emulator.mem_write(light + 0x34, struct.pack('<2f', cosine, sine))
            emulator.mem_write(light + 0x148, struct.pack('<6f', *minimum, *maximum))
            emulator.mem_write(light + 8, bytes([instanced, 0, shadow]))
            emulator.mem_write(frame - 0x74, struct.pack('<I', table))
            emulator.mem_write(frame - 0x78, bytes(4))
            emulator.mem_write(0x1d3859a, bytes([force]))
            start, stop = 0x965a74, 0x965d42
        emulator.reg_write(UC_X86_REG_EBP, frame)
        emulator.reg_write(UC_X86_REG_EDI, manager)
        emulator.reg_write(UC_X86_REG_ESI, 0)
        emulator.reg_write(UC_X86_REG_ESP, frame - 4096)
        try:
            emulator.emu_start(start, stop, count=2000)
        except Exception as error:
            raise RuntimeError('Native volume range failed at %s: %s' %
                (hex(emulator.reg_read(UC_X86_REG_EIP)), repr((kind, camera, near, instanced, shadow, enabled, force)))) from error
        group = emulator.reg_read(UC_X86_REG_EAX)
        assert group < 3
        result.append(group)
    return result


def original_light_packets(executable, cases):
    """Run native load/multiply/classify/packet copies, stopping before push_back.

    Shadow callbacks are guarded off; quaternion/camera/light fields are real
    caller inputs. Unused padding is zeroed and is not asserted for PointLight.
    """
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_ESP
    from stock_tree_lod_oracle import OriginalTreeInstructionOracle
    oracle = OriginalTreeInstructionOracle(executable)
    emulator = oracle.emulator
    manager, cache, light, table = [oracle.allocate(size) for size in (256, 64, 512, 4)]
    frame = oracle.stack
    emulator.mem_write(manager + 0xa0, struct.pack('<I', cache))
    emulator.mem_write(manager + 0x2c, struct.pack('<I', table))
    emulator.mem_write(table, struct.pack('<I', light))
    result = []
    for kind, position, colour, intensity, inner, outer, cosine, tangent, orientation, camera in cases:
        emulator.mem_write(light, bytes(512))
        emulator.mem_write(frame - 4096, bytes(4096))
        emulator.mem_write(frame - 0xb0, struct.pack('<3f', *camera))
        emulator.mem_write(frame - 0x7c, struct.pack('<f', .1))
        emulator.mem_write(light + (0x4c if kind == 0 else 0x60), struct.pack('<3f', *position))
        emulator.mem_write(light + (0x38 if kind == 0 else 0x40), struct.pack('<4f', *colour))
        emulator.mem_write(light + (0x48 if kind == 0 else 0x30), struct.pack('<f', intensity))
        emulator.mem_write(light + (0x30 if kind == 0 else 0x24), struct.pack('<2f', inner, outer))
        emulator.reg_write(UC_X86_REG_EBP, frame)
        emulator.reg_write(UC_X86_REG_EDI, manager)
        emulator.reg_write(UC_X86_REG_ESI, 0)
        emulator.reg_write(UC_X86_REG_ESP, frame - 4096)
        if kind == 0:
            emulator.mem_write(frame - 0x80, struct.pack('<I', table))
            emulator.emu_start(0x965824, 0x965895, count=1000)
            emulator.emu_start(0x9658ba, 0x965993, count=2000)
            result.append(bytes(emulator.mem_read(frame - 0x150, 48)))
        else:
            emulator.mem_write(frame - 0x74, struct.pack('<I', table))
            emulator.mem_write(light + 0x34, struct.pack('<3f', cosine, .6, tangent))
            emulator.mem_write(light + 0x50, struct.pack('<4f', *orientation))
            emulator.reg_write(UC_X86_REG_EAX, table)
            emulator.emu_start(0x9659d7, 0x965a5f, count=1000)
            emulator.emu_start(0x965a74, 0x965d93, count=2000)
            result.append(bytes(emulator.mem_read(frame - 0x194, 64)))
    return result
