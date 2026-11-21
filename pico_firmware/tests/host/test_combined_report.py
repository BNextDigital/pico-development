"""Fault tests for the flashing/layout and footprint gates, no SDK required."""
import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('report', Path(__file__).resolve().parents[2] / 'tools/combined_report.py')
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


def uf2(addr=0x27000, count=1, index=0):
    block = bytearray(512)
    struct.pack_into('<8I', block, 0, 0x0a324655, 0x9e5d5157, 0x2000, addr, 256, index, count, 0xada52840)
    struct.pack_into('<I', block, 508, 0x0ab16f30)
    return block


class ReportTests(unittest.TestCase):
    def test_safe_image(self):
        self.assertEqual(report.verify_uf2(uf2(), 0x27000, 0xeb000), 1)
        report.gate(599 * 1024, 100000, 784 * 1024)

    def test_initial_budget_failure(self):
        for flash, ram in [(700 * 1024, 100000), (600000, 262145)]:
            with self.assertRaises(ValueError):
                report.gate(flash, ram, 784 * 1024)

    def test_reserved_writes_rejected(self):
        for addr in [0, 0xeb000, 0xec000, 0xf4000, 0xeaff0]:
            with self.assertRaises(ValueError):
                report.verify_uf2(uf2(addr), 0x27000, 0xeb000)

    def test_corrupt_incomplete_duplicate_images(self):
        for data in [b'', uf2()[:-1], uf2(count=2), uf2(index=1), uf2(count=2) * 2]:
            with self.assertRaises(ValueError):
                report.verify_uf2(data, 0x27000, 0xeb000)

    def test_partition_changes_rejected(self):
        dts = 'flash0: flash@0 { erase-block-size = < 0x1000 >; partitions {'
        for name, (addr, size) in report.PARTITIONS.items():
            dts += f'{name}: partition@{addr:x} {{ reg = < 0x{addr:x} 0x{size:x} >; }};'
        dts += '};};'
        report.verify_partitions(dts)
        for text in [dts.replace('0xeb000', '0xeb004'), dts.replace('0xec000', '0xe1000'), dts.replace('0x1000 >; partitions', '0x2000 >; partitions')]:
            with self.assertRaises(ValueError):
                report.verify_partitions(text)


if __name__ == '__main__':
    unittest.main()
