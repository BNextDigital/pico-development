"""Check linked resource budgets, partition geometry and UF2 write addresses."""
import hashlib
import re
import struct
from pathlib import Path

TARGET = 600 * 1024
THRESHOLD = 700 * 1024
PARTITIONS = {
    'reserved_partition_0': (0, 0x27000),
    'code_partition': (0x27000, 0xc4000),
    'bt_fast_pair_partition': (0xeb000, 0x1000),
    'storage_partition': (0xec000, 0x8000),
    'boot_partition': (0xf4000, 0xc000),
}


def verify_partitions(dts):
    for name, expected in PARTITIONS.items():
        match = re.search(r'\b' + name + r':\s*partition@\w+\s*\{([^}]+)\}', dts)
        if not match:
            raise ValueError('Missing partition: ' + name)
        cells = re.search(r'reg\s*=\s*<\s*(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s*>', match[1])
        if not cells or tuple(int(x, 16) for x in cells.groups()) != expected:
            raise ValueError('Unexpected partition address/size: ' + name)
        if any(value % 4096 for value in expected):
            raise ValueError('Unaligned partition: ' + name)
    flash = re.search(r'flash0:\s*flash@0\s*\{(.+?)partitions\s*\{', dts, re.S)
    if not flash or not re.search(r'erase-block-size\s*=\s*<\s*0x1000\s*>', flash[1]):
        raise ValueError('Internal erase geometry is not 4 KiB')


def verify_uf2(data, start, end):
    if not data or len(data) % 512:
        raise ValueError('Invalid UF2 length')
    count = len(data) // 512
    seen = set()
    ranges = []
    for i in range(count):
        fields = struct.unpack_from('<8I', data, i * 512)
        magic0, magic1, flags, addr, size, index, total, family = fields
        if (magic0, magic1) != (0x0a324655, 0x9e5d5157) or struct.unpack_from('<I', data, i * 512 + 508)[0] != 0x0ab16f30:
            raise ValueError('Invalid UF2 magic')
        if flags != 0x2000 or family != 0xada52840 or not 0 < size <= 476:
            raise ValueError('Unsupported UF2 block flags/family/payload')
        if total != count or index >= count or index in seen:
            raise ValueError('Incomplete/duplicate UF2 block')
        if addr < start or addr + size > end:
            raise ValueError('UF2 writes outside application partition')
        seen.add(index)
        ranges.append((addr, addr + size))
    ranges.sort()
    if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
        raise ValueError('Overlapping UF2 payloads')
    return count


def gate(flash, ram, capacity):
    if flash >= THRESHOLD:
        raise ValueError('Linked application reached initial 700 KiB failure threshold')
    if flash > capacity or ram > 256 * 1024:
        raise ValueError('Image exceeds a physical memory region')


def measure(directory, combined=True):
    from elftools.elf.elffile import ELFFile
    directory = Path(directory)
    config = (directory / '.config').read_text()
    if ('CONFIG_PICO_FIND_HUB=y' in config) != combined:
        raise ValueError('Profile argument does not match linked configuration')
    for symbol in ('PICO_MICROPHONE', 'PICO_MOTION', 'PICO_AUDIO_I2S', 'PICO_DISPLAY'):
        if f'CONFIG_{symbol}=y' not in config:
            raise ValueError('Required existing feature disabled: ' + symbol)
    raw = (directory / 'zephyr.elf').read_bytes()
    with (directory / 'zephyr.elf').open('rb') as source:
        elf = ELFFile(source)
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
        flash = symbols['_flash_used']
        ram = symbols['_image_ram_size']
        capacity = 784 * 1024 if combined else 788 * 1024
        if combined:
            verify_partitions((directory / 'zephyr.dts').read_text())
            for symbol in ('bt_fast_pair_enable', 'bt_fast_pair_adv_manager_enable',
                           'bt_fast_pair_fhn_ring_state_update', 'activation_size_probe'):
                if symbol not in symbols:
                    raise ValueError('SDK size anchor stripped: ' + symbol)
        segments = []
        for seg in elf.iter_segments():
            if seg['p_type'] != 'PT_LOAD' or not seg['p_filesz']:
                continue
            addr, length = seg['p_paddr'], seg['p_filesz']
            # IDT_LIST is linker tooling data at 0xffffxxxx, never flashed.
            if addr >= 0xffff0000:
                continue
            if not 0x27000 <= addr < addr + length <= 0x27000 + capacity:
                raise ValueError('ELF load segment outside application')
            segments.append({'address': hex(addr), 'bytes': length})
    gate(flash, ram, capacity)
    uf2 = (directory / 'zephyr.uf2').read_bytes()
    blocks = verify_uf2(uf2, 0x27000, 0x27000 + capacity)
    return {'flash_bytes': flash, 'static_ram_bytes': ram,
            'application_capacity_bytes': capacity,
            'below_600_KiB_target': flash < TARGET,
            'below_700_KiB_gate': flash < THRESHOLD,
            'elf_sha256': hashlib.sha256(raw).hexdigest(),
            'uf2_sha256': hashlib.sha256(uf2).hexdigest(),
            'uf2_blocks': blocks, 'load_segments': segments,
            'partition_map_verified': combined,
            'runtime_peak_measured': False, 'current_measured': False,
            'network_activation': 'disabled' if combined else 'not compiled'}
