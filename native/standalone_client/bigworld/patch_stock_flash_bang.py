"""Attach #1513 flash-bang providers to each actual renderer pipeline."""
import argparse
from pathlib import Path


def replace_once(text, before, after):
    if after in text:
        return text
    if text.count(before) != 1:
        raise RuntimeError('FlashBang owner anchor changed: ' + repr(before))
    return text.replace(before, after, 1)


def patch(root):
    root = Path(root)
    pending = {}

    def edit(relative, before, after):
        path = root / relative
        text = pending.get(path, path.read_text())
        pending[path] = replace_once(text, before, after)

    edit('lib/moo/renderer.hpp', 'class HDRSupport;',
        'class StockFlashBangSupport;\nclass HDRSupport;')
    edit('lib/moo/renderer.hpp', '#include "cstdmf/singleton.hpp"',
        '#include "cstdmf/singleton.hpp"\n#include <memory>')
    edit('lib/moo/renderer.hpp', '    virtual ~IRendererPipeline() = 0 { }',
        '    virtual ~IRendererPipeline() = 0 { }\n'
        '    virtual std::shared_ptr<StockFlashBangSupport> stockFlashBangSupport() const\n'
        '    { return std::shared_ptr<StockFlashBangSupport>(); }')
    for name in ('Deferred', 'Forward'):
        header = 'lib/moo/%s_pipeline.hpp' % name.lower()
        edit(header, '    virtual ~%sPipeline();' % name,
            ('    virtual ~%sPipeline();\n'
             'private:\n    std::shared_ptr<StockFlashBangSupport> m_stockFlashBang;\npublic:\n'
             '    std::shared_ptr<StockFlashBangSupport> stockFlashBangSupport() const override\n'
             '    { return m_stockFlashBang; }') % name)
        cpp = 'lib/moo/%s_pipeline.cpp' % name.lower()
        include = '#include "%s_pipeline.hpp"' % name.lower()
        edit(cpp, include, include + '\n#include "stock_flash_bang.hpp"')
        if name == 'Deferred':
            edit(cpp, '    m_dynamicShadowManager->fini();',
                '    m_stockFlashBang->close();\n    m_stockFlashBang.reset();\n'
                '    m_dynamicShadowManager->fini();')
            edit(cpp, '    MF_WATCH("Render/DS/show g-buffer channel",',
                '    m_stockFlashBang.reset(new StockFlashBangSupport());\n'
                '    MF_WATCH("Render/DS/show g-buffer channel",')
            edit(cpp, '    m_HDRSupport->tick(dt);',
                '    m_HDRSupport->tick(dt);\n'
                '    const auto flashBang = m_stockFlashBang;\n'
                '    flashBang->tick();')
            edit(cpp, '    drawPostDeferred();',
                '    drawPostDeferred();\n'
                '    const auto flashBang = m_stockFlashBang;\n'
                '    if (!flashBang->draw()) ERROR_MSG("FlashBang: scene composition failed\\n");')
        else:
            edit(cpp, 'ForwardPipeline::~ForwardPipeline()\n{',
                'ForwardPipeline::~ForwardPipeline()\n{\n'
                '    m_stockFlashBang->close();\n    m_stockFlashBang.reset();')
            edit(cpp, 'ForwardPipeline::ForwardPipeline()\n{',
                'ForwardPipeline::ForwardPipeline()\n{\n'
                '    m_stockFlashBang.reset(new StockFlashBangSupport());')
            edit(cpp, 'void ForwardPipeline::tick(float /*dt*/)\n{\n    BW_GUARD;',
                'void ForwardPipeline::tick(float /*dt*/)\n{\n    BW_GUARD;\n'
                '    const auto flashBang = m_stockFlashBang;\n'
                '    flashBang->tick();')
            edit(cpp, 'void ForwardPipeline::endSemitransparentDraw()\n{\n    BW_GUARD;',
                'void ForwardPipeline::endSemitransparentDraw()\n{\n    BW_GUARD;\n'
                '    const auto flashBang = m_stockFlashBang;\n'
                '    if (!flashBang->draw()) ERROR_MSG("FlashBang: scene composition failed\\n");')
    edit('client/script_app.cpp', '#include "script_app.hpp"',
        '#include "script_app.hpp"\n#include "stock_flash_bang.hpp"')
    edit('client/script_app.cpp', 'void ScriptApp::fini()\n{\n\tBW_GUARD;',
        'void ScriptApp::fini()\n{\n\tBW_GUARD;\n\tfiniStockFlashBangAnimations();')
    edit('client/script_app.cpp', 'bool ScriptApp::init()\n{\n\tBW_GUARD;',
        'bool ScriptApp::init()\n{\n\tBW_GUARD;\n\tinitStockFlashBangAnimations();')
    for path, text in pending.items():
        path.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bigworld_root', type=Path)
    patch(parser.parse_args().bigworld_root)
