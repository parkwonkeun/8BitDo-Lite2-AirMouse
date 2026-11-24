"""Pinned inputs shared by the Windows installer and firmware source build."""
import hashlib
from pathlib import Path
from urllib.request import Request, urlopen

BASE_URL = 'http://dl.8bitdo.com:8080/firmwareFile/upload/8807004b-61a7-49f4-877f-f42d8f9cba70.dat'
BASE_SHA = '121c1df789577997572373d049bfa245eb1fdae231223b0bb37463b257d3e27f'
UPDATER_URL = 'https://download.8bitdo.com/Tools/FirmwareUpdater/8BitDo_Firmware_Updater_Win.zip'
UPDATER_SHA = '88fd6af7eb62a33546415db75a40907ef9120d14f10c6cd7a4337ea59994a26f'
R58_SHA = 'c776b803e8f95434c7043365ac51bcb9fae03199d6a1eddd47258fde4d5010fa'
R57_SHA = 'cf5377bc9b2257df86cca2b2b9cc7389dac7ac92bc844076c07c03755d6800c4'


def verify(data, expected, label):
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError(f'SHA-256 mismatch: {label}')
    return data


def fetch(cache, name, url, expected):
    path = Path(cache) / name
    if path.is_file():
        return verify(path.read_bytes(), expected, name)
    print(f'Downloading {name} ...', flush=True)
    request = Request(url, headers={'User-Agent': 'Mozilla/5.0', 'Referer': 'https://support.8bitdo.com/'})
    with urlopen(request, timeout=60) as response:
        data = verify(response.read(), expected, name)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return data
