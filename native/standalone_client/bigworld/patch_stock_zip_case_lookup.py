#!/usr/bin/env python3
"""Restore #1513 canonical ZIP keys and direct map lookups."""
import argparse
import os
from pathlib import Path
import stat
import tempfile


SOURCE = 'lib/resmgr/zip_file_system.cpp'
# #1513 cleanSlashes (0x011aeae0), central-directory insertion (0x011b236a),
# and dirTest (0x011aff44) use the same canonical-key contract. Keep the
# original slash, duplicate, directory, write and teardown algorithms.
SEAMS = (
    (r'''/**
 * This returns a string with all slashes normalised to single forward slashes
 */
void cleanSlashes( BW::string& filename )
{
	BW::string& result = filename;
	BW::string::size_type slashpos;
	// Clean up any backslashes
	std::replace( result.begin(), result.end(), '\\', '/' );
	if ( ( slashpos = result.find( "//", 0 ) ) != BW::string::npos )
	{
		// Clean up any duplicate slashes
		do
		{
			result.replace( slashpos, 2, "/" );
			slashpos = result.find( "//", slashpos );
		} while ( slashpos != BW::string::npos );
	}
	// Get rid of any leading slashes
	if ( !result.empty() && result[ 0 ] == '/' )
		result.erase( 0, 1 );

	// Get rid of any trailing slashes
	if ( !result.empty() && result[ result.length() - 1 ] == '/' )
	{
		result.erase( result.length() - 1, 1 );
	}
//	if ( result != filename )
//		TRACE_MSG( "cleanSlashes: %s => %s\n", filename.c_str(), result.c_str() );
}''',
     r'''/**
 * Canonicalise ZIP keys exactly as #1513 before either insertion or lookup.
 */
void cleanSlashes( BW::string& filename )
{
	// #1513 0x011aeaf2 calls the CRT on the NUL-terminated bytes first.
	if ( !filename.empty() )
		_strlwr( &filename[0] );
	BW::string& result = filename;
	BW::string::size_type slashpos;
	// Clean up any backslashes
	std::replace( result.begin(), result.end(), '\\', '/' );
	if ( ( slashpos = result.find( "//", 0 ) ) != BW::string::npos )
	{
		// Clean up any duplicate slashes
		do
		{
			result.replace( slashpos, 2, "/" );
			slashpos = result.find( "//", slashpos );
		} while ( slashpos != BW::string::npos );
	}
	// Get rid of any leading slashes
	if ( !result.empty() && result[ 0 ] == '/' )
		result.erase( 0, 1 );

	// Get rid of any trailing slashes
	if ( !result.empty() && result[ result.length() - 1 ] == '/' )
	{
		result.erase( result.length() - 1, 1 );
	}
//	if ( result != filename )
//		TRACE_MSG( "cleanSlashes: %s => %s\n", filename.c_str(), result.c_str() );
}'''),
    (r'''

/**
 * This template function performs a slash-safe lookup of a filename
 * in the provided map. It assumes the slashes loaded into the map
 * are correct!
 *
 * In a non-consumer build, it will check for case mismatch and log it
 * as a warning. This will slow down missed lookups considerably.
 */
template< class MapType > inline typename MapType::iterator fileMapLookup( MapType& fileMap, const BW::StringRef& filename )
{
	// Don't handle bad-case lookups for now
	BW::string mapKey = filename.to_string();
	cleanSlashes( mapKey );
#if ENABLE_FILE_CASE_CHECKING
	typename MapType::iterator fileIt = fileMap.find( mapKey );
	if ( fileIt != fileMap.end() )
		return fileIt;
	// Check for a case-mismatch. We can't use FilenameCaseChecker here
	// because it relies on the Windows Shell interface to find the right file
	for( fileIt = fileMap.begin(); fileIt != fileMap.end(); fileIt++ )
	{
		// Early out if we have a different filename length
		if ( mapKey.length() != fileIt->first.length() )
			continue;
		const BW::StringRef& candidateName = fileIt->first;
		if ( candidateName.equals_lower( mapKey ) )
		{
			// We found a match! Complain bitterly, but accept it.
			WARNING_MSG( "Case in filename %s does not match case in zip file %s\n",@SPACE@
				mapKey.c_str(), fileIt->first.to_string().c_str() );
			return fileIt;
		}
	}
	return fileMap.end();
 #else // ENABLE_FILE_CASE_CHECKING
@SPACE@	return fileMap.find( mapKey );
 #endif // ENABLE_FILE_CASE_CHECKING
}


/**
 * This template function performs a slash-safe lookup of a filename
 * in the provided map. It assumes the slashes loaded into the map
 * are correct!
 *
 * In a non-consumer build, it will check for case mismatch and log it
 * as a warning. This will slow down missed lookups considerably.
 */
template< class MapType > inline typename MapType::const_iterator fileMapLookupConst( const MapType& fileMap, const BW::StringRef& filename )
{
	// Don't handle bad-case lookups for now
	BW::string mapKey = filename.to_string();
	cleanSlashes( mapKey );
#if ENABLE_FILE_CASE_CHECKING
	typename MapType::const_iterator fileIt = fileMap.find( mapKey );
	if ( fileIt != fileMap.end() )
		return fileIt;
	for( fileIt = fileMap.begin(); fileIt != fileMap.end(); fileIt++ )
	{
		// Early out if we have a different filename length
		if ( mapKey.length() != fileIt->first.length() )
			continue;
		const BW::StringRef& candidateName = fileIt->first;
		if ( candidateName.equals_lower( mapKey ) )
		{
			// We found a match! Complain bitterly, but accept it.
			WARNING_MSG( "Case in filename %s does not match case in zip file %s\n",@SPACE@
				mapKey.c_str(), fileIt->first.to_string().c_str() );
			return fileIt;
		}
	}
	return fileMap.end();
#else // ENABLE_FILE_CASE_CHECKING
	return fileMap.find( mapKey );
#endif // ENABLE_FILE_CASE_CHECKING
}
''',
     r'''

/**
 * ZIP producers and consumers share cleanSlashes, including case folding.
 * #1513 stores the last central-directory entry under each canonical key.
 */
template< class MapType > inline typename MapType::iterator fileMapLookup( MapType& fileMap, const BW::StringRef& filename )
{
	BW::string mapKey = filename.to_string();
	cleanSlashes( mapKey );
	return fileMap.find( mapKey );
}


template< class MapType > inline typename MapType::const_iterator fileMapLookupConst( const MapType& fileMap, const BW::StringRef& filename )
{
	BW::string mapKey = filename.to_string();
	cleanSlashes( mapKey );
	return fileMap.find( mapKey );
}'''),
    (r'''bool ZipFileSystem::dirTest( const BW::StringRef& name )
{@SPACE@
	// If this isn't the root of the current zip, then
	// we need to iterate up until we find it since the root
	// owns the directory map.
	if ( internalChildNode() )
	{
		return parentZip_->dirTest( tag_ + "/" + name );
	}
	else
	{
		return dirMap_.find( name) != dirMap_.end();
	}
}''',
     r'''bool ZipFileSystem::dirTest( const BW::StringRef& name )
{@SPACE@
	// If this isn't the root of the current zip, then
	// we need to iterate up until we find it since the root
	// owns the directory map.
	if ( internalChildNode() )
	{
		return parentZip_->dirTest( tag_ + "/" + name );
	}
	else
	{
		return fileMapLookup( dirMap_, name ) != dirMap_.end();
	}
}'''),
)

# Preserve literal pinned-source spacing without trailing source whitespace.
SEAMS = tuple(tuple(text.replace('@SPACE@', ' ') for text in pair)
              for pair in SEAMS)


def _matches(source, final):
    # Reverse final seams and revalidate their unique original boundaries.
    if any(source.count(pair[bool(final)]) != 1 for pair in SEAMS):
        return False
    if final:
        for before, after in SEAMS:
            source = source.replace(after, before, 1)
    return (all(source.count(before) == 1 for before, after in SEAMS) and
            not any(after in source for before, after in SEAMS))


def patch_source(source):
    if _matches(source, True):
        return source
    if not _matches(source, False):
        raise ValueError('Pinned ZIP canonical-key seam is partial, duplicated or changed')
    for before, after in SEAMS:
        source = source.replace(before, after, 1)
    if not _matches(source, True):
        raise ValueError('ZIP canonical-key patch did not reach its final state')
    return source


def patch(root):
    path = Path(root) / SOURCE
    source = path.read_text(encoding='utf-8')
    patched = patch_source(source)
    if patched == source:
        return False
    # A changed seam or failed write must leave the input file untouched.
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8',
                                         dir=path.parent, prefix=path.name + '.',
                                         delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(patched)
        temporary.chmod(stat.S_IMODE(path.stat().st_mode))
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()
    return True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
