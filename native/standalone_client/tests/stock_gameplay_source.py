"""Prepare generated native headers without changing the pinned source input."""
import importlib.util
from pathlib import Path
import shutil
import tempfile

_headers = {}


def navigation_include(source):
    source = Path(source).resolve()
    if source not in _headers:
        temporary = tempfile.TemporaryDirectory(prefix='navigation-law-')
        output = Path(temporary.name)
        for name in ('offline_navigation.cpp', 'offline_navigation.h'):
            shutil.copyfile(source / 'native' / name, output / name)
        path = Path(__file__).resolve().parents[1] / 'bigworld/stage_navigation_map_law.py'
        spec = importlib.util.spec_from_file_location('test_navigation_map_law', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.stage(output)
        _headers[source] = temporary
    return _headers[source].name
