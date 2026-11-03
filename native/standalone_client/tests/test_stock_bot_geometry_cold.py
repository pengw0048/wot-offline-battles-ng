"""Opt-in complete catalogue audit; requires owned exact resources and Python 2.7."""
import os
import shutil
import sys
import unittest

from audit_stock_bot_geometry_cold import audit


class ColdBotResourceTests(unittest.TestCase):
    def test_every_plan_and_siege_member_reaches_resource_backed_geometry(self):
        required = ('WOT_0922_CLIENT','WOT_FORK_SOURCE','WOT_PYTHON27')
        if not all(os.environ.get(name) for name in required):
            self.skipTest('set ' + ', '.join(required) + ' for the complete exact-resource cold audit')
        if sys.platform == 'win32' or not shutil.which('c++'):
            self.skipTest('the existing CPython 2.7 BSP host requires a Unix C++ compiler')
        report = audit(*(os.environ[name] for name in required))
        self.assertEqual(report['plans'], report['vehicles']*4)
        self.assertEqual(report['siege_descriptors'], 16)
        self.assertEqual(report['descriptors'],report['plans']+report['siege_descriptors'])
        self.assertGreater(report['cold_testers'],report['plans'])
        print('Complete cold Bot resource audit:', report)


if __name__ == '__main__':
    unittest.main()
