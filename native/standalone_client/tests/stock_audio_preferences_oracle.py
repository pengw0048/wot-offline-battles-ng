"""Execute the complete #1513 speaker preference savers.

DataSection writes, DataResource file I/O, the preference filename owner and
reference-count imports are explicit boundaries. No graphics writer or full
application save is substituted for those original instructions.
"""
import copy
import struct


class OriginalSpeakerPreferences:
    def __init__(self, executable):
        import pefile
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
        from unicorn import x86_const as reg
        pe = pefile.PE(str(executable))
        if pe.FILE_HEADER.Machine != 0x14c:
            raise ValueError('The original speaker oracle requires the pinned x86 client')
        image = pe.get_memory_mapped_image()
        self.machine = Uc(UC_ARCH_X86, UC_MODE_32)
        self.reg = reg
        self.machine.mem_map(pe.OPTIONAL_HEADER.ImageBase, (len(image) + 4095) & ~4095)
        self.machine.mem_write(pe.OPTIONAL_HEADER.ImageBase, image)
        for address, size in ((0, 4096), (0x10000000, 65536),
                              (0x20000000, 65536), (0x30000000, 65536)):
            self.machine.mem_map(address, size)
        self.imports = {}
        for dll in pe.DIRECTORY_ENTRY_IMPORT:
            for entry in dll.imports:
                address = 0x30001000 + len(self.imports) * 8
                self.imports[address] = entry.name.decode() if entry.name else str(entry.ordinal)
                self.put(entry.address, address)
        self.machine.hook_add(UC_HOOK_CODE, self.hook)
        self.live_root, self.disk_root = 0x20000100, 0x20000200

    def put(self, address, *values):
        self.machine.mem_write(address, struct.pack('<' + 'I' * len(values), *values))

    def word(self, address):
        return struct.unpack('<I', self.machine.mem_read(address, 4))[0]

    def string(self, address):
        size = self.word(address + 16)
        if self.word(address + 20) >= 16:
            address = self.word(address)
        return bytes(self.machine.mem_read(address, size))

    def write_string(self, address, value):
        self.machine.mem_write(address, bytes(24))
        if len(value) < 16:
            self.machine.mem_write(address, value + b'\0')
            capacity = 15
        else:
            self.machine.mem_write(0x20001000, value + b'\0')
            self.put(address, 0x20001000)
            capacity = len(value)
        self.put(address + 16, len(value), capacity)

    def returned(self, value=0, cleanup=0):
        machine, reg = self.machine, self.reg
        stack = machine.reg_read(reg.UC_X86_REG_ESP)
        destination = self.word(stack)
        machine.reg_write(reg.UC_X86_REG_ESP, stack + 4 + cleanup)
        machine.reg_write(reg.UC_X86_REG_EAX, value & 0xffffffff)
        machine.reg_write(reg.UC_X86_REG_EIP, destination)

    def hook(self, machine, address, unused_size, unused_data):
        reg = self.reg
        stack = machine.reg_read(reg.UC_X86_REG_ESP)
        arg = lambda index: self.word(stack + 4 + index * 4)
        owner = machine.reg_read(reg.UC_X86_REG_ECX)
        if 0x12533c0 <= address < 0x12537d8:
            return
        if address in self.imports:
            name = self.imports[address]
            if name not in ('InterlockedIncrement', 'InterlockedDecrement'):
                raise AssertionError(('Unexpected original import', name))
            value = self.word(arg(0)) + (1 if name == 'InterlockedIncrement' else -1)
            self.put(arg(0), value)
            self.returned(value, 4)
        elif address == 0x623850:  # Existing preference owner singleton.
            self.returned(0x20000300)
        elif address == 0x6238f0:  # Its current live root, returned as SmartPointer.
            self.put(arg(0), self.live_root if self.live is not None else 0)
            self.returned(arg(0), 4)
        elif address == 0x623a80:  # Resolve the same owner's preference filename.
            self.write_string(arg(0), self.path)
            self.trace.append(('resolve', self.path))
            self.returned(arg(0), 4)
        elif address == 0x1197c90:  # DataResource default construction.
            machine.mem_write(owner, bytes(12))
            self.returned(owner)
        elif address == 0x11980b0:  # DataResource::load(filename, XML, false).
            assert self.string(arg(0)) == self.path
            assert (arg(1), arg(2)) == (0, 0)
            self.trace.append(('load', self.path, self.load_error))
            self.returned(self.load_error, 12)
        elif address == 0x1198010:  # DataResource::getRootSection.
            self.put(arg(0), 0 if self.null_root else self.disk_root)
            self.returned(arg(0), 4)
        elif address == 0x1180190:  # DataSection::writeUInt(path, value).
            key = bytes(machine.mem_read(self.word(arg(0)), self.word(arg(0) + 4)))
            value = arg(1)
            if owner == self.live_root:
                self.live[key] = value
                self.trace.append(('live-write', key, value))
            elif owner == self.disk_root:
                self.loaded[key] = value
                self.trace.append(('disk-write', key, value))
            else:
                raise AssertionError(('Unexpected DataSection owner', hex(owner)))
            self.returned(cleanup=8)
        elif address == 0x613c20:  # Empty string used for save-to-loaded-path.
            assert arg(1) == 0
            self.write_string(owner, b'')
            self.returned(owner, 8)
        elif address == 0x11982b0:  # DataResource::save("").
            assert self.string(arg(0)) == b''
            self.trace.append(('save', self.path, self.save_error))
            if not self.save_error:
                self.disk = copy.deepcopy(self.loaded)
            self.returned(self.save_error, 4)
        elif address in (0x11e7c30, 0x13d809e, 0x13d8019):
            # Temporary string destruction, security cookie and string free.
            self.returned()
        else:
            raise AssertionError(('Unexpected original execution boundary', hex(address)))

    def save(self, key, value, *, live=True, load_error=0, save_error=0,
             null_root=False, path=b'C:/player settings/preferences.xml'):
        addresses = {
            b'scriptsPreferences/soundPrefs/detectedSpeakerConfig': 0x12533c0,
            b'scriptsPreferences/soundPrefs/speakerConfig': 0x12535d0,
        }
        self.path = path
        self.trace = []
        self.load_error, self.save_error, self.null_root = load_error, save_error, null_root
        self.live = {b'unrelated-live-value': 18} if live else None
        self.disk = {b'graphicsPreferences/entry/FAR_PLANE': 3,
                     b'graphicsPreferences/entry/OBJECT_LOD': 3,
                     b'devicePreferences/windowedWidth': 1014,
                     b'scriptsPreferences/custom': b'preserve'}
        self.loaded = copy.deepcopy(self.disk)
        self.put(self.live_root, 0, 100)
        self.put(self.disk_root, 0, 100)
        self.machine.mem_write(0x10000000, bytes(65536))
        self.put(0x10008000, 0x30000000, value)
        self.machine.reg_write(self.reg.UC_X86_REG_ESP, 0x10008000)
        self.machine.reg_write(self.reg.UC_X86_REG_ECX, 0)
        self.machine.emu_start(addresses[key], 0x30000000, count=10000)
        assert self.machine.reg_read(self.reg.UC_X86_REG_EIP) == 0x30000000
        return list(self.trace), copy.deepcopy(self.live), copy.deepcopy(self.disk)
