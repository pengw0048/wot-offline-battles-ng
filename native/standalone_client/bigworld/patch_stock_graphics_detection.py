#!/usr/bin/env python3
"""Connect the #1513 graphics detector to device startup and Python."""
import argparse
from pathlib import Path


def patch(root):
    root = Path(root)
    path = root / 'lib/moo/graphics_settings_picker.cpp'
    source = path.read_text(encoding='utf-8-sig')
    include = '#include "stock_graphics_detection.hpp"'
    if include not in source:
        anchor = '#include "graphics_settings_picker.hpp"'
        if source.count(anchor) != 1:
            raise ValueError('Original graphics detector include changed')
        source = source.replace(anchor, anchor + '\n' + include, 1)
    publication = ('\tconfigureStockGraphicsDetection( pConfig,\n'
                   '\t\tBWResource::instance().openSection( s_autoConfigSettingsFileName.value() ) );')
    if publication not in source:
        anchor = '\tinitConfig();\n\treturn pConfig.exists();'
        if source.count(anchor) != 1:
            raise ValueError('Original graphics detector profile publication changed')
        source = source.replace(anchor, '\tinitConfig();\n' + publication + '\n\treturn pConfig.exists();', 1)
    device = root / 'client/device_app.cpp'
    device_source = device.read_text(encoding='utf-8-sig')
    include = '#include "stock_graphics_detection.hpp"'
    if include not in device_source:
        anchor = '#include "device_app.hpp"'
        if device_source.count(anchor) != 1: raise ValueError('DeviceApp include changed')
        device_source = device_source.replace(anchor, anchor + '\n' + include, 1)
    finish = '    finishStockGraphicsDetectionStartup();'
    if finish not in device_source:
        anchor = '    pLensEffectManager_ = LensEffectManagerPtr( new LensEffectManager() );\n\n    return ret;'
        if device_source.count(anchor) != 1: raise ValueError('DeviceApp startup changed')
        device_source = device_source.replace(anchor, anchor.replace('    return ret;', finish + '\n    return ret;'), 1)
    py = root / 'lib/romp/py_graphics_setting.cpp'
    py_source = py.read_text(encoding='utf-8-sig')
    if include not in py_source:
        anchor = '#include "py_graphics_setting.hpp"'
        if py_source.count(anchor) != 1: raise ValueError('Graphics Python include changed')
        py_source = py_source.replace(anchor, anchor + '\n' + include, 1)
    settings_include = '#include "stock_graphics_settings.hpp"'
    if settings_include not in py_source:
        py_source = py_source.replace(include, include + '\n' + settings_include, 1)
    old = ('\tMoo::GraphicsSettingsAutoDetector detector;\n\tdetector.init();\n'
           '\treturn (int)detector.detectBestSettings();')
    new = '\treturn autoDetectStockGraphicsSettings();'
    if new not in py_source:
        if py_source.count(old) != 1: raise ValueError('Graphics Python auto detect changed')
        py_source = py_source.replace(old, new, 1)
    application = Path(__file__).resolve().parent
    copies = ((application / 'stock_graphics_detection.hpp', root / 'lib/stock_graphics_detection.hpp'),
              (application.parent / 'include/wot/stock_graphics_detection.hpp', root / 'lib/wot/stock_graphics_detection.hpp'),
              (application / 'stock_gpu_detection.hpp', root / 'lib/stock_gpu_detection.hpp'),
              (application.parent / 'include/wot/stock_gpu_sampling.hpp', root / 'lib/wot/stock_gpu_sampling.hpp'),
              (application.parent / 'include/wot/stock_gpu_performancer.hpp', root / 'lib/wot/stock_gpu_performancer.hpp'))
    contents = [(destination, original.read_bytes()) for original, destination in copies]
    path.write_text(source, encoding='utf-8')
    device.write_text(device_source, encoding='utf-8')
    py.write_text(py_source, encoding='utf-8')
    for destination, data in contents:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
