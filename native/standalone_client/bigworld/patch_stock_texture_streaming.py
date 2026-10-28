#!/usr/bin/env python3
"""Keep unsupported DDS formats on the managed loader and retire failed streams once."""
import argparse
from pathlib import Path

MANAGER = 'lib/moo/texture_streaming_manager.cpp'
TEXTURE = 'lib/moo/streaming_texture.cpp'
OLD_FORMAT = '''\t// We don't understand the format
\tif (header.format_ == D3DFMT_UNKNOWN)
\t{
\t\treturn false;
\t}

\treturn true;
}'''
NEW_FORMAT = '''\t// Streaming copies DDS bytes without conversion. Unsupported device formats
\t// must use the existing managed loader, which can convert through D3DX.
\tif (header.format_ == D3DFMT_UNKNOWN ||
\t\t!Moo::rc().supportsTextureFormat( header.format_ ))
\t{
\t\treturn false;
\t}

\treturn true;
}'''
OLD_RETIRE = '''\tif (TextureStreamingManager * tsManager = streamingManager())
\t{
\t\ttsManager->unregisterTexture( this );\x20
\t}
\tassignTexture( NULL );'''
NEW_RETIRE = '''\tif (TextureStreamingManager * tsManager = streamingManager())
\t{
\t\t// Failed registration already releases its handle before destruction.
\t\tif (streamingTextureHandle_.isValid())
\t\t{
\t\t\ttsManager->unregisterTexture( this );
\t\t}
\t}
\tassignTexture( NULL );'''


def patch(root):
    root = Path(root)
    replacements = ((MANAGER, OLD_FORMAT, NEW_FORMAT),
                    (TEXTURE, OLD_RETIRE, NEW_RETIRE))
    updated = []
    for name, before, after in replacements:
        path = root / name
        text = path.read_text(encoding='utf-8')
        if text.count(after) == 1 and before not in text:
            updated.append((path, text))
        elif text.count(before) == 1 and after not in text:
            updated.append((path, text.replace(before, after, 1)))
        else:
            raise ValueError('Pinned texture streaming owner changed: ' + name)
    for path, text in updated:
        path.write_text(text, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
