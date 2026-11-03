#!/usr/bin/env python3
"""Mount owned WoT packages through BigWorld's existing ZIP filesystem."""

import argparse
from pathlib import Path


ORIGINAL = '''if ( BWResource::getExtension( pakPath ) == PACKED_EXTENSION &&
'''
PKG_REPLACEMENT = '''// WoT's .pkg files use the same ZIP container. Keep their authored
		// search order and read the installed resources without copying them.
		if ( (BWResource::getExtension( pakPath ) == PACKED_EXTENSION ||
			BWResource::getExtension( pakPath ) == "pkg") &&
'''
WOTMOD_DECLARATION = '''const bool isWotMod = bw_stricmp(
				BWResource::getExtension( pakPath ).to_string().c_str(), "wotmod" ) == 0;'''
REPLACEMENT = '''// WoT packages use ZIP containers, but .wotmod resources live under res/.
			''' + WOTMOD_DECLARATION + '''
			if ( (BWResource::getExtension( pakPath ) == PACKED_EXTENSION ||
				BWResource::getExtension( pakPath ) == "pkg" ||
				isWotMod) &&
'''
MOUNT_ORIGINAL = '''				baseFileSystem = new ZipFileSystem( pakPath );'''
MOUNT_REPLACEMENT = '''				ZipFileSystemPtr archive = new ZipFileSystem( pakPath );
				if (isWotMod)
				{
					if (archive->dirTest( "res" ))
					{
						// The child retains its read-only parent archive for the mount lifetime.
						baseFileSystem = new ZipFileSystem( archive, "res" );
					}
					else
					{
						CRITICAL_MSG( "BWResourceImpl::postAddPaths: "
							"'%s' has no res directory.\\n", pakPath.c_str() );
					}
				}
				else
				{
					baseFileSystem = archive;
				}'''
STREAM_ORIGINAL = '''	if (parentSystem_)
		return parentSystem_->streamFile( inPath );'''
STREAM_REPLACEMENT = '''	if (internalChildNode())
		return parentZip_->streamFile( tag_ + "/" + inPath );

''' + STREAM_ORIGINAL
FILE_ORIGINAL = '''	if (parentSystem_)
		return parentSystem_->posixFileOpen(path,mode);'''
FILE_REPLACEMENT = '''	if (internalChildNode())
		return parentZip_->posixFileOpen( tag_ + "/" + path, mode );

''' + FILE_ORIGINAL
IMPORT_ORIGINAL = '''		this->addNonResPath( BWResource::getPath( i ) + "/" + path );'''
IMPORT_REPLACEMENT = '''		BW::string root = BWResource::getPath( i );
		if (bw_stricmp( BWResource::getExtension( root ).to_string().c_str(), "wotmod" ) == 0)
			root += "/res";
		this->addNonResPath( root + "/" + path );'''


def patch(engine):
    path = engine / 'lib/resmgr/bwresource.cpp'
    source = path.read_text(encoding='utf-8')
    if not (REPLACEMENT in source and MOUNT_REPLACEMENT in source):
        original = PKG_REPLACEMENT if PKG_REPLACEMENT in source else ORIGINAL
        if source.count(original) != 1 or source.count(MOUNT_ORIGINAL) != 1:
            raise ValueError('pinned BWResource package dispatch has changed')
        source = source.replace(original, REPLACEMENT, 1)
        source = source.replace(MOUNT_ORIGINAL, MOUNT_REPLACEMENT, 1)
    zip_path = engine / 'lib/resmgr/zip_file_system.cpp'
    zip_source = zip_path.read_text(encoding='utf-8')
    for original, replacement in ((STREAM_ORIGINAL, STREAM_REPLACEMENT),
                                  (FILE_ORIGINAL, FILE_REPLACEMENT)):
        if replacement in zip_source:
            continue
        if zip_source.count(original) != 1:
            raise ValueError('pinned ZipFileSystem child read contract has changed')
        zip_source = zip_source.replace(original, replacement, 1)
    import_path = engine / 'lib/pyscript/py_import_paths.cpp'
    import_source = import_path.read_text(encoding='utf-8')
    if IMPORT_REPLACEMENT not in import_source:
        if import_source.count(IMPORT_ORIGINAL) != 1:
            raise ValueError('pinned Python resource import paths have changed')
        import_source = import_source.replace(IMPORT_ORIGINAL, IMPORT_REPLACEMENT, 1)
        import_source = import_source.replace('#include "py_import_paths.hpp"',
            '#include "py_import_paths.hpp"\n#include "cstdmf/string_utils.hpp"', 1)
    path.write_text(source, encoding='utf-8')
    zip_path.write_text(zip_source, encoding='utf-8')
    import_path.write_text(import_source, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine', type=Path)
    patch(parser.parse_args().engine)
