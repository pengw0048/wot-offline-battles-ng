#!/usr/bin/env python3
"""Read-only native surface audit over exact CPython 2.7 code objects.

Run with Python 3; --python27 reads scripts.pkg in a child CPython 2.7.  Source
registrations are candidates, not proof of linking or rendering. A live module
snapshot overrides the source-name census while preserving every source site.
"""
from __future__ import print_function
import argparse
import bisect
import collections
import dis
import json
import os
import re
import subprocess
import struct
import sys
import tarfile
import types
import zipfile


def write_json(path, value):
    with open(path, 'w') as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write('\n')


def opcodes(code):
    offset, extended = 0, 0
    data = code.co_code
    while offset < len(data):
        at = offset
        op = ord(data[offset])
        offset += 1
        argument = None
        if op >= dis.HAVE_ARGUMENT:
            argument = ord(data[offset]) | ord(data[offset + 1]) << 8 | extended
            offset += 2
            extended = argument << 16 if op == dis.EXTENDED_ARG else 0
        yield at, dis.opname[op], argument, offset


def extract(package, registry):
    import marshal
    modules = set(registry['modules'])
    factories = set(registry['factory_paths'])
    archive = zipfile.ZipFile(package)
    references, unresolved, import_stars = [], [], []
    fields = {}
    code_objects = []
    members = 0

    def collect(member, code, path, aliases, class_path):
        path = path + [code.co_name]
        if '__module__' in code.co_names and code.co_argcount == 0:
            class_path = '.'.join(path)
        code_objects.append((member, code, path, dict(aliases), class_path))
        # Imports are lexical module aliases. Functions can also import locally.
        stack = []
        inherited = dict(aliases)
        for at, op, arg, unused in opcodes(code):
            if op == 'IMPORT_NAME':
                stack = [('native', code.co_names[arg])] if code.co_names[arg] in modules else [('unknown', '')]
            elif op == 'IMPORT_FROM' and stack:
                owner = stack[-1]
                stack.append(('native', owner[1] + '.' + code.co_names[arg]) if owner[0] == 'native' else ('unknown', ''))
            elif op in ('STORE_NAME', 'STORE_GLOBAL', 'STORE_FAST') and stack:
                name = code.co_varnames[arg] if op == 'STORE_FAST' else code.co_names[arg]
                value = stack.pop()
                if value[0] == 'native':
                    inherited[name] = value
            elif op == 'POP_TOP' and stack:
                stack.pop()
            elif op not in ('LOAD_CONST', 'EXTENDED_ARG'):
                stack = []
        code_objects[-1] = (member, code, path, inherited, class_path)
        for value in code.co_consts:
            if isinstance(value, types.CodeType):
                collect(member, value, path, inherited, class_path)

    for member in archive.namelist():
        if not member.endswith('.pyc'):
            continue
        data = archive.read(member)
        if data[:4] != b'\x03\xf3\x0d\x0a':
            raise ValueError('Unexpected Python magic: ' + member)
        members += 1
        collect(member, marshal.loads(data[8:]), [], {}, '')

    def display(code, item):
        at, op, arg, unused = item
        value = ''
        if op in ('LOAD_GLOBAL', 'LOAD_NAME', 'LOAD_ATTR', 'IMPORT_NAME', 'IMPORT_FROM'):
            value = code.co_names[arg]
        elif op == 'LOAD_FAST':
            value = code.co_varnames[arg]
        elif op == 'LOAD_CONST':
            value = repr(code.co_consts[arg])[:120]
        elif arg is not None:
            value = str(arg)
        return '%d %s %s' % (at, op, value)

    def guards(code, ops, offset, target, aliases):
        result = []
        for index, (at, op, arg, following) in enumerate(ops):
            if op in ('POP_JUMP_IF_FALSE', 'POP_JUMP_IF_TRUE', 'JUMP_IF_FALSE_OR_POP', 'JUMP_IF_TRUE_OR_POP'):
                if at < offset < arg:
                    before = ops[max(0, index - 14):index]
                    text = ' | '.join(display(code, item) for item in before)[-1000:]
                    names = [code.co_names[item[2]] for item in before
                             if item[1] in ('LOAD_GLOBAL', 'LOAD_NAME', 'LOAD_ATTR')]
                    strings = [code.co_consts[item[2]] for item in before
                               if item[1] == 'LOAD_CONST' and isinstance(code.co_consts[item[2]], basestring)]
                    native_aliases = [name for name in names if aliases.get(name) == ('native', target.split('.')[0])]
                    explicit = 'hasattr' in names and bool(native_aliases) and target.split('.')[1] in strings
                    result.append({'kind': 'forward-branch', 'offset': at, 'target': arg,
                        'condition': text, 'explicit_native_existence_guard': explicit,
                        'build_or_editor_guard': any('EDITOR' in name or 'DEVELOPMENT' in name or name.startswith('IS_DEBUG') for name in names),
                        'nonclient_platform_guard': bool(before and before[-1][1] in ('LOAD_GLOBAL', 'LOAD_NAME') and
                            code.co_names[before[-1][2]] in ('IS_BASEAPP', 'IS_CELLAPP') and op == 'POP_JUMP_IF_FALSE')})
            elif op in ('SETUP_EXCEPT', 'SETUP_FINALLY'):
                stop = following + arg
                if at < offset < stop:
                    handler = [item for item in ops if stop <= item[0] < stop + 110]
                    text = ' | '.join(display(code, item) for item in handler)
                    result.append({'kind': 'exception-region' if op == 'SETUP_EXCEPT' else 'finally-region',
                        'offset': at, 'target': stop, 'handler_prefix': text,
                        'mentions_AttributeError': 'AttributeError' in text})
        return result

    def scope(member, path):
        lower = member.lower()
        if not (member.startswith('scripts/client/') or member.startswith('scripts/common/')):
            return 'non-client-script'
        if any(part in lower for part in ('/editor/', '/editors/', '/unit_tests/', '/tests/', '/test/', '/debug/', '/debug_utils/', '/bwobsolete_helpers/', '/bwobsolete_tests/')):
            return 'editor-test-debug-candidate'
        if any(part.startswith(('test_', 'debug_', 'bwobsolete_')) for part in lower.split('/')):
            return 'editor-test-debug-candidate'
        return 'client-common-runtime-candidate'

    # First infer directly constructed native fields. This is a type
    # hypothesis only, and never turns a function's return into a native type.
    for pass_index in (0, 1):
        for member, code, path, imported, class_path in code_objects:
            ops = list(opcodes(code))
            line_starts = list(dis.findlinestarts(code))
            line_offsets = [item[0] for item in line_starts]
            local = dict(imported)
            for parameter in code.co_varnames[:code.co_argcount]:
                local[parameter] = ('unknown', 'function-parameter')
            stack = []
            if code.co_argcount and code.co_varnames[0] in ('self', 'cls'):
                local[code.co_varnames[0]] = ('self', class_path)
            def pop():
                return stack.pop() if stack else ('unknown', '')
            def add(at, native_path, evidence, receiver='module'):
                if pass_index == 0:
                    return
                first = native_path.split('.')[0]
                if first not in modules or len(native_path.split('.')) < 2:
                    return
                line = line_starts[max(0, bisect.bisect_right(line_offsets, at) - 1)][1] if line_starts else code.co_firstlineno
                references.append({'member': member, 'code_path': '.'.join(path),
                    'source_file': code.co_filename, 'line': line, 'offset': at,
                    'native_path': native_path, 'evidence': evidence, 'receiver': receiver,
                    'scope': scope(member, path), 'guards': guards(code, ops, at, native_path, imported),
                    'module_or_class_initialization': code.co_name == '<module>' or '__module__' in code.co_names})
            for index, (at, op, arg, following) in enumerate(ops):
                if op in ('LOAD_GLOBAL', 'LOAD_NAME', 'LOAD_FAST'):
                    name = code.co_varnames[arg] if op == 'LOAD_FAST' else code.co_names[arg]
                    value = local.get(name, ('unknown', name))
                    stack.append(value)
                    if value[0] == 'native' and '.' in value[1]:
                        add(at, value[1], 'import-or-assigned-native-alias')
                elif op == 'LOAD_ATTR':
                    owner = pop()
                    name = code.co_names[arg]
                    if owner[0] == 'native':
                        value = ('native', owner[1] + '.' + name)
                        add(at, value[1], 'direct-module-attribute')
                    elif owner[0] == 'instance':
                        value = ('unknown', '')
                        add(at, owner[1] + '.' + name, 'direct-constructor-result', 'typed-instance-hypothesis')
                    elif owner[0] == 'self':
                        value = fields.get((owner[1], name), ('unknown', ''))
                    else:
                        value = ('unknown', '')
                    stack.append(value)
                elif op == 'LOAD_CONST':
                    stack.append(('constant', code.co_consts[arg]))
                elif op == 'IMPORT_NAME':
                    pop(); pop()
                    name = code.co_names[arg]
                    stack.append(('native', name) if name in modules else ('unknown', ''))
                elif op == 'IMPORT_FROM':
                    owner = stack[-1] if stack else ('unknown', '')
                    value = ('native', owner[1] + '.' + code.co_names[arg]) if owner[0] == 'native' else ('unknown', '')
                    stack.append(value)
                    if value[0] == 'native':
                        add(at, value[1], 'from-native-import')
                elif op == 'IMPORT_STAR':
                    if pass_index and stack and stack[-1][0] == 'native':
                        import_stars.append({'member': member, 'code_path': '.'.join(path), 'module': stack[-1][1], 'offset': at})
                    pop()
                elif op in ('STORE_NAME', 'STORE_GLOBAL', 'STORE_FAST'):
                    name = code.co_varnames[arg] if op == 'STORE_FAST' else code.co_names[arg]
                    local[name] = pop()
                elif op == 'STORE_ATTR':
                    owner, value = pop(), pop()
                    if owner[0] == 'self' and value[0] == 'instance':
                        key = (owner[1], code.co_names[arg])
                        previous = fields.get(key)
                        if previous is None or previous == value:
                            fields[key] = value
                        else:
                            fields[key] = ('unknown', 'conflicting-native-field-producers')
                elif op.startswith('CALL_FUNCTION'):
                    count = (arg & 255) + 2 * (arg >> 8)
                    count += int(op in ('CALL_FUNCTION_VAR', 'CALL_FUNCTION_VAR_KW')) + int(op in ('CALL_FUNCTION_KW', 'CALL_FUNCTION_VAR_KW'))
                    for unused in range(count):
                        pop()
                    function = pop()
                    stack.append(('instance', function[1]) if function[0] == 'native' and function[1] in factories else ('unknown', ''))
                elif op == 'POP_TOP' or op == 'RETURN_VALUE':
                    pop()
                elif op == 'DUP_TOP':
                    stack.append(stack[-1] if stack else ('unknown', ''))
                elif op == 'ROT_TWO' and len(stack) >= 2:
                    stack[-2:] = stack[-2:][::-1]
                elif op == 'ROT_THREE' and len(stack) >= 3:
                    stack[-3:] = [stack[-1], stack[-3], stack[-2]]
                elif op in ('BUILD_TUPLE', 'BUILD_LIST', 'BUILD_SET'):
                    for unused in range(arg):
                        pop()
                    stack.append(('unknown', ''))
                elif op == 'BUILD_MAP':
                    stack.append(('unknown', ''))
                elif op in ('BINARY_SUBSCR', 'COMPARE_OP') or op.startswith(('BINARY_', 'INPLACE_')):
                    pop(); pop(); stack.append(('unknown', ''))
                elif op.startswith('UNARY_') or op == 'GET_ITER':
                    pop(); stack.append(('unknown', ''))
                elif op == 'UNPACK_SEQUENCE':
                    pop(); stack.extend([('unknown', '')] * arg)
                elif op.startswith('POP_JUMP'):
                    pop()
                elif op == 'FOR_ITER':
                    stack.append(('unknown', ''))
                elif op not in ('JUMP_FORWARD', 'JUMP_ABSOLUTE', 'SETUP_LOOP', 'POP_BLOCK', 'SETUP_EXCEPT', 'SETUP_FINALLY', 'NOP', 'EXTENDED_ARG'):
                    # Conservatively stop propagating through an unmodelled
                    # opcode. Direct module chains resume on the next load.
                    stack = []
    unique = {}
    for ref in references:
        key = (ref['member'], ref['code_path'], ref['offset'], ref['native_path'])
        unique[key] = ref
    return {'format': 1, 'package': package, 'pyc_members': members,
        'code_objects': len(code_objects), 'modules': sorted(modules),
        'references': sorted(unique.values(), key=lambda x: (x['native_path'], x['member'], x['code_path'], x['offset'])),
        'native_field_hypotheses': [{'class_path': key[0], 'attribute': key[1], 'type': value[1]}
            for key, value in sorted(fields.items()) if value[0] == 'instance'],
        'star_imports': import_stars,
        'limitations': ['Direct module references and native constructor aliases only; arbitrary return-object types are not invented.',
            'Typed local/field references are conservative constructor hypotheses, not native dispatch proof.',
            'Factories discovered from a live type snapshot are accepted as additional constructor hypotheses.',
            'Forward branch and exception ranges are retained; this is not a full callgraph or proof of live reachability.',
            'Else regions, dynamic getattr strings, extension-added Python monkeypatches and native-internal calls require separate review.']}


def stripped(source):
    # Preserve lines and string contents. Only C++ comments are removed.
    token = re.compile(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|//[^\n]*|/\*.*?\*/', re.S)
    return token.sub(lambda match: match.group(1) if match.group(1) else re.sub(r'[^\n]', ' ', match.group()), source)


def macro_calls(source):
    for match in re.finditer(r'\b(PY_[A-Z_]+|Py_Header)\s*\(', source):
        start, index, depth, quoted, escaped = match.start(), match.end(), 1, None, False
        parts, begin = [], index
        while index < len(source) and depth:
            char = source[index]
            if quoted:
                if escaped:
                    escaped = False
                elif char == '\\':
                    escaped = True
                elif char == quoted:
                    quoted = None
            elif char in ('"', "'"):
                quoted = char
            elif char == '(':
                depth += 1
            elif char == ')':
                depth -= 1
            elif char == ',' and depth == 1:
                parts.append(source[begin:index].strip()); begin = index + 1
            index += 1
        if not depth:
            parts.append(source[begin:index - 1].strip())
            yield match.group(1), parts, start


def registrations(repo, sdk_archive, modules):
    exports, classes, source_files, unresolved = [], {}, 0, []
    def scan(name, source, origin):
        source = stripped(source)
        lines = source.splitlines()
        offsets = [0]
        for line in lines:
            offsets.append(offsets[-1] + len(line) + 1)
        conditions, per_line = [], []
        for line in lines:
            if re.match(r'\s*#\s*(if|ifdef|ifndef)\b', line):
                conditions.append(line.strip())
            elif re.match(r'\s*#\s*(else|elif)\b', line) and conditions:
                conditions[-1] += ' ; ' + line.strip()
            per_line.append(list(conditions))
            if re.match(r'\s*#\s*endif\b', line) and conditions:
                conditions.pop()
        current_methods, current_attrs = None, None
        for dynamic in re.finditer(r'\b(\w+)::pyGetAttribute\s*\(', source):
            classes.setdefault(dynamic.group(1), {'methods': [], 'attributes': [], 'bases': [], 'sources': []})['dynamic_lookup_override'] = True
        for macro, args, at in macro_calls(source):
            line = bisect.bisect_right(offsets, at)
            proof = {'origin': origin, 'source': name, 'line': line, 'macro': macro,
                     'preprocessor_guards': per_line[min(line - 1, len(per_line) - 1)] if per_line else []}
            if macro == 'Py_Header' and len(args) >= 2:
                cls = classes.setdefault(args[0], {'methods': [], 'attributes': [], 'bases': [], 'sources': []})
                cls['bases'].append(args[1]); cls['sources'].append(proof)
            if macro == 'PY_BEGIN_METHODS' and args:
                current_methods = args[0]
                classes.setdefault(current_methods, {'methods': [], 'attributes': [], 'bases': [], 'sources': []})
            elif macro == 'PY_END_METHODS':
                current_methods = None
            elif macro == 'PY_BEGIN_ATTRIBUTES' and args:
                current_attrs = args[0]
                classes.setdefault(current_attrs, {'methods': [], 'attributes': [], 'bases': [], 'sources': []})
            elif macro == 'PY_END_ATTRIBUTES':
                current_attrs = None
            elif macro in ('PY_METHOD', 'PY_METHOD_WITH_DOC') and current_methods and args:
                classes[current_methods]['methods'].append(args[0])
            elif macro in ('PY_ATTRIBUTE', 'PY_ATTRIBUTE_WITH_DOC') and current_attrs and args:
                classes[current_attrs]['attributes'].append(args[0])
            module, exported, cls_name, kind = None, None, None, None
            if macro in ('PY_MODULE_FUNCTION', 'PY_MODULE_FUNCTION_WITH_KEYWORDS', 'PY_MODULE_FUNCTION_WITH_DOC') and len(args) >= 2:
                exported, module, kind = args[0], args[1], 'function'
            elif macro in ('PY_MODULE_STATIC_METHOD', 'PY_MODULE_STATIC_METHOD_WITH_DOC') and len(args) >= 3:
                exported, module, kind = args[1], args[2], 'function'
            elif macro == 'PY_AUTO_MODULE_FUNCTION' and len(args) >= 4:
                exported, module, kind = args[1], args[3], 'function'
            elif macro == 'PY_FACTORY' and len(args) >= 2:
                exported, module, cls_name, kind = args[0], args[1], args[0], 'factory'
            elif macro == 'PY_FACTORY_NAMED' and len(args) >= 3:
                exported, module, cls_name, kind = args[1].strip('"'), args[2], args[0], 'factory'
            elif macro == 'PY_MODULE_ATTRIBUTE' and len(args) >= 2:
                module, exported, kind = args[0], args[1], 'attribute'
            if module and re.match(r'^[A-Za-z_]\w*$', module) and re.match(r'^[A-Za-z_]\w*$', exported):
                if module in ('MODULE', 'MODULE_NAME', 'moduleName', 'MODULE_NAME_'):
                    continue
                if modules is None or module in modules:
                    proof.update(module=module, name=exported, kind=kind, native_class=cls_name)
                    exports.append(proof)
    for directory, unused, files in os.walk(os.path.join(repo, 'native/standalone_client/bigworld')):
        for filename in files:
            if filename.endswith(('.cpp', '.hpp', '.inc')):
                path = os.path.join(directory, filename)
                with open(path, 'r') as stream:
                    scan(os.path.relpath(path, repo), stream.read(), 'NG-source-candidate')
                source_files += 1
    if sdk_archive:
        with tarfile.open(sdk_archive) as archive:
            for member in archive:
                if not member.isfile() or not member.name.endswith(('.cpp', '.hpp', '.ipp')):
                    continue
                relative = member.name.split('/programming/bigworld/')[-1]
                if relative == member.name or not relative.startswith(('client/', 'lib/')):
                    continue
                data = archive.extractfile(member).read().decode('utf-8', 'replace')
                if 'PY_' not in data and 'Py_Header' not in data:
                    continue
                scan(relative, data, 'SDK-source-candidate')
                source_files += 1
    for cls in classes.values():
        for key in ('methods', 'attributes', 'bases'):
            cls[key] = sorted(set(cls[key]))
    selected = sorted(set(item['module'] for item in exports))
    if modules is not None:
        selected = sorted(modules)
    return {'format': 1, 'repo': repo, 'sdk_archive': sdk_archive,
        'source_files_scanned': source_files, 'modules': selected,
        'exports': exports, 'classes': classes,
        'factory_paths': sorted(set(item['module'] + '.' + item['name'] for item in exports if item['kind'] == 'factory')),
        'limitations': ['Macros are source candidates; preprocessor guards and final linking are not evaluated.',
            'Direct PyModule_AddObject/PyMethodDef tables, Python startup modifications and generated patch output require live dir snapshots.',
            'SDK client/lib sources are included; source existence does not prove a source is compiled into this NG binary.']}


def original_catalog(path, executable):
    with open(path) as stream:
        original = json.load(stream)
    with open(executable, 'rb') as stream:
        image = stream.read()
    pe = struct.unpack_from('<I', image, 60)[0]
    machine, count = struct.unpack_from('<HH', image, pe + 4)
    optional_size = struct.unpack_from('<H', image, pe + 20)[0]
    if machine != 0x14c or struct.unpack_from('<H', image, pe + 24)[0] != 0x10b:
        raise ValueError('Original registration validation requires the pinned x86 PE')
    base = struct.unpack_from('<I', image, pe + 24 + 28)[0]
    sections = []
    for index in range(count):
        at = pe + 24 + optional_size + index * 40
        virtual_size, rva, raw_size, raw_offset = struct.unpack_from('<IIII', image, at + 8)
        sections.append((base + rva, raw_size, raw_offset))
    def read(address, size):
        for start, count, offset in sections:
            if start <= address and address + size <= start + count:
                return image[offset + address - start:offset + address - start + size]
        return b''
    exports, members = {}, set()
    for item in original.get('registration_coverage', []):
        record = dict(item['registration'])
        native_path = record['module'] + '.' + record['name']
        start = int(record['register_address'], 16)
        call_at = int(record.get('constructor_call_address', record['register_address']), 16)
        body = read(start, max(64, call_at - start + 8))
        call = read(call_at, 5)
        declared = int(record.get('constructor_address', '0'), 16)
        actual = call_at + 5 + struct.unpack_from('<i', call, 1)[0] if call[:1] == b'\xe8' else None
        encoded_name = record['name'].encode('ascii') + b'\0'
        name_reference = any(read(struct.unpack_from('<I', body, index)[0], len(encoded_name)) == encoded_name
            for index in range(max(0, len(body) - 3)))
        target = record.get('target_address') or record.get('type_address')
        binding_reference = target is not None and struct.pack('<I', int(target, 16)) in body
        record['current_PE_registration_bytes_verified'] = bool(actual == declared and name_reference and binding_reference)
        record['current_PE_validation'] = {'constructor_call_matches': actual == declared,
            'name_pointer_matches': name_reference, 'binding_pointer_matches': binding_reference}
        exports.setdefault(native_path, []).append(record)
    for row in original.get('rows', []):
        evidence = row.get('original_evidence', {})
        if row.get('type') and row.get('module') and row.get('kind') in ('method', 'property'):
            if evidence.get('method') or evidence.get('attribute') or evidence.get('member') or row.get('original_accessors'):
                members.add(row['module'] + '.' + row['type'] + '.' + row['name'])
    return {'path': path, 'target': original.get('target'), 'exports': exports,
        'members': sorted(members), 'registration_sites': sum(len(value) for value in exports.values()),
        'PE_instruction_verified_sites': sum(record['current_PE_registration_bytes_verified']
            for value in exports.values() for record in value),
        'limitations': ['Only positive original PE registrations and positive reviewed member records are used.',
            'Not in this catalog means original export evidence unresolved, not a claim of original absence.',
            'No historical NG implementation status from the reference inventory is used.']}


def load_snapshot(path):
    if not path:
        return {}, {}
    with open(path) as stream:
        value = json.load(stream)
    modules = value.get('modules', value)
    if not isinstance(modules, dict):
        raise ValueError('Runtime snapshot modules must be a mapping')
    names, schemas = {}, {}
    for module, item in modules.items():
        if isinstance(item, list):
            names[module] = set(item)
        elif isinstance(item, dict):
            listed = item.get('names', item.get('dir', item.get('exports')))
            if isinstance(listed, list):
                names[module] = set(listed)
            for typename, schema in item.get('types', {}).items():
                if isinstance(schema, list):
                    schemas[module + '.' + typename] = set(schema)
                else:
                    schemas[module + '.' + typename] = set(schema.get('methods', []) + schema.get('attributes', []) + schema.get('names', []))
    return names, schemas


def compare(demand, registry, snapshot):
    live_names, live_types = load_snapshot(snapshot)
    available = collections.defaultdict(list)
    factory_classes = {}
    for item in registry['exports']:
        native_path = item['module'] + '.' + item['name']
        available[native_path].append(item)
        if item['native_class']:
            factory_classes[native_path] = item['native_class']
    def schema(cls, seen=None):
        seen = set() if seen is None else set(seen)
        if cls in seen:
            return set()
        seen.add(cls)
        value = registry['classes'].get(cls, {})
        names = set(value.get('methods', []) + value.get('attributes', []))
        for base in value.get('bases', []):
            names.update(schema(base, seen))
        return names
    def dynamic_lookup(cls, seen=None):
        seen = set() if seen is None else set(seen)
        if cls in seen or cls in ('PyObjectPlus', 'PyObject'):
            return False
        seen.add(cls)
        value = registry['classes'].get(cls, {})
        return value.get('dynamic_lookup_override', False) or any(
            dynamic_lookup(base, seen) for base in value.get('bases', []))
    original = registry.get('original_positive_catalog', {})
    groups = {}
    for ref in demand['references']:
        components = ref['native_path'].split('.')
        export = '.'.join(components[:2])
        typed = ref['receiver'] == 'typed-instance-hypothesis'
        status, evidence = 'source-registration-present', 'source-macro-candidate'
        if components[0] in live_names:
            status = 'runtime-export-present' if components[1] in live_names[components[0]] else 'runtime-export-absent'
            evidence = 'live-dir-snapshot'
        elif export not in available:
            status, evidence = 'source-export-not-found', 'unresolved-until-live-snapshot'
        if len(components) >= 3 and status not in ('runtime-export-absent', 'source-export-not-found'):
            cls_name = factory_classes.get(export)
            if export in live_types:
                status = 'runtime-member-present' if components[2] in live_types[export] else 'runtime-member-absent'
                evidence = 'live-type-dir-snapshot'
                if status == 'runtime-member-absent' and cls_name and dynamic_lookup(cls_name):
                    status, evidence = 'dynamic-member-needs-instance-evidence', 'SDK-pyGetAttribute-override'
            elif cls_name:
                status = 'source-member-present' if components[2] in schema(cls_name) else 'source-member-not-found'
                evidence = 'source-class-schema-candidate'
                if status == 'source-member-not-found' and dynamic_lookup(cls_name):
                    status, evidence = 'dynamic-member-needs-instance-evidence', 'SDK-pyGetAttribute-override'
            else:
                status, evidence = 'member-schema-unresolved', 'manual-or-dynamic-native-type'
        key = (ref['native_path'], status)
        group = groups.setdefault(key, {'native_path': ref['native_path'], 'status': status,
            'evidence': evidence, 'sites': [], 'registrations': available.get(export, [])})
        group['sites'].append(ref)
    results = []
    for group in groups.values():
        sites = group['sites']
        unguarded = [site for site in sites if site['scope'] == 'client-common-runtime-candidate' and
            not any(guard.get('explicit_native_existence_guard') or guard.get('build_or_editor_guard') or guard.get('nonclient_platform_guard') for guard in site['guards'])]
        initialization = [site for site in unguarded if site['module_or_class_initialization']]
        missing = 'absent' in group['status'] or 'not-found' in group['status']
        if not missing:
            priority = 'covered-name-only'
        elif initialization:
            priority = 'P1-import-lifecycle-candidate'
        elif unguarded:
            priority = 'P2-live-path-needs-reachability'
        else:
            priority = 'P3-guarded-editor-or-nonclient-candidate'
        native_path = group['native_path']
        export = '.'.join(native_path.split('.')[:2])
        original_export = original.get('exports', {}).get(export, [])
        original_member = native_path in original.get('members', [])
        if group['status'] == 'runtime-export-absent':
            verified = any(item.get('current_PE_registration_bytes_verified') for item in original_export)
            difference = ('confirmed-export-gap-versus-original' if verified else
                'original-registration-instruction-unverified' if original_export else
                'stock-reference-without-reviewed-original-export')
        elif group['status'] == 'runtime-member-absent':
            difference = 'confirmed-member-gap-versus-original' if original_member else 'original-member-baseline-unresolved'
        else:
            difference = 'not-a-confirmed-missing-runtime-name'
        if missing and original and difference in ('stock-reference-without-reviewed-original-export', 'original-member-baseline-unresolved', 'original-registration-instruction-unverified'):
            priority = 'P3-original-baseline-or-inference-unresolved'
        group.update(difference_status=difference, original_export_registrations=original_export,
            original_member_positive_catalog_match=original_member)
        group.update(priority=priority, site_count=len(sites), unguarded_client_site_count=len(unguarded),
            import_initialization_site_count=len(initialization),
            native_behavior_proved=False, live_reachability_proved=False)
        results.append(group)
    results.sort(key=lambda value: (value['priority'], value['native_path']))
    counts = collections.Counter(value['status'] for value in results)
    return {'format': 1, 'runtime_snapshot': snapshot, 'summary': dict(counts),
        'required_native_paths': len(results), 'groups': results,
        'limitations': demand['limitations'] + registry['limitations'] + [
            'A missing export is not automatically live: caller reachability, existing script patches and target feature flags must be checked.',
            'Source or runtime name coverage proves neither signatures, value schemas, lifecycle behavior nor GPU/native equivalence.',
            'P1/P2 are investigation priorities; they are not confirmed end-user bugs without an executed path.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo')
    parser.add_argument('--client')
    parser.add_argument('--sdk-archive')
    parser.add_argument('--python27', default=os.environ.get('WOT_PYTHON27', 'python2.7'))
    parser.add_argument('--modules', default='BigWorld,GUI,Math')
    parser.add_argument('--runtime-snapshot')
    parser.add_argument('--original-api', help='Optional original-PE inventory: use module/type names only, never old implementation status')
    parser.add_argument('--output-dir')
    parser.add_argument('--extract-child', action='store_true')
    parser.add_argument('--registry')
    parser.add_argument('--demand')
    args = parser.parse_args()
    if args.extract_child:
        if sys.version_info[:2] != (2, 7):
            raise RuntimeError('PYC extraction requires CPython 2.7')
        with open(args.registry) as stream:
            registry = json.load(stream)
        write_json(args.demand, extract(os.path.join(args.client, 'res/packages/scripts.pkg'), registry))
        return
    if not (args.repo and args.client and args.output_dir):
        parser.error('--repo, --client and --output-dir are required')
    from inspect_client import inspect_client
    inspect_client(__import__('pathlib').Path(args.client))
    if not os.path.isdir(args.output_dir):
        os.makedirs(args.output_dir)
    modules = None if args.modules == 'all' else set(args.modules.split(','))
    registry = registrations(args.repo, args.sdk_archive, modules)
    if args.original_api:
        with open(args.original_api) as stream:
            original = json.load(stream)
        original_modules = set(row.get('module') for row in original.get('rows', [])
            if row.get('module') and not row['module'].startswith('<'))
        registry['original_module_catalog'] = {'path': args.original_api,
            'target': original.get('target'), 'modules': sorted(original_modules),
            'used_fields': ['positive original registrations and schemas; no historical NG implementation status']}
        registry['original_positive_catalog'] = original_catalog(args.original_api,
            os.path.join(args.client, 'WorldOfTanks.exe'))
        if modules is None:
            registry['modules'] = sorted(set(registry['modules']) | original_modules)
    registry_path = os.path.join(args.output_dir, 'source-registrations.json')
    demand_path = os.path.join(args.output_dir, 'exact-pyc-demand.json')
    if args.runtime_snapshot:
        unused_names, live_types = load_snapshot(args.runtime_snapshot)
        registry['factory_paths'] = sorted(set(registry['factory_paths']) | set(live_types))
    write_json(registry_path, registry)
    subprocess.check_call([args.python27, '-B', os.path.abspath(__file__), '--extract-child',
        '--client', args.client, '--registry', registry_path, '--demand', demand_path])
    with open(demand_path) as stream:
        demand = json.load(stream)
    result = compare(demand, registry, args.runtime_snapshot)
    try:
        result['source_revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=args.repo).decode().strip()
        result['source_worktree_status'] = subprocess.check_output(['git', 'status', '--short'], cwd=args.repo).decode().splitlines()
    except subprocess.CalledProcessError:
        result['source_revision'] = None
    result['client'] = args.client
    result['original_PE_registration_verification'] = {key: registry.get('original_positive_catalog', {}).get(key)
        for key in ('registration_sites', 'PE_instruction_verified_sites', 'target')}
    result['scanned_pyc_members'] = demand['pyc_members']
    result['scanned_code_objects'] = demand['code_objects']
    write_json(os.path.join(args.output_dir, 'native-interface-audit.json'), result)
    missing = [{'native_path': group['native_path'], 'status': group['status'],
        'priority': group['priority'], 'difference_status': group['difference_status'], 'site_count': group['site_count'],
        'unguarded_client_site_count': group['unguarded_client_site_count'],
        'first_sites': [{key: site[key] for key in ('member', 'code_path', 'line', 'offset', 'guards', 'evidence')}
            for site in group['sites'][:8]]}
        for group in result['groups'] if any(word in group['status'] for word in ('absent', 'not-found'))]
    write_json(os.path.join(args.output_dir, 'missing-candidates.json'), missing)
    print(json.dumps({'pyc_members': demand['pyc_members'], 'code_objects': demand['code_objects'],
        'references': len(demand['references']), 'modules': registry['modules'],
        'status_counts': result['summary'], 'missing_candidates': len(missing),
        'difference_counts': dict(collections.Counter(item['difference_status'] for item in missing)),
        'original_PE_verified_sites': registry.get('original_positive_catalog', {}).get('PE_instruction_verified_sites')}, sort_keys=True))

if __name__ == '__main__':
    main()
