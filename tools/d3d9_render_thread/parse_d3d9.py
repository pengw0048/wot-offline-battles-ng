"""Parse mingw d3d9.h COM interface declarations into (interface, base, [(ret, name, [(type, pname)])])."""
import re, sys, json
H = sys.argv[1]  # mingw-w64 i686 include/d3d9.h
text = open(H).read()
ifaces = {}
for m in re.finditer(r'DECLARE_INTERFACE_IID_\((\w+),\s*(\w+),\s*"[^"]+"\)\s*\{(.*?)\n\};', text, re.S):
    name, base, body = m.group(1), m.group(2), m.group(3)
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    methods = []
    for mm in re.finditer(r'STDMETHOD(_)?\(([^)]*?)\)\s*\(THIS_?\s*(.*?)\)\s*PURE;', body, re.S):
        if mm.group(1):
            ret, mname = [x.strip() for x in mm.group(2).split(',', 1)]
        else:
            ret, mname = 'HRESULT', mm.group(2).strip()
        args = ' '.join(mm.group(3).split())
        params = []
        if args:
            for i, a in enumerate(args.split(',')):
                a = a.strip().replace('struct ', '')
                pm = re.match(r'(.*?)(\w+)\s*$', a)
                if not pm or a.endswith('*'):
                    typ, pn = a, 'a%d' % i
                else:
                    typ, pn = pm.group(1).strip(), pm.group(2)
                    if not typ: typ, pn = pn, 'a%d' % i
                params.append((typ, pn))
        # mingw spells the SDK's BOOL as WINBOOL.
        params = [(t.replace('WINBOOL', 'BOOL'), n) for t, n in params]
        methods.append((ret.replace('WINBOOL', 'BOOL'), mname, params))
    ifaces[name] = (base, methods)
json.dump(ifaces, open(__import__('pathlib').Path(__file__).with_name('d3d9_ifaces.json'), 'w'), indent=1)
for k, (b, ms) in ifaces.items(): print(k, b, len(ms))
