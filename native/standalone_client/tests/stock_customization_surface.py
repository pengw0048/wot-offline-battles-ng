# Python 2.7: inspect original code objects; never import stock game services.
from __future__ import print_function
import dis
import json
import marshal
import sys
import types
import zipfile

archive = zipfile.ZipFile(sys.argv[1])
references = []
helper_methods = set()

def instructions(code):
    offset = 0
    while offset < len(code.co_code):
        at = offset
        op = ord(code.co_code[offset])
        offset += 1
        argument = None
        if op >= dis.HAVE_ARGUMENT:
            argument = ord(code.co_code[offset]) | ord(code.co_code[offset + 1]) << 8
            offset += 2
        yield at, dis.opname[op], argument

def walk(member, code, path):
    path = path + [code.co_name]
    ops = list(instructions(code))
    for index, (offset, op, arg) in enumerate(ops[:-1]):
        next_offset, next_op, next_arg = ops[index + 1]
        if op in ('LOAD_GLOBAL', 'LOAD_NAME') and code.co_names[arg] == 'BigWorld' and next_op == 'LOAD_ATTR':
            references.append({'member': member, 'function': '.'.join(path),
                'offset': offset, 'name': code.co_names[next_arg]})
        if op == 'LOAD_ATTR' and code.co_names[arg] == '_helper' and next_op == 'LOAD_ATTR':
            helper_methods.add(code.co_names[next_arg])
    for value in code.co_consts:
        if isinstance(value, types.CodeType):
            walk(member, value, path)

for member in archive.namelist():
    if member.endswith('.pyc') and (
            '/gui/customization/' in member or
            '/gui/Scaleform/daapi/view/lobby/customization/' in member or
            member == 'scripts/client/vehicle_systems/camouflages.pyc'):
        walk(member, marshal.loads(archive.read(member)[8:]), [])
print(json.dumps({'references': references, 'helper_methods': sorted(helper_methods)}, sort_keys=True))
