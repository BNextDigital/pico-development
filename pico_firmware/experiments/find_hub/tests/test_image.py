import base64
import hashlib
import importlib.util
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import image

class ImageTests(unittest.TestCase):
    def record(self):
        # Synthetic unit-test scalar, not a Google-registered model credential.
        return image.registration('0x123456',base64.b64encode(bytes([1])*32).decode())
    def test_registration_layout_and_hash(self):
        record=self.record()
        self.assertEqual(record[:8],bytes.fromhex('fa57fa57123456ff'))
        self.assertEqual(record[40:],hashlib.sha256(record[:40]).digest())
        self.assertEqual(len(record),72)
    def test_invalid_registration_is_rejected(self):
        for model,key in [('123456','x'),('0x123456','x'),('0x123456',base64.b64encode(bytes(32)).decode()),
                          ('0x123456',base64.b64encode(bytes([255])*32).decode()),('0x123456',None)]:
            with self.assertRaises(ValueError): image.registration(model,key)
    def test_guarded_uf2_cannot_reach_settings_or_bootloader(self):
        data=image.make_uf2(b'\x55'*333,self.record())
        image.inspect_uf2(data)
        addresses=[struct.unpack_from('<I',data,offset+12)[0] for offset in range(0,len(data),512)]
        self.assertEqual(min(addresses),image.APP_START)
        self.assertEqual(max(addresses)+256,image.REG_END)
        self.assertLessEqual(max(addresses)+256,image.SETTINGS_START)
        for forbidden in [0,image.SETTINGS_START,image.PICO_SETTINGS_START,image.BOOT_START,0x10001000]:
            changed=bytearray(data); struct.pack_into('<I',changed,12,forbidden)
            with self.assertRaises(ValueError): image.inspect_uf2(changed)
        changed=bytearray(data); struct.pack_into('<I',changed,28,0)
        with self.assertRaises(ValueError): image.inspect_uf2(changed)
        with self.assertRaises(ValueError): image.inspect_uf2(data[:-1])
    def test_invalid_or_oversize_application_is_rejected(self):
        for binary in [b'',bytes(image.APP_END-image.APP_START+1)]:
            with self.assertRaises(ValueError): image.make_uf2(binary,self.record())
        with self.assertRaises(ValueError): image.make_uf2(b'\x55',bytes(72))
    def test_wrong_board_or_layout_config_is_rejected(self):
        required='''CONFIG_BOARD_TARGET="xiao_ble/nrf52840/sense"
CONFIG_FLASH_LOAD_OFFSET=0x27000
CONFIG_FLASH_LOAD_SIZE=0xb9000
CONFIG_BT_FAST_PAIR_FHN=y
CONFIG_BT_FAST_PAIR_FHN_DULT=y
CONFIG_USE_DT_CODE_PARTITION=y
'''
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'.config'; path.write_text(required); image.verify_config(path)
            for before,after in [('xiao_ble/nrf52840/sense','nrf52840dk/nrf52840'),('0x27000','0x4000'),('0xb9000','0xc5000')]:
                path.write_text(required.replace(before,after))
                with self.assertRaises(ValueError): image.verify_config(path)
    def test_generated_partitions_preserve_normal_pico_settings(self):
        regions={'code_partition':(image.APP_START,image.APP_END-image.APP_START),
                 'bt_fast_pair_partition':(image.REG_START,image.REG_END-image.REG_START),
                 'storage_partition':(image.SETTINGS_START,image.SETTINGS_END-image.SETTINGS_START),
                 'boot_partition':(image.BOOT_START,0x100000-image.BOOT_START),
                 'reserved_partition_0':(0,image.APP_START)}
        text='\n'.join(f'{label}: partition@{start:x} {{ reg = <0x{start:x} 0x{size:x}>; }};' for label,(start,size) in regions.items())
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'zephyr.dts'; path.write_text(text); image.verify_dts(path)
            path.write_text(text.replace('0xe1000 0x8000','0xec000 0x8000'))
            with self.assertRaises(ValueError): image.verify_dts(path)
    @unittest.skipUnless(os.environ.get('PICO_TEST_NRF_DIR'),'Optional pinned SDK format parity check')
    def test_registration_matches_pinned_sdk_tool(self):
        script=Path(os.environ['PICO_TEST_NRF_DIR'])/'scripts/nrf_provision/fast_pair/fp_provision_cli.py'
        spec=importlib.util.spec_from_file_location('sdk_provision',script); module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        from intelhex import IntelHex
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'test.hex'
            module.prepare_fast_pair_provisioning(str(path),image.REG_START,'0x123456',base64.b64encode(bytes([1])*32).decode())
            self.assertEqual(bytes(IntelHex(str(path)).tobinarray(start=image.REG_START,size=72)),self.record())

if __name__=='__main__': unittest.main()
