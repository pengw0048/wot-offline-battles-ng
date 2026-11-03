"""Expose the pinned native map geometry to the independent runtime."""
from pathlib import Path


def stage(native):
    native = Path(native)
    source = (native / 'offline_navigation.cpp').read_text(encoding='utf-8')
    start = 'const std::array<Cell, 8> DIRECTIONS = '
    end = 'using Penalties = std::unordered_map<Edge,double,EdgeHash>;'
    if source.count(start) != 1 or source.count(end) != 1:
        raise ValueError('The pinned navigation map geometry boundary changed')
    law = source[source.index(start):source.index(end)]
    for signature in ('Edge edge(', 'double distance(', 'std::vector<Cell> crossed_cells(',
                      'SegmentKey segment_key('):
        if law.count(signature) != 1:
            raise ValueError('The pinned navigation map helper changed: ' + signature)
        law = law.replace(signature, 'inline ' + signature, 1)
    # The search uses fatal motion bits; the synchronous driver also queries
    # other masks over these same exact physical cell crossings.
    signature = 'bool local_motion_hazard(Point start, Point end) const {'
    bits = '(hazards[index(cell)]&9U)'
    if law.count(signature) != 1 or law.count(bits) != 1:
        raise ValueError('The pinned physical cell traversal boundary changed')
    law = law.replace(signature, 'bool local_motion_hazard(Point start, Point end, unsigned mask=9U) const {')
    law = law.replace(bits, '(hazards[index(cell)]&mask)')
    header = '''// Generated from the pinned gameplay native map geometry.
#pragma once
#include "offline_navigation.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
namespace offline_navigation_map_law {
using namespace offline_navigation;
'''
    (native / 'offline_navigation_map_law.hpp').write_text(header + law + '}\n', encoding='utf-8')
