#!/usr/bin/env python3
"""Give DebugApp the #1513 frame-rate sampling and the getFPS accessor."""

import argparse
from pathlib import Path


PUBLIC_SAMPLING = '''    if (fpsIndex_ >= 50)
        fpsIndex_ = 0;

    fpsCache_[ fpsIndex_ ] = 1.f / dRenderTime;
    dTimeCache_[ fpsIndex_ ] = dRenderTime;

    minFps_ = fpsCache_[0];
    maxFps_ = fpsCache_[0];

    float totalTimeInCache = 0.f;
    for (uint i = 1; i < 50; i++)
    {
        minFps_ = min( minFps_, fpsCache_[i] );
        maxFps_ = max( maxFps_, fpsCache_[i] );
        totalTimeInCache += dTimeCache_[i];
    }

    fpsIndex_++;

    timeSinceFPSUpdate_ += dRenderTime;
    if (timeSinceFPSUpdate_ >= 0.5f)
    {
        fps_ = 1.f / dRenderTime;
        fpsAverage_ = 50.f / totalTimeInCache;
        timeSinceFPSUpdate_ = 0;
    }'''

STOCK_SAMPLING = '''    // #1513 006323a0: fpsIndex_ counts frames; the statistics cover the
    // first min(count, 50) cached samples, so warm-up frames are not zeros.
    const uint32 sampleSlot = fpsIndex_ % 50;
    fpsCache_[ sampleSlot ] = 1.f / dRenderTime;
    dTimeCache_[ sampleSlot ] = dRenderTime;

    minFps_ = fpsCache_[0];
    maxFps_ = fpsCache_[0];

    const uint32 samples = fpsIndex_ < 50 ? fpsIndex_ : 50;
    float totalTimeInCache = dTimeCache_[0];
    for (uint32 i = 1; i < samples; i++)
    {
        if (minFps_ > fpsCache_[i]) minFps_ = fpsCache_[i];
        if (fpsCache_[i] > maxFps_) maxFps_ = fpsCache_[i];
        totalTimeInCache += dTimeCache_[i];
    }

    fpsIndex_++;

    timeSinceFPSUpdate_ += dRenderTime;
    if (timeSinceFPSUpdate_ >= 0.5f)
    {
        timeSinceFPSUpdate_ = 0;
        fps_ = 1.f / dRenderTime_;
        fpsAverage_ = float( samples ) / totalTimeInCache;
    }'''

ACCESSOR = '''

\t// #1513 00631fa0: fps, 50-frame average, minimum and maximum.
\tvoid fpsStatistics( float & fps, float & average,
\t\tfloat & minimum, float & maximum ) const
\t{
\t\tfps = fps_;
\t\taverage = fpsAverage_;
\t\tminimum = minFps_;
\t\tmaximum = maxFps_;
\t}'''


def replace(text, old, new, label):
    if new in text:
        return text
    if text.count(old) != 1:
        raise RuntimeError('The reviewed DebugApp anchor changed: ' + label)
    return text.replace(old, new, 1)


def patch(root):
    root = Path(root)
    header = root / 'client/debug_app.hpp'
    text = header.read_text()
    text = replace(text, '\tstatic DebugApp instance;', '\tstatic DebugApp instance;' + ACCESSOR, 'accessor')
    text = replace(text, '\tint\t\t\t\t\tfpsIndex_;', '\tuint32\t\t\t\tfpsIndex_;', 'frame counter')
    header.write_text(text)

    source = root / 'client/debug_app.cpp'
    text = source.read_text()
    text = replace(text, PUBLIC_SAMPLING, STOCK_SAMPLING, 'frame-rate sampling')
    source.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
