"""Execute original cache-only classification/getters; no hardware measurement."""
import struct

import unicorn
from unicorn.x86_const import UC_X86_REG_EDX

from stock_debug_queries_oracle import OriginalDebugQueries
import stock_runtime_services_oracle as base

OWNER, OBJECTS, INPUT = (base.MARKS + offset for offset in (0x10000, 0x11000, 0x12000))
CACHE = 0x1d382d0


class OriginalGraphicsDetection(OriginalDebugQueries):
    def __init__(self, executable):
        super().__init__(executable)
        self.shader = 0x300
        for address, callback in ((0x9263f0, lambda: self._return(0)),
                                   (0x926420, lambda: self._return(self.shader >> 8)),
                                   (0x926450, lambda: self._return(self.shader & 255))):
            self.hooks[address] = callback
            self.u.hook_add(unicorn.UC_HOOK_CODE, lambda u, a, s, d: self.hooks[a](),
                            begin=address, end=address)
        self.u.mem_write(OWNER, b'\0' * 0x600)
        for index, name in enumerate(('max', 'high', 'medium', 'low', 'min')):
            pointer = OBJECTS + 0x100 * index
            self.u.mem_write(OWNER + 4 * index, struct.pack('<I', pointer))
            self.u.mem_write(pointer + 4, struct.pack('<I', 1))
            self.string(pointer + 0x38, name)

    def string(self, address, text):
        value = text.encode('ascii')
        assert len(value) < 16
        self.u.mem_write(address, value.ljust(16, b'\0') + struct.pack('<II', len(value), 15))

    def default_profiles(self):
        # 009365e0's reviewed mov [ebx+disp32],imm32 initializers precede
        # resource parsing; decode those fixed instructions without executing
        # or replacing any hardware producer.
        code = bytes(self.u.mem_read(0x9365e0, 0x450))
        def value(offset):
            marker = b'\xc7\x83' + struct.pack('<I', offset)
            assert code.count(marker) == 1
            return struct.unpack('<I', code[code.index(marker) + 6:code.index(marker) + 10])[0]
        output = []
        for index in range(5):
            fields = [value(0x294 + index * 0x80 + field * 4) for field in range(8)]
            output.append({'minimum': [fields[1], fields[2], fields[3], fields[4], fields[0]],
                           'pixelShader': struct.unpack('<f', struct.pack('<I', fields[6]))[0],
                           'vertexShader': struct.unpack('<f', struct.pack('<I', fields[7]))[0]})
        return output

    def configure(self, profiles):
        for index, profile in enumerate(profiles):
            gpu, score, ram, virtual, cpu = profile['minimum']
            self.u.mem_write(OWNER + 0x14 + 0x80 * index,
                             struct.pack('<6I2f', cpu, gpu, score, ram, virtual, 0,
                                         profile['pixelShader'], profile['vertexShader']))

    def store(self, values):
        self.u.mem_write(CACHE, struct.pack('<7q', *values))

    def classify(self, values, shader):
        self.store(values)
        self.shader = shader
        result = self.call(0x9357b0, OWNER, [])
        assert list(struct.unpack('<7q', self.u.mem_read(CACHE, 56))) == values
        return result

    def score(self, index):
        self.call(0x935d30, OWNER, [struct.pack('<I', index & 0xffffffff)])
        raw = struct.pack('<II', self.u.reg_read(base.UC_X86_REG_EAX),
                          self.u.reg_read(UC_X86_REG_EDX))
        return struct.unpack('<q', raw)[0]

    def name(self, text):
        self.string(INPUT, text)
        return self.call(0x935db0, OWNER, [struct.pack('<I', INPUT)])
