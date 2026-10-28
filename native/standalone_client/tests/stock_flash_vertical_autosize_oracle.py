"""Execute the #1513 vertical autosize native methods, keeping layout calls explicit."""
import struct
import unicorn
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP
from stock_flash_input_mode_oracle import OriginalFlashInputMode, OBJECT, VALUE, MARKS, STACK, SENTINEL


class OriginalVerticalAutoSize(OriginalFlashInputMode):
    def __init__(self, executable):
        super().__init__(executable)
        self.field, self.document = MARKS + 0x4000, MARKS + 0x5000
        self.string, self.node, self.data = MARKS + 0x6000, MARKS + 0x6100, MARKS + 0x6200
        self.native, self.vm = MARKS + 0x7000, MARKS + 0x8000
        self.hooks.update({0xc145d0: self.enable_y, 0xcdb2d0: self.compare,
                           0xc9a6a0: self.dirty, 0xc237d0: self.result_string,
                           0xe59b00: self.error_object, 0xe605d0: self.throw_error})
        for address in (0xc145d0, 0xcdb2d0, 0xc9a6a0, 0xc237d0, 0xe59b00, 0xe605d0):
            self.u.hook_add(unicorn.UC_HOOK_CODE, self._hook, begin=address, end=address)

    def read32(self, address):
        return struct.unpack('<I', self.u.mem_read(address, 4))[0]

    def write32(self, address, value):
        self.u.mem_write(address, struct.pack('<I', value))

    def enable_y(self):
        self.u.mem_write(self.document + 0x108, bytes([self.u.mem_read(self.document+0x108, 1)[0] | 2]))
        self._return(0)

    def compare(self):
        text = self._cstr(self.read32(self.read32(self.u.reg_read(UC_X86_REG_ECX))))
        self._return(int(text == self._cstr(self._arg(1))), 4)

    def dirty(self):
        self.dirty_count += 1
        self._return(0)

    def result_string(self):
        self.result = self._cstr(self._arg(1))
        self._return(0, 4)

    def error_object(self):
        self.error_code = self._arg(1)
        self.write32(self.u.reg_read(UC_X86_REG_ECX) + 4, MARKS + 0x9100)
        self.write32(MARKS + 0x910c, 2)
        self._return(self.u.reg_read(UC_X86_REG_ECX), 16)

    def throw_error(self):
        self._return(0, 4)

    def run(self, value, y=False, align=0, enabled=True, null=False):
        self.u.mem_write(OBJECT, bytes(0xa000))
        self.write32(OBJECT + 0x14, self.native)
        self.write32(self.native + 0x4c, self.vm)
        self.u.mem_write(self.vm + 0x234, bytes([int(enabled)]))
        self.write32(self.field + 0x4c, self.field + 0x100)
        self.write32(self.field + 0x180, self.document)
        self.u.mem_write(self.document+0x108, bytes([int(y)*2, align<<2, 0, 0]))
        self.write32(self.string, self.node); self.write32(self.node, self.data)
        self.u.mem_write(self.data, value.encode('ascii')+b'\0')
        self.dirty_count, self.result, self.error_code = 0, None, None
        args = (VALUE, 0 if null else self.field, self.string)
        self.u.reg_write(UC_X86_REG_ECX, OBJECT)
        self.u.reg_write(UC_X86_REG_ESP, STACK + 0x80000)
        self.u.mem_write(STACK+0x80000, struct.pack('<IIII', SENTINEL, *args))
        self.u.emu_start(0xe24ec0, SENTINEL+1, count=10000)
        flags = bytes(self.u.mem_read(self.document+0x108, 4))
        if not null:
            self.u.reg_write(UC_X86_REG_ECX, OBJECT)
            self.u.reg_write(UC_X86_REG_ESP, STACK+0x80000)
            self.u.mem_write(STACK+0x80000, struct.pack('<III', SENTINEL, VALUE, self.field))
            self.u.emu_start(0xe25020, SENTINEL+1, count=10000)
        return {'y':bool(flags[0]&2), 'align':(flags[1]>>2)&3,
                'dirty':self.dirty_count, 'result':self.result, 'error':self.error_code}
