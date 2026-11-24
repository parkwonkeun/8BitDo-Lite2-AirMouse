"""Package a built r33 image, companion APK and ARM64 bridge on Windows."""
from hashlib import sha256
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from zipfile import ZipFile, ZIP_DEFLATED

from release_inputs import BASE_SHA, R33_SHA, verify

root = Path(__file__).resolve().parents[1]
work = root / 'work/public-release-r33'
assets = work / 'assets'
dist = work / 'dist'
assets.mkdir(parents=True, exist_ok=True)
dist.mkdir(parents=True, exist_ok=True)
base = verify((root / 'outputs/Lite2-v1.06-official.dat').read_bytes(), BASE_SHA, 'base')
image = verify((root / 'build/Lite2AirMouse-v106-s-mode-r33.dat').read_bytes(), R33_SHA, 'r33')
runs = []
start = None
for i, byte in enumerate(image):
    changed = i >= len(base) or byte != base[i]
    if changed and start is None:
        start = i
    if not changed and start is not None:
        runs.append((start, image[start:i].hex()))
        start = None
if start is not None:
    runs.append((start, image[start:].hex()))
delta = work / 'r33.delta.json'
delta.write_text(json.dumps({'length': len(image), 'runs': runs}, separators=(',', ':')))
helper = dist / 'NativeUpdaterFlash.exe'
csc = Path(os.environ['WINDIR']) / 'Microsoft.NET/Framework/v4.0.30319/csc.exe'
subprocess.run([str(csc), '/nologo', '/platform:x86', '/optimize+', f'/out:{helper}', str(root / 'tools/NativeUpdaterFlash.cs')], cwd=root, check=True)
subprocess.run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--onefile', '--name', 'Lite2Updater', '--distpath', str(dist),
                '--specpath', str(work), '--workpath', str(work / 'pyinstaller'), 'tools/windows_updater.py'], cwd=root, check=True)
apk = assets / 'Lite2Control-0.3.15.apk'
shutil.copyfile(root / 'android/build/Lite2Control-debug.apk', apk)
windows = assets / 'Lite2AirMouse-r33-Windows-x64.zip'
android = assets / 'Lite2Control-0.3.15-Android-arm64.zip'
with ZipFile(windows, 'w', ZIP_DEFLATED) as z:
    for file in (dist / 'Lite2Updater.exe', helper, delta, root / 'release/Update.cmd', root / 'README.md'):
        z.write(file, file.name)
with ZipFile(android, 'w', ZIP_DEFLATED) as z:
    for file in (apk, root / 'work/android-bridge/lite2-pointer-bridge', root / 'android/bridge/start-y700.ps1', root / 'release/Start-Android.cmd', root / 'README.md'):
        z.write(file, file.name)
    z.write(root/'docs/SLOT-CALIBRATION.md', 'docs/SLOT-CALIBRATION.md')
(assets / 'SHA256SUMS.txt').write_text(''.join(f'{sha256(f.read_bytes()).hexdigest()}  {f.name}\n' for f in (windows, android, apk)), encoding='ascii')
