#!/usr/bin/env python3
"""Build an opt-in profile from an NCS workspace and enforce measured image gates.

Requires west, pyelftools and the NCS 3.4.2 toolchain in the invoking environment.
No flash, provisioning, credential loading or cloud build is performed.
"""
import argparse
import json
import re
from pathlib import Path
import subprocess
import sys

from combined_report import measure


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-root', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--profile', choices=['pico', 'pico-lto', 'combined', 'profile'], default='combined')
    parser.add_argument('--report-only', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    sdk = args.sdk_root.resolve()
    if not args.report_only:
        version_file = sdk / 'nrf' / 'VERSION'
        version = version_file.read_text()
        fields = dict(re.findall(r'^(VERSION_MAJOR|VERSION_MINOR|PATCHLEVEL)\s*=\s*(\d+)', version, re.M))
        if tuple(fields.get(key) for key in ('VERSION_MAJOR', 'VERSION_MINOR', 'PATCHLEVEL')) != ('3', '4', '2'):
            raise ValueError('This measurement profile requires nRF Connect SDK 3.4.2')
        overlays = ['overlays/xiao_ble_audio.overlay', 'overlays/xiao_ble_motion_tuner.overlay']
        configs = ['configs/xiao_ble.conf', 'configs/xiao_audio.conf', 'configs/xiao_tuner.conf', 'configs/xiao_motion.conf']
        if args.profile in ('combined', 'profile'):
            overlays.append('overlays/xiao_find_hub.overlay')
            configs.append('configs/xiao_find_hub.conf')
        if args.profile == 'pico-lto':
            configs.append('configs/xiao_size.conf')
        if args.profile == 'profile':
            configs.append('configs/xiao_profile.conf')
        subprocess.run([sys.executable, '-m', 'west', 'build', '--no-sysbuild', '-p', 'auto',
                        '-b', 'xiao_ble/nrf52840/sense', '-d', str(build), str(root), '--',
                        '-DNCS_TOOLCHAIN_VERSION=NONE',
                        '-DDTC_OVERLAY_FILE=' + ';'.join(overlays),
                        '-DEXTRA_CONF_FILE=' + ';'.join(configs)], cwd=sdk, check=True)
    result = measure(build / 'zephyr', combined=args.profile in ('combined', 'profile'))
    (build / 'pico-resource-report.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
