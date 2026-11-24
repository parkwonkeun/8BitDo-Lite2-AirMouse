"""Apply the S BLE link-up light fix to the tested personal r51 image."""
import hashlib
import subprocess
import tempfile
from pathlib import Path
from elftools.elf.elffile import ELFFile
from build_ble_discovery import BASE, ROOT, BIN, decrypt, cryptdo8, thumb_bl

R51 = '1f97666769de83853a74e30dc5cad8c58e7ff5ca96fd1b4f3f0223770ad0ac18'
EVENT_CALL = 0x01019c8e
# Confirmed linker slack after the r51 profile and after its executable code.
REGIONS = {'.led_event': (0x159cc,0x159f8), '.led_apply': (0x15fcc,0x15ff8)}
TARGET = ROOT/'build/Lite2AirMouse-r51.1-ble-led.dat'

def main():
    prior=(ROOT/'build/Lite2AirMouse-r51-ble-controls.dat').read_bytes()
    if hashlib.sha256(prior).hexdigest()!=R51:
        raise ValueError('Expected the verified r51 package')
    image=bytearray(decrypt(prior))
    with tempfile.TemporaryDirectory() as temp:
        tmp=Path(temp)
        pieces=[f'{name} {BASE+start} : {{ *({name}) }}' for name,(start,end) in REGIONS.items()]
        (tmp/'led.ld').write_text('SECTIONS { '+' '.join(pieces)+' /DISCARD/ : { *(.ARM.exidx*) *(.comment) } }')
        subprocess.run([BIN/'arm-none-eabi-gcc.exe','-mcpu=cortex-m0','-mthumb','-Oz','-ffreestanding',
                        '-nostdlib','-Wall','-Werror','-T',tmp/'led.ld',ROOT/'src/ble_connected_led.c',
                        '-o',tmp/'led.elf'],check=True)
        with (tmp/'led.elf').open('rb') as f:
            elf=ELFFile(f)
            for name,(start,end) in REGIONS.items():
                section=elf.get_section_by_name(name)
                code=section.data()
                if section['sh_addr']!=BASE+start or len(code)>end-start or image[start:end]!=b'\xff'*(end-start):
                    raise ValueError(f'Invalid {name} region ({len(code)} bytes)')
                image[start:start+len(code)]=code
            entry=elf.get_section_by_name('.symtab').get_symbol_by_name('ble_led_connected')[0]['st_value']
    offset=EVENT_CALL-BASE
    assert image[offset:offset+4]==thumb_bl(EVENT_CALL,0x0102141c)
    image[offset:offset+4]=thumb_bl(EVENT_CALL,entry)
    dat=cryptdo8.build_section(bytes(image),cryptdo8.CIPHERS['yichip'],106,BASE,len(image),0x5112)
    assert decrypt(dat)==image
    TARGET.write_bytes(dat)
    print(TARGET.name,hashlib.sha256(dat).hexdigest())
if __name__=='__main__': main()
