"""Get pinned inputs for the source build. No device access."""
from pathlib import Path
import subprocess
import sys

from release_inputs import BASE_SHA, BASE_URL, fetch, verify

root = Path(__file__).resolve().parents[1]
out = root / 'outputs'
base = 'Lite2-v1.06-official.dat'
fetch(out, base, BASE_URL, BASE_SHA)
fetch(out, 'cryptdo8.py', 'https://raw.githubusercontent.com/aeromodes/8cryptdo/aaffcebafa481631de9c1d722a5693d02234e43a/cryptdo8.py',
      '6701ba2b6089c47686b2785e1c5729e8533ef1102e1f7cdde904bad9b73167bc')
plain = out / 'Lite2-v1.06-decrypted-analysis-only.bin'
subprocess.run([sys.executable, str(out / 'cryptdo8.py'), 'decrypt', str(out / base), '-c', 'yichip', '-o', str(plain)], check=True)
verify(plain.read_bytes(), 'a40f6d88eeddad3536f2a70a96e6a1ff49268c022d9d44683ec9d8d1cdc2bb26', plain.name)
