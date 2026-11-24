"""Build r58 from an exact r57 image, without accessing hardware."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile

from elftools.elf.elffile import ELFFile
from build_ble_discovery import BASE, BIN, ROOT, cryptdo8, decrypt, thumb_bl
from release_inputs import R57_SHA

PERSONAL_R57 = '43dbc445bce1c2ff30d9f865133635d8bb225999f4ec41333884b45374421a90'
# Constant Nintendo handshake reply templates. Their sole stock consumer
# (0x010277fc) was retired by the S mouse. Keep adjacent live data untouched.
RANGES = (('scan', 0x0102b694, 0x0102b780), ('events', 0x0102b7b4, 0x0102b8d8))
NATIVE = dict(read_buttons=0x01021b65, usb_active=0x01020a81, power_down=0x01026959,
              native_idle=0x0101ef39, previous_start=0x0102c971,
              previous_link=0x01027be1, previous_disconnect=0x0102d6b5)
HOOKS = ((0x0101deac, 'read_buttons', 'idle_scan'),
         (0x0101cffe, 'native_idle', 'idle_native_check'),
         (0x01019c8e, 'previous_link', 'idle_link'),
         (0x01019c96, 'previous_link', 'idle_link'),
         (0x0101a3ae, 'previous_disconnect', 'idle_disconnect'))

def build(public):
    suffix = '-public' if public else ''
    folder = ROOT / 'build'
    original = (folder / f'Lite2AirMouse-r57-home-power{suffix}.dat').read_bytes()
    assert hashlib.sha256(original).hexdigest() == (R57_SHA if public else PERSONAL_R57)
    image = bytearray(decrypt(original))
    stock = (ROOT / 'outputs/Lite2-v1.06-decrypted-analysis-only.bin').read_bytes()
    assert image[0x5fd4:0x5fd8] != thumb_bl(0x0101dfd4, 0x010277fc)
    for _, start, end in RANGES:
        assert image[start-BASE:end-BASE] == stock[start-BASE:end-BASE]
        assert not any(start <= value < end for (value,) in
                       (struct.unpack_from('<I', image, i) for i in range(len(image)-3)))
    script = '\n'.join(f'{k} = {v};' for k, v in NATIVE.items()) + """
SECTIONS {
 .scan 0x0102b694 : { *(.text.idle_scan) *(.text.managed) *(.text.tick_period) *(.text.restart_clock) }
 .events 0x0102b7b4 : { *(.text*) *(.rodata*) }
 /DISCARD/ : { *(.ARM.exidx*) *(.comment) }
}
"""
    with tempfile.TemporaryDirectory() as d:
        temp = Path(d)
        (temp/'idle.ld').write_text(script)
        subprocess.run([BIN/'arm-none-eabi-gcc.exe', '-mcpu=cortex-m0', '-mthumb', '-Oz',
                        '-ffunction-sections', '-ffreestanding', '-nostdlib', '-Wall', '-Werror',
                        '-T', temp/'idle.ld', ROOT/'src/button_idle.c', '-o', temp/'idle.elf'], check=True)
        with (temp/'idle.elf').open('rb') as stream:
            elf = ELFFile(stream)
            for name, start, end in RANGES:
                section = elf.get_section_by_name('.' + name)
                data = section.data()
                assert section['sh_addr'] == start and len(data) <= end-start, (name, len(data))
                image[start-BASE:start-BASE+len(data)] = data
            symbols = elf.get_section_by_name('.symtab')
            def addr(name):
                return symbols.get_symbol_by_name(name)[0]['st_value'] | 1
            for at, previous, name in HOOKS:
                offset = at - BASE
                assert image[offset:offset+4] == thumb_bl(at, NATIVE[previous])
                image[offset:offset+4] = thumb_bl(at, addr(name))
            assert struct.unpack_from('<I', image, 0x20c)[0] == NATIVE['previous_start']
            struct.pack_into('<I', image, 0x20c, addr('idle_start'))
    package = cryptdo8.build_section(bytes(image), cryptdo8.CIPHERS['yichip'], 106, BASE, len(image), 0x5112)
    assert decrypt(package) == image
    target = folder / f'Lite2AirMouse-r58-button-idle{suffix}.dat'
    target.write_bytes(package)
    print(target.name, hashlib.sha256(package).hexdigest())

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--public', action='store_true')
    build(parser.parse_args().public)
