#!/usr/bin/env python3
"""Give the App frame-pacing owner the #1513 special and reduced FPS modes."""

import argparse
from pathlib import Path


def insert(text, anchor, addition, label):
    if addition in text:
        return text
    if text.count(anchor) != 1:
        raise RuntimeError('The reviewed App frame-pacing anchor changed: ' + label)
    return text.replace(anchor, anchor + addition, 1)


HEADER_METHODS = '''
\t// #1513 Hangar special and arena-loading reduced frame pacing.
\tvoid setSpecialFPSMode( float fps );
\tvoid disableSpecialFPSMode();
\tvoid setReducedFpsMode( bool enabled );'''

SOURCE_METHODS = '''


/**
 *	#1513 0061e950: the Hangar caps frames at fps, or at
 *	renderer/reducedFrameRate when fps <= 0.
 */
void App::setSpecialFPSMode( float fps )
{
\tframePacing_.setSpecial( minFrameTime_, fps, frameTimerFreq() );
}


/**
 *	#1513 0061e880: restores the frame time saved by the first special call.
 */
void App::disableSpecialFPSMode()
{
\tframePacing_.disableSpecial( minFrameTime_ );
}


/**
 *	#1513 wg_setReducedFpsMode 00619890 writes the flag read by updateFrame.
 */
void App::setReducedFpsMode( bool enabled )
{
\tframePacing_.setReduced( enabled );
}'''


def patch(root):
    root = Path(root)
    header = root / 'client/app.hpp'
    text = header.read_text()
    text = insert(text, '#include "resmgr/auto_config.hpp"\n',
                  '#include "stock_runtime_services.hpp"\n', 'header include')
    text = insert(text, '\tdouble getRenderTimeNow() const;', HEADER_METHODS, 'public methods')
    text = insert(text, '\tuint64 minFrameTime_;', '\n\twot::StockFramePacing framePacing_;',
                  'frame-pacing state')
    header.write_text(text)

    source = root / 'client/app.cpp'
    text = source.read_text()
    text = insert(text,
        '\tminFrameTime_ = frameRate != 0 ? uint64(frameTimerFreq() / frameRate) : 0;',
        '\n\tint reducedFrameRate = configSection->readInt( "renderer/reducedFrameRate", 0 );'
        '\n\tframePacing_.configure( reducedFrameRate != 0 ?'
        '\n\t\tuint64(frameTimerFreq() / reducedFrameRate) : 0 );',
        'frame-rate configuration')
    text = insert(text, '\t\tsleepTime = max( sleepTime, sleepTime_ );',
                  '\n\t\tsleepTime = framePacing_.sleepTime( sleepTime );', 'frame sleep')
    text = insert(text,
        '\t\tFRAME_TIMER_TYPE( frameTimerValue() - appStartRenderTime_ );\n'
        '\treturn double( deltaRenderTime / frameTimerFreq() );\n}',
        SOURCE_METHODS, 'method definitions')
    source.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
