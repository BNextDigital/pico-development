"""Strict UF2 packaging for the isolated XIAO image; never writes settings/bootloaders."""
import base64
import hashlib
import re
import struct
from pathlib import Path

APP_START = 0x27000
APP_END = 0xE0000
REG_START = APP_END
REG_END = 0xE1000
SETTINGS_START = REG_END
SETTINGS_END = 0xE9000
PICO_SETTINGS_START = 0xEC000
BOOT_START = 0xF4000
FAMILY = 0xADA52840
P256_ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551


def registration(model_id: str, encoded_key: str) -> bytes:
    if not isinstance(model_id, str) or not re.fullmatch(r'0x[0-9a-fA-F]{6}', model_id):
        raise ValueError('Model ID must be 0x followed by six hexadecimal digits')
    try:
        key = base64.b64decode(encoded_key, validate=True)
    except (ValueError, TypeError):
        raise ValueError('Anti-spoofing key must be valid base64') from None
    if len(key) != 32 or not 0 < int.from_bytes(key, 'big') < P256_ORDER:
        raise ValueError('Anti-spoofing key must be a valid 32-byte P-256 private scalar')
    # SDK 3.4.2 fp_registration_data.c/fp_provision_cli.py layout.
    body = bytes.fromhex('fa57fa57') + bytes.fromhex(model_id[2:]) + b'\xff' + key
    return body + hashlib.sha256(body).digest()


def verify_config(path: Path) -> None:
    config = path.read_text()
    required = {'CONFIG_BOARD_TARGET': '"xiao_ble/nrf52840/sense"',
                'CONFIG_FLASH_LOAD_OFFSET': '0x27000', 'CONFIG_FLASH_LOAD_SIZE': '0xb9000',
                'CONFIG_BT_FAST_PAIR_FHN': 'y', 'CONFIG_BT_FAST_PAIR_FHN_DULT': 'y',
                'CONFIG_USE_DT_CODE_PARTITION': 'y'}
    entries = dict(re.findall(r'^(CONFIG_\w+)=(.*)$', config, re.M))
    for key, value in required.items():
        if entries.get(key) != value:
            raise ValueError(f'Unexpected {key}: refusing to package this image')
    if entries.get('CONFIG_PARTITION_MANAGER_ENABLED') == 'y':
        raise ValueError('Partition Manager is not supported by this DTS-isolated experiment')
    if entries.get('CONFIG_BOOTLOADER_MCUBOOT') == 'y':
        raise ValueError('MCUboot image cannot be used with this UF2 layout')


def verify_dts(path: Path) -> None:
    text = re.sub(r'/\*.*?\*/', '', path.read_text(), flags=re.S)
    expected = {'code_partition': (APP_START, APP_END-APP_START),
                'bt_fast_pair_partition': (REG_START, REG_END-REG_START),
                'storage_partition': (SETTINGS_START, SETTINGS_END-SETTINGS_START),
                'boot_partition': (BOOT_START, 0x100000-BOOT_START),
                'reserved_partition_0': (0, APP_START)}
    for label, region in expected.items():
        nodes = re.findall(r'\b'+label+r':\s*partition@[0-9a-fA-F]+\s*\{([^{}]*)\}', text, re.S)
        if len(nodes) != 1:
            raise ValueError(f'Expected exactly one {label} in generated devicetree')
        values = re.search(r'\breg\s*=\s*<\s*(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s*>', nodes[0])
        if not values or tuple(int(v,16) for v in values.groups()) != region:
            raise ValueError(f'Unexpected {label} partition; refusing this image')


def make_uf2(application: bytes, record: bytes) -> bytes:
    if not application or len(application) > APP_END - APP_START:
        raise ValueError('Application does not fit the isolated XIAO code partition')
    if len(record) != 72 or record[:4] != bytes.fromhex('fa57fa57') or hashlib.sha256(record[:40]).digest() != record[40:]:
        raise ValueError('Registration record has an invalid format or integrity hash')
    # Contiguous application + registration area avoids gaps in bootloader image accounting.
    payload = bytearray(b'\xff' * (REG_END - APP_START))
    payload[:len(application)] = application
    payload[REG_START-APP_START:REG_START-APP_START+len(record)] = record
    blocks = len(payload) // 256
    output = bytearray()
    for number in range(blocks):
        address = APP_START + number*256
        header = struct.pack('<8I', 0x0A324655, 0x9E5D5157, 0x2000,
                             address, 256, number, blocks, FAMILY)
        output += header + payload[number*256:(number+1)*256] + bytes(220) + struct.pack('<I', 0x0AB16F30)
    inspect_uf2(output)
    return bytes(output)


def inspect_uf2(data: bytes) -> None:
    if not data or len(data) % 512:
        raise ValueError('Invalid UF2 size')
    blocks = len(data)//512
    expected_blocks = (REG_END-APP_START)//256
    if blocks != expected_blocks:
        raise ValueError('UF2 is not the expected contiguous XIAO image')
    for number in range(blocks):
        block = data[number*512:(number+1)*512]
        magic1, magic2, flags, address, size, index, count, family = struct.unpack('<8I', block[:32])
        if (magic1,magic2,flags,size,index,count,family) != (0x0A324655,0x9E5D5157,0x2000,256,number,blocks,FAMILY):
            raise ValueError('Unexpected UF2 header/family/sequence')
        if address != APP_START+number*256 or address+size > REG_END:
            raise ValueError('UF2 would write outside the experiment application/registration area')
        if struct.unpack('<I',block[508:])[0] != 0x0AB16F30:
            raise ValueError('Invalid UF2 trailer')
