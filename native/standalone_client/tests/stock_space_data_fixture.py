"""Prepare private typed-space resources using the exact original definitions."""
import argparse
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[3]


def prepare(client, output):
    sys.path.insert(0, str(ROOT / 'tools'))
    from packed_xml import read_packed_xml, write_packed_xml, PackedElement
    with zipfile.ZipFile(client / 'res/packages/scripts.pkg') as archive:
        originals = {name: archive.read(name) for name in archive.namelist()
                     if name == 'scripts/spaces.xml' or name.startswith('scripts/space_defs/') and name.endswith('.def')}
        aliases = read_packed_xml(archive.read('scripts/entity_defs/alias.xml'))
        boolean = [(name, value) for name, value in aliases.children if name == b'BOOL']
        if len(boolean) != 1:
            raise ValueError('Pinned BOOL alias is unavailable')
        alias = PackedElement(children=boolean)
        # A second real BW alias exercises the production DataType builder.
        alias_bytes = write_packed_xml(alias)
        for fixture in ('original', 'alternate', 'invalid'):
            folder = output / fixture
            for name, data in originals.items():
                path = folder / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            path = folder / 'scripts/entity_defs/alias.xml'
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(alias_bytes)
            # The alias file is private test input, never written into a client.
            if fixture == 'alternate':
                path.write_text('<root><BOOL>UINT8</BOOL><MaskAlias>UINT32</MaskAlias></root>')
                (folder / 'scripts/spaces.xml').write_text('<root><FixtureSpace/></root>')
                (folder / 'scripts/space_defs/FixtureSpace.def').write_text('''<root>
<Parent>GeneralSpaceData</Parent>
<Properties>
 <weather><Type>STRING</Type><Default>rain</Default><Exposed/></weather>
 <itemsVisibilityMask><Type>MaskAlias</Type><Default>7</Default><Exposed>false</Exposed></itemsVisibilityMask>
 <privateLoose><Type>UINT32</Type><Default>100</Default></privateLoose>
</Properties>
</root>''')
            if fixture == 'invalid':
                (folder / 'scripts/spaces.xml').write_text('<root><InvalidSpace/></root>')
                (folder / 'scripts/space_defs/InvalidSpace.def').write_text('''<root><Properties>
<broken><Type>THIS_TYPE_DOES_NOT_EXIST</Type><Exposed/></broken>
</Properties></root>''')
    print('Prepared original, inherited/aliased and invalid typed-space fixtures in %s' % output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('client', type=Path)
    parser.add_argument('output', type=Path)
    options = parser.parse_args()
    prepare(options.client, options.output)
