"""Read-only x86 oracle for #1513's existing-property material binder.

The original binder and StringRef comparison execute unchanged. Only the
external property-descriptor callback, Interlocked refcounts and destructor
receipt are supplied by the harness. No game process or GPU is started.
"""
from pathlib import Path
import struct


def audit(executable, target_name="g_baseColor"):
    import pefile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP,
                                   UC_X86_REG_ECX, UC_X86_REG_EIP,
                                   UC_X86_REG_ESP)

    pe = pefile.PE(str(Path(executable)))
    assert pe.FILE_HEADER.Machine == 0x14c, 'Expected the exact #1513 x86 executable'
    base = pe.OPTIONAL_HEADER.ImageBase
    assert pe.get_data(0x96d8d0 - base, 6) == bytes.fromhex('55 8b ec 83 ec 34')
    if target_name == "vTransform_diffuse":
        # af07a0's track owner calls this same binder and immediately overwrites
        # EAX for refcount work: a missing slot's false result is ignored.
        assert pe.get_data(0xaf094f - base, 0x15) == bytes.fromhex(
            'e8 7c cf e7 ff 51 8b c4 8d 5f 04 53 89 38 ff d6 e9 bf 00 00 00')
        name_address = struct.unpack('<I', pe.get_data(0x1a2082c - base, 4))[0]
        assert pe.get_string_at_rva(name_address - base) == target_name.encode('ascii')
    image = pe.get_memory_mapped_image()
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (len(image) + 4095) & ~4095)
    machine.mem_write(base, image)
    memory = 0x30000000
    machine.mem_map(memory, 0x20000)
    owner, effect, dx_effect, dx_table = memory, memory + 0x100, memory + 0x200, memory + 0x300
    records, target, replacement = memory + 0x400, memory + 0x500, memory + 0x600
    old_a, old_b, new = memory + 0x700, memory + 0x800, memory + 0x900
    prop_table, names, stack = memory + 0xa00, memory + 0x1000, memory + 0x10000
    describe, increment, decrement, destroy, stop = (memory + at for at in (0x18000, 0x18100, 0x18200, 0x18300, 0x19000))
    state = {}

    def read(address):
        return struct.unpack('<I', machine.mem_read(address, 4))[0]

    def write(address, value):
        machine.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def returned(value, arguments):
        sp = machine.reg_read(UC_X86_REG_ESP)
        destination = read(sp)
        machine.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        machine.reg_write(UC_X86_REG_ESP, sp + 4 + arguments)
        machine.reg_write(UC_X86_REG_EIP, destination)

    def code(unused_machine, address, unused_size, unused):
        if address == stop:
            machine.emu_stop()
        elif address == describe:
            sp = machine.reg_read(UC_X86_REG_ESP)
            assert machine.reg_read(UC_X86_REG_ECX) == dx_effect
            handle, output = read(sp + 4), read(sp + 8)
            state['queries'].append(handle)
            if handle in state['bad_descriptors']:
                returned(0x80004005, 8)
            else:
                write(output, state['names'][handle])
                returned(0, 8)
        elif address in (increment, decrement):
            ref = read(machine.reg_read(UC_X86_REG_ESP) + 4)
            assert ref in (old_a + 4, old_b + 4, new + 4)
            value = read(ref) + (1 if address == increment else -1)
            assert value >= 0
            write(ref, value)
            state['ref_events'].append((address == increment, ref))
            returned(value, 4)
        elif address == destroy:
            obj = machine.reg_read(UC_X86_REG_ECX)
            assert obj in (old_a, old_b, new)
            assert read(obj + 4) == 0
            assert read(machine.reg_read(UC_X86_REG_ESP) + 4) == 1
            state['destroyed'].append(obj)
            returned(obj, 4)
        else:
            # Absence may not escape into a logging/creation/refresh path. The
            # unchanged StringRef equality routine is the only nested code.
            assert 0x96d8d0 <= address <= 0x96d99d or 0x686680 <= address <= 0x686710, hex(address)

    machine.hook_add(UC_HOOK_CODE, code)
    # The fake property vtable exposes only destruction, so any original-type
    # query (getVector/be) would fail the instruction hook. The binder replaces
    # an existing slot without imposing a type requirement on its old pointer.
    # Name, records, new property, expected records/refcounts/result/queries,
    # optional absent owner/effect or descriptor failure.
    cases = (
        ('absent', [('unrelated', old_a)], new, [old_a], (2, 3, 2), False, [1], {}),
        ('prefix_is_not_equal', [('g_baseColorExtra', old_a), ('g_baseColo', old_b)], new,
         [old_a, old_b], (2, 3, 2), False, [1, 2], {}),
        ('empty', [], new, [], (2, 3, 2), False, [], {}),
        ('no_owner', [('g_baseColor', old_a)], new, [old_a], (2, 3, 2), False, [], {'owner': False}),
        ('no_effect', [('g_baseColor', old_a)], new, [old_a], (2, 3, 2), False, [], {'effect': False}),
        ('replace', [('g_baseColor', old_a)], new, [new], (1, 3, 3), True, [1], {}),
        ('same_pointer', [('g_baseColor', new)], new, [new], (2, 3, 2), True, [1], {}),
        ('empty_slot', [('g_baseColor', 0)], new, [new], (2, 3, 3), True, [1], {}),
        ('clear_slot', [('g_baseColor', old_a)], 0, [0], (1, 3, 2), True, [1], {}),
        ('release_last_reference', [('g_baseColor', old_a)], new, [new], (0, 3, 3), True, [1], {'old_count': 1}),
        ('first_duplicate', [('g_baseColor', old_a), ('g_baseColor', old_b)], new,
         [new, old_b], (1, 3, 3), True, [1], {}),
        ('failed_descriptor', [('g_baseColor', old_a)], new, [old_a], (2, 3, 2), False, [1], {'bad': {1}}),
    )
    for label, entries, value, expected, refs, success, queries, options in cases:
        machine.mem_write(memory, bytes(0x20000))
        write(0x149139c, increment)
        write(0x14913a0, decrement)
        write(dx_table + 8, describe)
        write(prop_table, destroy)
        write(owner + 8, 7)  # Matching effect-generation receipt: no refresh.
        write(owner + 0xc, effect if options.get('owner', True) else 0)
        write(effect + 0xc, dx_effect if options.get('effect', True) else 0)
        write(effect + 0x44, 7)
        write(dx_effect, dx_table)
        write(owner + 0x10, records)
        write(owner + 0x14, records + len(entries) * 8)
        for obj, count in ((old_a, options.get('old_count', 2)), (old_b, 3), (new, 2)):
            write(obj, prop_table)
            write(obj + 4, count)
        state = {'names': {}, 'queries': [], 'ref_events': [], 'destroyed': [],
                 'bad_descriptors': options.get('bad', set())}
        for index, (name, pointer) in enumerate(entries):
            name = target_name[:-1] if name == 'g_baseColo' else name.replace('g_baseColor', target_name)
            text = names + (index + 1) * 0x100
            machine.mem_write(text, name.encode('ascii') + b'\0')
            state['names'][index + 1] = text
            write(records + index * 8, index + 1)
            write(records + index * 8 + 4, pointer)
        machine.mem_write(names, target_name.encode('ascii') + b'\0')
        write(target, names)
        write(target + 4, len(target_name))
        write(replacement, value)
        machine.mem_write(stack, struct.pack('<III', stop, target, replacement))
        machine.reg_write(UC_X86_REG_ECX, owner)
        machine.reg_write(UC_X86_REG_EBP, 0)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.emu_start(0x96d8d0, stop, count=10000)
        assert machine.reg_read(UC_X86_REG_EIP) == stop, label
        assert bool(machine.reg_read(UC_X86_REG_EAX) & 255) == success, label
        assert [read(records + index * 8 + 4) for index in range(len(entries))] == expected, label
        assert tuple(read(obj + 4) for obj in (old_a, old_b, new)) == refs, label
        assert state['queries'] == queries, label
        assert state['destroyed'] == ([old_a] if label == 'release_last_reference' else []), label
        if not success or label == 'same_pointer':
            assert not state['ref_events'], label
    return len(cases)
