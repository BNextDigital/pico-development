#!/usr/bin/env python3
"""Build-only by default; explicit external credentials enable guarded UF2 packaging."""
import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from image import make_uf2, registration, verify_config, verify_dts

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nrf-dir', type=Path, required=True, help='SDK 3.4.2 nrf directory')
    parser.add_argument('--build-dir', type=Path, default=HERE/'build-xiao')
    parser.add_argument('--audio', action='store_true', help='Enable existing MAX98357A wiring/profile')
    parser.add_argument('--external-toolchain', action='store_true', help='Use a separately installed Zephyr SDK')
    parser.add_argument('--credentials', type=Path, help='External private JSON; never pass a key on the command line')
    args = parser.parse_args()
    nrf = args.nrf_dir.resolve()
    actual = subprocess.check_output(['git','-C',str(nrf),'rev-parse','HEAD'],text=True).strip()
    pinned = subprocess.check_output(['git','-C',str(nrf),'rev-parse','v3.4.2^{commit}'],text=True).strip()
    if actual != pinned:
        raise ValueError('Use unmodified nRF Connect SDK v3.4.2; sample interfaces are version-pinned')
    if subprocess.check_output(['git','-C',str(nrf),'status','--porcelain','--untracked-files=no'],text=True).strip():
        raise ValueError('SDK tracked files are modified; refusing to build an unaudited sample')
    west = shutil.which('west')
    if not west:
        raise ValueError('Open an nRF Connect SDK 3.4.2 terminal with west available')
    build = args.build_dir.resolve()
    if HERE.is_relative_to(build) or nrf.is_relative_to(build):
        raise ValueError('Build directory cannot contain the application or SDK sources')
    if build.exists() and any(build.iterdir()) and not (build/'CMakeCache.txt').exists():
        raise ValueError('Build directory must be empty or an existing Zephyr build directory')
    record = None
    if args.credentials:
        credentials_path = args.credentials.resolve()
        if credentials_path.is_relative_to(HERE.parents[2]):
            raise ValueError('Credentials must be outside the Pico repository')
        credentials = json.loads(credentials_path.read_text())
        if not isinstance(credentials,dict):
            raise ValueError('Credentials JSON must be an object')
        record = registration(credentials.get('model_id'),credentials.get('anti_spoofing_key'))
    build.mkdir(parents=True,exist_ok=True)
    image_path = build/'pico-find-hub.uf2'
    # Avoid leaving a previous credential-bearing artifact looking like a fresh build.
    if image_path.exists():
        image_path.unlink()
    command = [west,'build','--no-sysbuild','-p','always','-b','xiao_ble/nrf52840/sense',
               '-d',str(build),str(HERE),'--',f'-DPICO_NRF_DIR={nrf}',
               f'-DPICO_FHN_AUDIO={"ON" if args.audio else "OFF"}']
    if args.external_toolchain:
        command.append('-DNCS_TOOLCHAIN_VERSION=NONE')
    subprocess.run(command,cwd=nrf.parent,check=True)
    verify_config(build/'zephyr/.config')
    verify_dts(build/'zephyr/zephyr.dts')
    print('XIAO build verified; application, FHN data and settings use isolated partitions.')
    if record is None:
        print('Build-only: no credentials supplied, no UF2 produced. Network activation is not expected.')
        return
    if not args.audio:
        raise ValueError('Flashable network prototype requires --audio and the wired speaker; visual-only builds remain build-only')
    data = make_uf2((build/'zephyr/zephyr.bin').read_bytes(),record)
    fd = os.open(image_path,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600)
    with os.fdopen(fd,'wb') as output:
        output.write(data)
    print('Created guarded pico-find-hub.uf2. It contains private provisioning data: keep it private.')
    print('No flashing performed. Google model/test-account permissions are still required.')


if __name__ == '__main__':
    try:
        main()
    except (ValueError,OSError,json.JSONDecodeError,subprocess.CalledProcessError) as error:
        # Never print credentials, command-line keys, or a credential-bearing config.
        print(f'Find Hub prototype: {error}',file=sys.stderr)
        sys.exit(1)
