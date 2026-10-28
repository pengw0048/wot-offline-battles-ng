"""Connect the #1513 TrajectoryDrawer to its GUIApp lifetime and draw phase."""
from pathlib import Path
import argparse


def insert_once(text,marker,addition):
    if addition in text:return text
    if text.count(marker)!=1:raise RuntimeError('TrajectoryDrawer anchor is not unique: '+repr(marker))
    return text.replace(marker,marker+addition)


def patch(root):
    path=Path(root)/'client/gui_app.cpp'
    text=path.read_text()
    text=insert_once(text,'#include "gui_app.hpp"','\n#include "stock_trajectory_drawer.hpp"')
    text=insert_once(text,'    // access simple gui','\n    if (!initialiseStockTrajectoryDrawer()) return false;')
    text=insert_once(text,'void GUIApp::fini()\n{\n    BW_GUARD;','\n    finaliseStockTrajectoryDrawer();')
    text=insert_once(text,'    BW_GUARD_PROFILER( AppTick_GUI );','\n    tickStockTrajectoryDrawer(dRenderTime);')
    # 006338b4 is before 006338c9's binoculars and before Flash composition.
    text=insert_once(text,'    rp().beginGUIDraw();',
        '\n    if (!drawStockTrajectoryDrawer())\n        ERROR_MSG("TrajectoryDrawer: original line draw failed\\n");')
    path.write_text(text)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bigworld_root',type=Path)
    patch(parser.parse_args().bigworld_root)
