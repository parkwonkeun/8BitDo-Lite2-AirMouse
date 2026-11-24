"""Prepare the pinned r57 image and update one Lite 2 connected in D mode."""
import argparse
from io import BytesIO
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
import time
from zipfile import ZipFile

from release_inputs import BASE_SHA, BASE_URL, R57_SHA, UPDATER_SHA, UPDATER_URL, fetch, verify


def prepare(cache, kit, destination):
    base = fetch(cache, 'Lite2-v1.06-official.dat', BASE_URL, BASE_SHA)
    delta = json.loads((kit / 'r57.delta.json').read_text())
    image = bytearray(base)
    image.extend(bytes(delta['length'] - len(image)))
    for offset, value in delta['runs']:
        change = bytes.fromhex(value)
        image[offset:offset + len(change)] = change
    verify(image, R57_SHA, 'reconstructed r57')
    image_path = destination / 'Lite2AirMouse-r57.dat'
    image_path.write_bytes(image)
    extract_vendor_tools(cache, destination)
    print(f'r57 SHA-256 verified: {R57_SHA}', flush=True)
    return image_path


def extract_vendor_tools(cache, destination):
    archive = fetch(cache, '8BitDo_Firmware_Updater_Win.zip', UPDATER_URL, UPDATER_SHA)
    prefix = '8BitDo_Firmware_Updater_Win/'
    with ZipFile(BytesIO(archive)) as z:
        for item in z.infolist():
            relative = item.filename.removeprefix(prefix)
            if not item.is_dir() and (relative == '8BitDoFirmwareUpdaterTools.dll' or relative.startswith('data/')):
                target = destination / relative
                target.parent.mkdir(exist_ok=True, parents=True)
                target.write_bytes(z.read(item))


def wait_for(paths, identity):
    for _ in range(100):
        if paths(identity):
            return
        time.sleep(0.15)
    raise RuntimeError(f'USB device did not reappear: {identity}')


def flash(image, helper):
    from hid_transport import open_unique, r, transfer
    if r.paths('vid_2dc8&pid_3208'):
        raise RuntimeError('Disconnect other controllers. Start with one Lite 2 in normal D mode.')
    handle = open_unique(0x2dc8, 0x5112)
    try:
        print('One D-mode Lite 2 found. Keep the data cable connected until the check finishes.')
        if input('Type R57 and press Enter to write the firmware: ').strip() != 'R57':
            print('Cancelled.')
            return
        packet = bytes.fromhex('81 05 00 51 00 00 00').ljust(64, b'\0')
        if transfer(handle, packet) != 64:
            raise RuntimeError('Could not send the D-mode boot command')
    finally:
        r.k.CloseHandle(handle)
    wait_for(r.paths, 'vid_2dc8&pid_3208')
    if len(r.paths('vid_2dc8&pid_3208')) != 1:
        raise RuntimeError('Expected exactly one bootloader')
    subprocess.run([str(helper), '--flash', str(image), R57_SHA], cwd=image.parent, check=True)
    wait_for(r.paths, 'vid_2dc8&pid_5112')
    handle = open_unique(0x2dc8, 0x5112)
    try:
        packet = bytes.fromhex('81 05 00 21 01 00 00').ljust(64, b'\0')
        if transfer(handle, packet) != 64 or (transfer(handle) or b'')[:2] != b'\x02\x22':
            raise RuntimeError('Write completed but the runtime check failed')
    finally:
        r.k.CloseHandle(handle)
    print('Update complete. The controller restarted and answered the D-mode USB check.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-only', metavar='DAT', type=Path, help='Export the image without accessing USB')
    parser.add_argument('--cache', type=Path, default=Path(os.environ['LOCALAPPDATA']) / 'Lite2AirMouse' / 'vendor')
    args = parser.parse_args()
    kit = Path(sys.executable if getattr(sys, 'frozen', False) else __file__).resolve().parent
    with TemporaryDirectory(prefix='lite2-r57-') as temporary:
        directory = Path(temporary)
        image = prepare(args.cache, kit, directory)
        if args.prepare_only:
            args.prepare_only.write_bytes(image.read_bytes())
            print(f'Prepared {args.prepare_only.resolve()}')
            return
        helper = directory / 'NativeUpdaterFlash.exe'
        helper.write_bytes((kit / helper.name).read_bytes())
        flash(image, helper)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        sys.exit(f'Update stopped: {error}')
