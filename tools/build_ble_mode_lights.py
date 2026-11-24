"""Build r52.1 from the exact tested r52, replacing only LED-related paths."""
import hashlib
import struct
import subprocess
import tempfile
from pathlib import Path
from elftools.elf.elffile import ELFFile
from build_ble_discovery import BASE, ROOT, BIN, decrypt, cryptdo8, thumb_bl

R52='b1fe199375c285c4438c61fdbe6bdbf3cffcfce9e95cbf15bee2deba59970a91'
TARGET=ROOT/'build/Lite2AirMouse-r52.1-ble-lights.dat'
START,END=0x01027fd0,0x01028080  # Remaining retired Switch span, see src/patch.ld.
EVENT_START,EVENT_END=0x01027be0,0x01027c00  # Tail of the retired gravity span.
HOOKS={0x0101820c:(0x0102da71,'led_start'), 0x010211ac:(0x0102ca8d,'led_timer'),
       0x0102242c:(0x0102ca3d,'led_write')}
EVENTS={0x01019c8e:0x0102d9cc, 0x01019c96:0x0102141c}

def main():
    prior=(ROOT/'build/Lite2AirMouse-r52-ble-reconnect.dat').read_bytes()
    if hashlib.sha256(prior).hexdigest()!=R52: raise ValueError('Expected r52')
    image=bytearray(decrypt(prior))
    stock=(ROOT/'outputs/Lite2-v1.06-decrypted-analysis-only.bin').read_bytes()
    assert image[START-BASE:END-BASE]==stock[START-BASE:END-BASE]
    assert image[EVENT_START-BASE:EVENT_END-BASE]==stock[EVENT_START-BASE:EVENT_END-BASE]
    with tempfile.TemporaryDirectory() as d:
        tmp=Path(d)
        externals=dict(r52_start=0x0102da71,r52_link_log=0x0102d9cd,r52_lights=0x0102ca3d,
                       r52_timer=0x0102ca8d,mode_led=0x0102c289,usb_active=0x01020a81)
        script='\n'.join(f'{name} = {addr};' for name,addr in externals.items())
        script+=f'\nSECTIONS {{ .event {EVENT_START} : {{ *(.event) }} . = {START}; .text : {{ *(.text*) *(.rodata*) }} /DISCARD/ : {{ *(.ARM.exidx*) *(.comment) }} }}'
        (tmp/'lights.ld').write_text(script)
        subprocess.run([BIN/'arm-none-eabi-gcc.exe','-mcpu=cortex-m0','-mthumb','-Oz','-ffreestanding',
                        '-nostdlib','-Wall','-Werror','-T',tmp/'lights.ld',ROOT/'src/ble_mode_lights.c',
                        '-o',tmp/'lights.elf'],check=True)
        with (tmp/'lights.elf').open('rb') as f:
            elf=ELFFile(f); symbols=elf.get_section_by_name('.symtab')
            def addr(s):return symbols.get_symbol_by_name(s)[0]['st_value']|1
            event=elf.get_section_by_name('.event').data()
            assert len(event)<=EVENT_END-EVENT_START
            image[EVENT_START-BASE:EVENT_START-BASE+len(event)]=event
            code=elf.get_section_by_name('.text').data()
            if len(code)>END-START:raise ValueError(f'LED span too small: {len(code)}')
            image[START-BASE:START-BASE+len(code)]=code
            for a,(old,name) in HOOKS.items():
                assert image[a-BASE:a-BASE+4]==struct.pack('<I',old)
                struct.pack_into('<I',image,a-BASE,addr(name))
            for a,old in EVENTS.items():
                assert image[a-BASE:a-BASE+4]==thumb_bl(a,old)
                image[a-BASE:a-BASE+4]=thumb_bl(a,addr('led_link'))
    package=cryptdo8.build_section(bytes(image),cryptdo8.CIPHERS['yichip'],106,BASE,len(image),0x5112)
    assert decrypt(package)==image
    TARGET.write_bytes(package)
    print(TARGET.name,hashlib.sha256(package).hexdigest(),len(code))
if __name__=='__main__':main()
