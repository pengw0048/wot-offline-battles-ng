#!/usr/bin/env python3
"""Retain loaded bitmap resource owners in runtime render fills and their snapshots."""
import argparse
from pathlib import Path

BITMAP = 'Src/GFx/AS3/Obj/Display/AS3_Obj_Display_BitmapData.h'
GRAPHICS = 'Src/GFx/AS3/Obj/Display/AS3_Obj_Display_Graphics.cpp'
DRAW_H = 'Src/GFx/GFx_DrawingContext.h'
DRAW_CPP = 'Src/GFx/GFx_DrawingContext.cpp'
FILL = 'Src/Render/Render_ShapeDataDefs.h'
AVM_BITMAP = 'Src/GFx/AS3/AS3_AvmBitmap.cpp'

OWNER = '''// Runtime bitmap fills may outlive BitmapData and its loaded-image MovieDef.
// Keep that resource lease on the render fill, without retaining an AS3 object.
class BitmapFillResourceOwner : public RefCountBase<BitmapFillResourceOwner, Stat_Default_Mem>
{
    Ptr<MovieDefImpl> Definition;
public:
    explicit BitmapFillResourceOwner(MovieDefImpl* definition) : Definition(definition) {}
};

Ptr<RefCountImpl> CreateBitmapFillResourceOwner(MovieDefImpl* definition)
{
    if (!definition)
        return Ptr<RefCountImpl>();
    return *SF_NEW BitmapFillResourceOwner(definition);
}

'''
ASSIGNMENT = '''
    ComplexFill& operator=(const ComplexFill& other)
    {
        if (this != &other)
        {
            // Preserve both leases until every old render payload has retired.
            Ptr<RefCountImpl> nextOwner(other.pImageOwner);
            pImage = other.pImage;
            pGradient = other.pGradient;
            ImageMatrix = other.ImageMatrix;
            FillMode = other.FillMode;
            BindIndex = other.BindIndex;
            pImageOwner = nextOwner;
        }
        return *this;
    }
'''
SEAMS = (
    (BITMAP, '        ImageResource*  GetImageResource();',
     '        ImageResource*  GetImageResource();\n'
     '        MovieDefImpl* GetResourceMovieDef() const { return pDefImpl.GetPtr(); }'),
    (GRAPHICS, '        pDrawing->BeginBitmapFill(fillType, pimageRes, mtxF);',
     '        pDrawing->BeginBitmapFill(fillType, pimageRes, mtxF, bitmap->GetResourceMovieDef());'),
    (DRAW_H, 'class ImageResource;\n', 'class ImageResource;\nclass MovieDefImpl;\n'
     'Ptr<RefCountImpl> CreateBitmapFillResourceOwner(MovieDefImpl* definition);\n'),
    (DRAW_H, '                            const Matrix& mtx);',
     '                            const Matrix& mtx, MovieDefImpl* resourceOwner = NULL);'),
    (DRAW_CPP, '#include "GFx/GFx_ImageResource.h"',
     '#include "GFx/GFx_ImageResource.h"\n#include "GFx/GFx_MovieDef.h"'),
    (DRAW_CPP, 'DrawingContext::DrawingContext(Render::Context& renCtxt, ImageCreator* imgCreator) :',
     OWNER + 'DrawingContext::DrawingContext(Render::Context& renCtxt, ImageCreator* imgCreator) :'),
    (DRAW_CPP, '                                    const Matrix& mtx)\n',
     '                                    const Matrix& mtx, MovieDefImpl* resourceOwner)\n'),
    (DRAW_CPP, '        fs.pFill->pImage = img;',
     '        fs.pFill->pImageOwner = CreateBitmapFillResourceOwner(resourceOwner);\n'
     '        fs.pFill->pImage = img;'),
    (FILL, '    Ptr<Image>          pImage;\n',
     '    // Declared first so every image is destroyed before its resource heap owner.\n'
     '    Ptr<RefCountImpl>   pImageOwner;\n    Ptr<Image>          pImage;\n'),
    (FILL, '    ComplexFill(const ComplexFill& o) : pImage(o.pImage), pGradient(o.pGradient),',
     '    ComplexFill(const ComplexFill& o) : pImageOwner(o.pImageOwner), pImage(o.pImage), pGradient(o.pGradient),'),
    (FILL, '        ImageMatrix(o.ImageMatrix), FillMode(o.FillMode), BindIndex(o.BindIndex) {}\n};',
     '        ImageMatrix(o.ImageMatrix), FillMode(o.FillMode), BindIndex(o.BindIndex) {}\n' + ASSIGNMENT + '};'),
    (AVM_BITMAP, '#include "GFx/AS3/AS3_AvmBitmap.h"',
     '#include "GFx/AS3/AS3_AvmBitmap.h"\n#include "GFx/GFx_DrawingContext.h"'),
    (AVM_BITMAP, '        fill.pFill->pImage = img;',
     '        fill.pFill->pImageOwner = CreateBitmapFillResourceOwner(pDefImpl);\n'
     '        fill.pFill->pImage = img;'),
)
FILES = tuple(dict.fromkeys(name for name, unused, unused2 in SEAMS))


def patch_sources(sources):
    final = all(sources[name].count(after) == 1 and sources[name].count(before) == after.count(before)
                for name, before, after in SEAMS)
    if final:
        return dict(sources)
    pristine = all(sources[name].count(before) == 1 and sources[name].count(after) == 0
                   for name, before, after in SEAMS)
    if not pristine:
        raise ValueError('Pinned runtime bitmap fill owner seams are partial, duplicated or changed')
    result = dict(sources)
    for name, before, after in SEAMS:
        result[name] = result[name].replace(before, after, 1)
    return result


def patch(root):
    root = Path(root)
    sources = {name: (root/name).read_text(encoding='utf-8-sig') for name in FILES}
    result = patch_sources(sources)
    for name in FILES:
        if result[name] != sources[name]:
            (root/name).write_text(result[name], encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdk_root', type=Path)
    patch(parser.parse_args().sdk_root)
