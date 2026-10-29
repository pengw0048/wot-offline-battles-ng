"""Keep GPU timing opt-in and contain invalid query batches before publication."""
import argparse
from pathlib import Path

FILES = ('lib/moo/gpu_profiler.cpp', 'lib/moo/gpu_profiler.hpp', 'lib/moo/gpu_profiler.ipp')


def replace_once(text, before, after):
    if text.count(after) == 1 and before not in text:
        return text
    if text.count(before) != 1 or after in text:
        raise ValueError('Pinned GPU profiler seam changed: ' + before[:100])
    return text.replace(before, after, 1)


QUERY_READER = '''// Publish only complete, successful timing queries. Device loss or a failed
// query invalidates this GPU batch without changing the CPU profile tree.
static bool readGPUProfileQuery( IDirect3DQuery9 * query, void * value, DWORD size )
{
    HRESULT result;
    do
    {
        result = query->GetData( value, size, D3DGETDATA_FLUSH );
    }
    while (result == S_FALSE &&
        Moo::rc().device()->TestCooperativeLevel() != D3DERR_DEVICELOST);
    return result == S_OK;
}

'''

BEGIN_FRAME = '''void GPUProfiler::beginFrame()
{
    BW_GUARD;
    PROFILER_SCOPED(GPUPRofiler_beginFrame);
    if (!initialized_ || !resourcesCreated_)
    {
        return;
    }

    MF_ASSERT( !inFrame_ );
    if (!g_profiler.watcherEnabled())
    {
        for (size_t i = 0; i < MAX_GPU_FRAME_COUNT; ++i)
        {
            profilerFrames_[i].timingReady_ = false;
        }
        return;
    }

    inFrame_ = true;
    ++frameSerial_;
    curFrame_ = (curFrame_ + 1) % frameStore_;
    GPUProfileFrame & frame = profilerFrames_[curFrame_];
    frame.timingReady_ = false;
    frame.queriesValid_ = SUCCEEDED( frame.disjointQuery_->Issue( D3DISSUE_BEGIN ) );
    frame.queriesValid_ = SUCCEEDED( frame.frameBeginQuery_->Issue( D3DISSUE_END ) ) &&
        frame.queriesValid_;
    frame.queryCount_ = 0;
    frame.recordsCount_ = 0;
}'''

BEGIN_SCOPE = '''uint64 GPUProfiler::beginScope(const char* name)
{
    if (!initialized_ || !resourcesCreated_ || !inFrame_ ||
        !g_profiler.watcherEnabled())
    {
        return uint64( -1 );
    }

    GPUProfileFrame & frame = profilerFrames_[curFrame_];
    if (frame.recordsCount_ == MAX_GPU_SCOPE_COUNT)
    {
        return uint64( -1 );
    }

    const size_t id = frame.recordsCount_++;
    GPUProfilerRecord & gpuRecord = frame.profilerRecords_[id];
    gpuRecord.name_ = name;
    gpuRecord.closed_ = false;
#ifdef GPU_COUNTERS
    gpuRecord.numDrawPrimCalls_[0] = rc().liveProfilingData().nDrawcalls_;
    gpuRecord.numPrimitives_[0] = rc().liveProfilingData().nPrimitives_;
#else
    g_profiler.addEntry(s_primCountName, Profiler::EVENT_COUNTER, rc().liveProfilingData().nPrimitives_);
    g_profiler.addEntry(s_drawPrimCallsName, Profiler::EVENT_COUNTER, rc().liveProfilingData().nDrawcalls_);
#endif
    frame.queriesValid_ = SUCCEEDED( gpuRecord.startQuery_->Issue( D3DISSUE_END ) ) &&
        frame.queriesValid_;
    gpuRecord.startIndex_ = frame.queryCount_++;
    return frameSerial_ * MAX_GPU_SCOPE_COUNT + id;
}'''

END_SCOPE = '''void GPUProfiler::endScope(uint64 token)
{
    if (!initialized_ || !resourcesCreated_ || !inFrame_ ||
        token == uint64( -1 ) || token / MAX_GPU_SCOPE_COUNT != frameSerial_)
    {
        return;
    }

    GPUProfileFrame & frame = profilerFrames_[curFrame_];
    const size_t id = size_t( token % MAX_GPU_SCOPE_COUNT );
    if (id >= frame.recordsCount_ || frame.profilerRecords_[id].closed_)
    {
        return;
    }
    GPUProfilerRecord & record = frame.profilerRecords_[id];
    record.closed_ = true;
    frame.queriesValid_ = SUCCEEDED( record.endQuery_->Issue( D3DISSUE_END ) ) &&
        frame.queriesValid_;
    record.endIndex_ = frame.queryCount_++;
#ifdef GPU_COUNTERS
    record.numDrawPrimCalls_[1] = rc().liveProfilingData().nDrawcalls_;
    record.numPrimitives_[1] = rc().liveProfilingData().nPrimitives_;
#else
    g_profiler.addEntry(s_primCountName, Profiler::EVENT_COUNTER, rc().liveProfilingData().nPrimitives_);
    g_profiler.addEntry(s_drawPrimCallsName, Profiler::EVENT_COUNTER, rc().liveProfilingData().nDrawcalls_);
#endif
}'''


# The native query adapter can return successful timestamps that do not form a
# nested timeline. Validate the complete batch before adding even its root.
VALIDATE_EVENTS = '''    int64 previousTime = startEvent.time_;
    const char * openScopes[MAX_GPU_SCOPE_COUNT];
    size_t depth = 0;
    const size_t eventCount = queryCount * QUERY_MULTIPLIER;
    for (size_t e = 0; e < eventCount; ++e)
    {
        const GPUProfileEvent & event = events[e];
        if (event.time_ < previousTime || event.time_ > endEvent.time_)
        {
            return;
        }
        previousTime = event.time_;
        if (event.event_ == Profiler::EVENT_START)
        {
            if (depth == MAX_GPU_SCOPE_COUNT) return;
            openScopes[depth++] = event.name_;
        }
        else if (event.event_ == Profiler::EVENT_END)
        {
            if (depth == 0 || openScopes[depth - 1] != event.name_) return;
            --depth;
        }
    }
    if (depth != 0) return;

    g_profiler.addGPUEntry( name, startEvent.time_, Profiler::EVENT_START );
'''


def function(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def patch_sources(sources):
    cpp, hpp, ipp = (sources[name] for name in FILES)
    marker = '// GPU profiling is opt-in; rejected query batches never reach the CPU profiler.'
    if marker in cpp:
        if cpp.count(marker) != 1 or 'uint64 frameSerial_;' not in hpp or 'double(time-startOfGPUFrameTimeStamp_)' not in ipp:
            raise ValueError('Partial GPU profiler patch')
        return dict(sources)
    cpp = replace_once(cpp, '#include "cstdmf/profiler.hpp"',
                       '#include "cstdmf/profiler.hpp"\n#include <limits>\n\n' + marker)
    cpp = replace_once(cpp, '\tinFrame_( false )', '\tinFrame_( false ),\n\tframeSerial_( 0 )')
    if cpp.count('for (size_t i = 0; i < frameStore_; ++i)') != 2:
        raise ValueError('Pinned GPU query resource loops changed')
    cpp = cpp.replace('for (size_t i = 0; i < frameStore_; ++i)',
                      'for (size_t i = 0; i < MAX_GPU_FRAME_COUNT; ++i)')
    cpp = replace_once(cpp, function(cpp, 'void GPUProfiler::beginFrame()'), BEGIN_FRAME)
    cpp = replace_once(cpp, function(cpp, 'int GPUProfiler::beginScope('), BEGIN_SCOPE)
    cpp = replace_once(cpp, function(cpp, 'void GPUProfiler::endScope('), END_SCOPE)
    cpp = replace_once(cpp, "static const char* s_primCountName", QUERY_READER + "static const char* s_primCountName")
    cpp = replace_once(cpp, '''\tGPUProfileFrame& frame = profilerFrames_[curFrame_];

\tframe.frameEndQuery_->Issue(D3DISSUE_END);
\tframe.freqQuery_->Issue(D3DISSUE_END);
\tframe.disjointQuery_->Issue(D3DISSUE_END);

\tGPUProfileFrame& frameToProcess = profilerFrames_[(curFrame_+1)%frameStore_];''', '''\tGPUProfileFrame& frame = profilerFrames_[curFrame_];
    frame.queriesValid_ = SUCCEEDED( frame.frameEndQuery_->Issue( D3DISSUE_END ) ) && frame.queriesValid_;
    frame.queriesValid_ = SUCCEEDED( frame.freqQuery_->Issue( D3DISSUE_END ) ) && frame.queriesValid_;
    frame.queriesValid_ = SUCCEEDED( frame.disjointQuery_->Issue( D3DISSUE_END ) ) && frame.queriesValid_;
    frame.timingReady_ = frame.queriesValid_ && frame.queryCount_ == 2 * frame.recordsCount_;
    if (!g_profiler.watcherEnabled())
    {
        for (size_t i = 0; i < MAX_GPU_FRAME_COUNT; ++i)
        {
            profilerFrames_[i].timingReady_ = false;
        }
        return;
    }

\tGPUProfileFrame& frameToProcess = profilerFrames_[(curFrame_+1)%frameStore_];
    if (!frameToProcess.timingReady_) return;
    frameToProcess.timingReady_ = false;''')
    cpp = replace_once(cpp, '''\twhile (frameToProcess.disjointQuery_->GetData((void*)&disjoint, sizeof(BOOL), D3DGETDATA_FLUSH)==S_FALSE
\t\t&& Moo::rc().device()->TestCooperativeLevel() != D3DERR_DEVICELOST);

\tif (disjoint)''', '''    if (!readGPUProfileQuery( frameToProcess.disjointQuery_, &disjoint, sizeof(BOOL) )) return;

\tif (disjoint)''')
    cpp = replace_once(cpp, '''\twhile (frameToProcess.freqQuery_->GetData((void*)&GPUProfileEvent::gpuFrequency_, sizeof(UINT64), D3DGETDATA_FLUSH)==S_FALSE
\t\t&& Moo::rc().device()->TestCooperativeLevel() != D3DERR_DEVICELOST);''', '''    if (!readGPUProfileQuery( frameToProcess.freqQuery_, &GPUProfileEvent::gpuFrequency_, sizeof(UINT64) ) ||
        GPUProfileEvent::gpuFrequency_ == 0) return;''')
    cpp = replace_once(cpp, '''\twhile (frameToProcess.frameBeginQuery_->GetData((void*)&frameStartTime, sizeof(UINT64), D3DGETDATA_FLUSH)==S_FALSE
\t\t&& Moo::rc().device()->TestCooperativeLevel() != D3DERR_DEVICELOST);
\twhile (frameToProcess.frameEndQuery_->GetData((void*)&frameEndTime, sizeof(UINT64), D3DGETDATA_FLUSH)==S_FALSE
\t\t&& Moo::rc().device()->TestCooperativeLevel() != D3DERR_DEVICELOST);''', '''    if (!readGPUProfileQuery( frameToProcess.frameBeginQuery_, &frameStartTime, sizeof(UINT64) ) ||
        !readGPUProfileQuery( frameToProcess.frameEndQuery_, &frameEndTime, sizeof(UINT64) ) ||
        frameEndTime < frameStartTime) return;
    const double frameCycles = double(frameEndTime - frameStartTime) *
        double(GPUProfileEvent::stampsPerSecond_) / double(GPUProfileEvent::gpuFrequency_);
    if (frameCycles >= double(std::numeric_limits<int64>::max())) return;''')
    cpp = replace_once(cpp, '''\tsize_t recordsCount = frameToProcess.recordsCount_;
''', '''\tsize_t recordsCount = frameToProcess.recordsCount_;
    if (recordsCount > MAX_GPU_SCOPE_COUNT || queryCount != 2 * recordsCount) return;
    bool populated[2 * MAX_GPU_SCOPE_COUNT] = {};
''')
    cpp = replace_once(cpp, '''\t\twhile (gpuRecord.startQuery_->GetData( (void*)&startTime, sizeof(UINT64), D3DGETDATA_FLUSH)==S_FALSE
\t\t\t&& Moo::rc().device()->TestCooperativeLevel() != D3DERR_DEVICELOST);
\t\twhile (gpuRecord.endQuery_->GetData( (void*)&endTime, sizeof(UINT64), D3DGETDATA_FLUSH)==S_FALSE
\t\t\t&& Moo::rc().device()->TestCooperativeLevel() != D3DERR_DEVICELOST);''', '''        if (!gpuRecord.closed_ ||
            !readGPUProfileQuery( gpuRecord.startQuery_, &startTime, sizeof(UINT64) ) ||
            !readGPUProfileQuery( gpuRecord.endQuery_, &endTime, sizeof(UINT64) ) ||
            startTime < frameStartTime || endTime < startTime || endTime > frameEndTime) return;
        if (gpuRecord.startIndex_ >= queryCount || gpuRecord.endIndex_ >= queryCount ||
            gpuRecord.startIndex_ >= gpuRecord.endIndex_ ||
            populated[gpuRecord.startIndex_] || populated[gpuRecord.endIndex_]) return;
        populated[gpuRecord.startIndex_] = populated[gpuRecord.endIndex_] = true;''')
    cpp = replace_once(cpp, '''\tg_profiler.addGPUEntry( name, startEvent.time_ , Profiler::EVENT_START );

\tsize_t eventCount = queryCount * QUERY_MULTIPLIER;''', VALIDATE_EVENTS)
    cpp = replace_once(cpp, '''void GPUProfiler::deleteUnmanagedObjects()
{
\tBW_GUARD;''', '''void GPUProfiler::deleteUnmanagedObjects()
{
\tBW_GUARD;
    inFrame_ = false;
    for (size_t i = 0; i < MAX_GPU_FRAME_COUNT; ++i)
    {
        profilerFrames_[i].timingReady_ = false;
    }''')
    hpp = replace_once(hpp, '\tint  beginScope( const char* name );\n\tvoid endScope( int id );',
                       '\tuint64 beginScope( const char* name );\n\tvoid endScope( uint64 token );')
    hpp = replace_once(hpp, '\t\tsize_t\t\t\t \tendIndex_;',
                       '\t\tsize_t\t\t\t \tendIndex_;\n\t\tbool closed_;')
    hpp = replace_once(hpp, '\t\tsize_t\t\t\t\t\t\trecordsCount_;',
                       '\t\tsize_t\t\t\t\t\t\trecordsCount_;\n\t\tbool timingReady_;\n\t\tbool queriesValid_;')
    hpp = replace_once(hpp, '\tbool\t\t\t\t\tinFrame_;',
                       '\tbool\t\t\t\t\tinFrame_;\n\tuint64 frameSerial_;')
    hpp = replace_once(hpp, '\tint scopeID_;', '\tuint64 scopeID_;')
    ipp = replace_once(ipp, '(time-startOfGPUFrameTimeStamp_)*stampsPerSecond_/(double)gpuFrequency_',
                       'double(time-startOfGPUFrameTimeStamp_)*double(stampsPerSecond_)/double(gpuFrequency_)')
    result = dict(sources)
    result.update(zip(FILES, (cpp, hpp, ipp)))
    return result


def patch(root):
    root = Path(root)
    sources = {name: (root / name).read_text(encoding='utf-8-sig') for name in FILES}
    changed = patch_sources(sources)
    for name in FILES:
        if changed[name] != sources[name]:
            (root / name).write_text(changed[name], encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
