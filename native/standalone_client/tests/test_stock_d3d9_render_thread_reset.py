"""Keep device Reset on the window thread while the render thread owns every other call."""
from pathlib import Path
import re
import unittest


SOURCE = Path(__file__).parents[1] / 'bigworld' / 'stock_d3d9_render_thread.cpp'


class RenderThreadResetTests(unittest.TestCase):
    def setUp(self):
        self.source = SOURCE.read_text()

    def test_reset_and_reset_ex_run_on_the_window_thread(self):
        # D3D9 returns D3DERR_INVALIDCALL for a Reset issued off the window
        # thread, which froze the back buffer at its first size.
        for name, call in (('Reset', r'REAL->Reset\(p\)'), ('ResetEx', r'REAL->ResetEx\(p, m\)')):
            definition = re.search(r'HRESULT WDevice::%s\([^)]*\) \{(.*)\}\n' % name, self.source)
            self.assertIsNotNone(definition, name)
            self.assertRegex(definition.group(1), r'resetOnWindowThread\(REAL, \[&\] \{ return %s; \}' % call)
            self.assertNotIn('rt::sync', definition.group(1))

    def test_render_thread_drains_and_parks_before_the_window_thread_resets(self):
        body = self.source[self.source.index('static HRESULT resetOnWindowThread'):]
        body = body[:body.index('\n}\n')]
        park = body.index('rt::post([] { beforeReset(); SetEvent(parked); WaitForSingleObject(resume, INFINITE); });')
        wait = body.index('WaitForSingleObject(parked, INFINITE);')
        owned = body.index('rt::g_parked = true;')
        reset = body.index('HRESULT h = reset(); refreshShadows(real);', owned)
        released = body.index('rt::g_parked = false;', reset)
        resumed = body.index('SetEvent(resume);', released)
        self.assertLess(park, wait)
        self.assertLess(wait, owned)
        self.assertLess(released, resumed)

    def test_window_thread_runs_device_work_inline_while_it_owns_the_device(self):
        # A window message during Reset must not wait on the parked thread.
        self.assertIn('inline bool mainOwnsDevice() { return g_parked && onMain(); }', self.source)
        self.assertIn('if (!g_started || onRender() || mainOwnsDevice()) return f();', self.source)
        self.assertEqual(self.source.count('if (mainOwnsDevice()) {'), 2)


if __name__ == '__main__':
    unittest.main()
