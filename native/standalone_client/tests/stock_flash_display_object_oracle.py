"""Run #1513 GFx setter dispatch and both 2D matrix descriptor wrappers unchanged.

The native Python descriptor, Python/GFx conversion and GFx matrix virtual calls
are explicit boundaries. Attribute routing and matrix packing execute original
x86 instructions, including the success early return and failure error reset.
"""
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP
from stock_flash_input_mode_oracle import OriginalFlashInputMode, OBJECT, VALUE, MARKS, STACK, SENTINEL


class OriginalDisplayObject(OriginalFlashInputMode):
    def __init__(self, executable):
        super().__init__(executable)
        self.events = []
        self.hooks.update({
            0x6a89a0: self.native_set,
            0xab1760: self.convert,
            0xfc3650: self.clear_error,
            0xab11e0: self.bind_script,
            0x13dac1d: self.zero_memory,
            0x13d809e: lambda: self._return(self.u.reg_read(UC_X86_REG_EAX)),
            0x68ea70: self.python_matrix,
            0x690130: self.read_python_matrix,
            MARKS + 0x10000: self.get_matrix,
            MARKS + 0x10020: self.set_matrix,
        })
        for address in (0x6a89a0, 0xab1760, 0xab11e0, 0x13dac1d, 0x13d809e,
                        0x68ea70, 0x690130, MARKS + 0x10000, MARKS + 0x10020):
            self.u.hook_add(unicorn.UC_HOOK_CODE, self._hook, begin=address, end=address)

    def clear_error(self):
        self.events.append('clear')
        self._clear()

    def native_set(self):
        assert self.u.reg_read(UC_X86_REG_ECX) == OBJECT + 4
        self.events.append('native')
        self.error = not self.native_result
        self._return(int(self.native_result), 8)

    def convert(self):
        self.events.append('convert')
        assert not self.error
        self.u.emu_stop()

    def bind_script(self):
        self.events.append('script')
        self._return(1, 4)

    def route(self, name, native_result):
        self.u.mem_write(OBJECT, b'\0' * 0x1000)
        string, name_ref, value_ref = OBJECT + 0x300, OBJECT + 0x400, OBJECT + 0x500
        self.u.mem_write(string + 0x14, name.encode('ascii') + b'\0')
        self.u.mem_write(name_ref, struct.pack('<I', string))
        self.u.mem_write(value_ref, struct.pack('<I', VALUE))
        self.events, self.error, self.native_result = [], False, native_result
        self.u.reg_write(UC_X86_REG_ESP, STACK + 0x80000)
        self.u.reg_write(UC_X86_REG_ECX, OBJECT)
        self.u.mem_write(STACK + 0x80000, struct.pack('<III', SENTINEL, name_ref, value_ref))
        self.u.emu_start(0xab2f40, SENTINEL + 1, count=100000)
        if native_result or name == 'script':
            assert self.u.reg_read(UC_X86_REG_EIP) == SENTINEL
            assert self.u.reg_read(UC_X86_REG_EAX) & 0xff == 1
        else:
            assert self.u.reg_read(UC_X86_REG_EIP) == 0xab1760
        return self.events

    def zero_memory(self):
        self.u.mem_write(self._arg(1), bytes([self._arg(2) & 255]) * self._arg(3))
        self._return(self._arg(1))

    def python_matrix(self):
        self.matrix_out = struct.unpack('<16f', self.u.mem_read(self._arg(1), 64))
        self._return(VALUE)

    def read_python_matrix(self):
        self.u.mem_write(self._arg(2), struct.pack('<16f', *self.matrix_in))
        self._return(0 if self.valid_matrix else -1)

    def get_matrix(self):
        self.u.mem_write(self._arg(2), struct.pack('<8f', *self.gfx_in))
        self._return(int(self.readable), 8)

    def set_matrix(self):
        self.gfx_out = struct.unpack('<8f', self.u.mem_read(self._arg(2), 32))
        self._return(1, 8)

    def prepare_matrix(self):
        interface, vtable = OBJECT + 0x600, OBJECT + 0x700
        self.u.mem_write(OBJECT, b'\0' * 0x1000)
        self.u.mem_write(OBJECT + 0x38, struct.pack('<I', interface))
        self.u.mem_write(interface, struct.pack('<I', vtable))
        self.u.mem_write(vtable + 0x78, struct.pack('<II', MARKS + 0x10000, MARKS + 0x10020))

    def get(self, values, readable=True):
        self.prepare_matrix()
        self.gfx_in, self.readable, self.matrix_out = values, readable, None
        assert self._call(0xab4de0, 0, OBJECT + 4, 0) == VALUE
        return self.matrix_out

    def set(self, values, valid=True):
        self.prepare_matrix()
        self.matrix_in, self.valid_matrix, self.gfx_out = values, valid, None
        result = self._call(0xab5ee0, 0, OBJECT + 4, VALUE, 0)
        assert result == (0 if valid else 0xffffffff)
        return self.gfx_out
