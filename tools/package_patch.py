"""Construct an EXPERIMENTAL image; no USB, Bluetooth, or flashing operations.

Only the exact recorded official v1.06 and decrypted image are accepted.
The result is a development candidate, not a hardware-validated release.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'outputs'),str(ROOT/'work/python-libs')]
import cryptdo8
from elftools.elf.elffile import ELFFile

BASE=0x01018000
PLAIN_SHA='a40f6d88eeddad3536f2a70a96e6a1ff49268c022d9d44683ec9d8d1cdc2bb26'
DAT_SHA='121c1df789577997572373d049bfa245eb1fdae231223b0bb37463b257d3e27f'
MOUSE=bytes.fromhex('''
05 01 09 02 a1 01 85 01
09 01 a1 00 05 09 19 01 29 03 25 01
75 01 95 03 81 02 75 05 95 01 81 03
05 01 09 30 09 31 16 01 80 26 ff 7f 75 10 95 02 81 06
09 38 15 81 25 7f 75 08 95 01 81 06
09 33 09 34 09 35 09 32 09 37 16 00 80 26 ff 7f 75 10 95 05 81 06 c0 c0''')
# Report 1 retains its legacy prefix for hosts with a cached descriptor.
# New Bluetooth hosts use the fine wheel byte appended after the telemetry.
BT_WHEEL_DIVISOR=60
_wheel=bytes.fromhex('09 38 15 81 25 7f 75 08 95 01 81 06')
BT_MOUSE=(MOUSE.replace(_wheel,bytes.fromhex('75 08 95 01 81 03'))[:-2]+
    bytes.fromhex('09 38 a1 02 a4 85 06 09 48 15 00 25 01 35')+
    bytes([BT_WHEEL_DIVISOR,0x45,BT_WHEEL_DIVISOR])+
    bytes.fromhex('75 08 95 01 b1 02 b4')+_wheel+bytes.fromhex('c0 c0 c0'))
# Standard 8-byte keyboard input and a separate Consumer Control collection.
KEYBOARD=bytes.fromhex(
    '05 01 09 06 a1 01 85 04 05 07 15 00 25 01 19 e0 29 e7 '
    '75 01 95 08 81 02 75 08 95 01 81 03 '
    '15 00 25 65 19 00 29 65 95 06 81 00 c0')
CONSUMER=bytes.fromhex(
    '05 0c 09 01 a1 01 85 05 15 00 25 01 75 01 95 02 '
    '0a 24 02 0a 23 02 81 02 95 06 81 03 c0')
USB_VENDOR=bytes.fromhex(
    '06 00 ff 09 01 a1 01 15 00 26 ff 00 75 08 95 3f '
    '85 02 09 02 81 03 85 81 09 03 91 83 c0')

def sha(data): return hashlib.sha256(data).hexdigest()

def thumb_bl(source,target):
    delta=(target&~1)-(source+4)
    if delta&1 or not -(1<<22)<=delta<(1<<22): raise ValueError('BL outside Thumb-1 range')
    return struct.pack('<HH',0xf000|((delta>>12)&0x7ff),0xf800|((delta>>1)&0x7ff))

def main():
    original=(ROOT/'outputs/Lite2-v1.06-decrypted-analysis-only.bin').read_bytes()
    official=(ROOT/'outputs/Lite2-v1.06-official.dat').read_bytes()
    if sha(original)!=PLAIN_SHA or sha(official)!=DAT_SHA: raise ValueError('Unexpected base image')
    if len(MOUSE)!=86: raise ValueError(f'HID descriptor length {len(MOUSE)}')
    with (ROOT/'build/mouse.elf').open('rb') as f:
        elf=ELFFile(f)
        symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    addon=(ROOT/'build/mouse.bin').read_bytes()
    image=bytearray(original);changes=[]
    def replace(address,old,new,reason):
        off=address-BASE
        if len(old)!=len(new) or image[off:off+len(old)]!=old: raise ValueError(f'Patch mismatch {address:x}')
        image[off:off+len(old)]=new
        changes.append(dict(address=f'0x{address:08x}',before=old.hex(),after=new.hex(),reason=reason))

    replace(0x0101820c,struct.pack('<I',0x010271e5),struct.pack('<I',symbols['lite2_start']|1),'Initialize private mouse state before stock main')
    replace(0x01018214,struct.pack('<I',0x2ff00),struct.pack('<I',0x2fe00),'Reserve 256 bytes above new initial stack; ROM ownership not yet hardware verified')
    replace(0x0101cfe8,bytes.fromhex('06 f0 b6 fc'),thumb_bl(0x0101cfe8,symbols['lite2_scan']),'S mode periodic report gate, preserving original scan helper')
    # The caller has already branched away for every mode except S.
    # Keep its supported 5 ms timer unit while connected, regardless of
    # the longer negotiated radio interval. Use the original setter.
    replace(0x0101a0c4,bytes.fromhex('a3 48 00 88'),bytes.fromhex('05 20 03 e0'),
            'Use a 5 ms S-mode scheduler tick on the Bluetooth interval event')
    replace(0x0101df62,bytes.fromhex('a5 48'),bytes.fromhex('0a e0'),
            'Process every 5 ms S Bluetooth scan instead of skipping alternate scans')
    # Route 0 follows stock housekeeping, 2 enters the stock fresh-input path.
    # Other wireless modes retain the original early return with r0 = 0.
    replace(0x0101df82,bytes.fromhex('02 f0 23 fb af 21 89 00 88 42 05 da 99 48 00 78 02 28 01 d1 00 20 be e7'),
            thumb_bl(0x0101df82,symbols['lite2_scan_pressure'])+
            bytes.fromhex('00 28 07 d0 02 28 6f d0 00 20 c2 e7 00 bf 00 bf 00 bf 00 bf'),
            'Sample S wireless movement under low radio capacity while deferring transmission')
    for address in (0x0101d02a,0x0101d034):
        replace(address,thumb_bl(address,0x0102644c),thumb_bl(address,symbols['lite2_scan_delay']),
                'Shorten the active S wireless stick scan interval')
    replace(0x0101e198,thumb_bl(0x0101e198,0x01020d38),thumb_bl(0x0101e198,symbols['lite2_dispatch']),'Dispatch S as mouse; call unchanged original report function for D and other modes')
    replace(0x0101dfd4,thumb_bl(0x0101dfd4,0x010277fc),thumb_bl(0x0101dfd4,symbols['lite2_switch_commands']),'S mouse does not wait for Nintendo host handshakes')
    replace(0x0101ae76,thumb_bl(0x0101ae76,0x0101a9c0),
            thumb_bl(0x0101ae76,symbols['lite2_set_device_class']),
            'Advertise both keyboard and pointing capabilities in S Bluetooth mode')
    replace(0x01021ec2,thumb_bl(0x01021ec2,0x0101a6fc),thumb_bl(0x01021ec2,symbols['lite2_set_sdp']),'S-only SDP call uses appended Classic mouse service record')
    replace(0x01021ed0,bytes.fromhex('60 e0'),bytes.fromhex('44 e0'),
            'Keep the r28 S role-switch feature and disable local Sniff')
    replace(0x01021fbc,struct.pack('<I',0x0102abc9),struct.pack('<I',symbols['lite2_mouse_name']),'S Bluetooth name Lite2 Air Mouse Plus; D name unchanged')
    replace(0x01022428,bytes.fromhex('70 b5 04 46 0d 46 00 2d'),
            bytes.fromhex('00 4b 18 47')+struct.pack('<I',symbols['lite2_player_lights']|1),
            'Route all player light writes through the S-mode LED override')
    usb=original[0x123ea:0x124b5]
    if len(usb)!=203 or usb[:10]!=bytes.fromhex('05 01 15 00 09 04 a1 01 85 30'):raise ValueError('Unexpected S USB descriptor')
    vendor=USB_VENDOR[:-1]+bytes.fromhex('85 03 09 04 75 08 95 3f 81 03 c0')
    descriptor=MOUSE+KEYBOARD+CONSUMER+vendor
    remaining=len(usb)-len(descriptor)
    if remaining<0:raise ValueError('S USB descriptor overflow')
    if remaining%2:
        descriptor+=bytes.fromhex('06 01 00')
        remaining-=3
    descriptor+=bytes.fromhex('05 01')*(remaining//2)
    replace(0x0102a3ea,usb,descriptor,'S USB mouse and diagnostic/vendor collection; D descriptor unchanged')
    image.extend(addon)
    image.extend(b'\xff'*(-len(image)%512))
    image=bytes(image)
    if len(image)>0x15000:raise ValueError('Patch exceeds backed-up application region')
    package=cryptdo8.build_section(image,cryptdo8.CIPHERS['yichip'],106,BASE,len(image),0x5112)
    section=cryptdo8.parse_sections(package)[0]
    if cryptdo8.decrypt_section(package,section,cryptdo8.CIPHERS['yichip'])!=image:
        raise ValueError('Re-encryption roundtrip failed')
    prefix=ROOT/'build/Lite2AirMouse-v106-s-mode-r31'
    prefix.with_suffix('.bin').write_bytes(image)
    prefix.with_suffix('.dat').write_bytes(package)
    manifest=dict(status='EXPERIMENTAL_S_MOUSE_KEYBOARD_D_STOCK',revision=31,subrevision=0,base_plain_sha256=PLAIN_SHA,
        base_dat_sha256=DAT_SHA,plain_sha256=sha(image),dat_sha256=sha(package),
        plain_bytes=len(image),dat_bytes=len(package),addon_bytes=len(addon),
        addon_address='0x0102be00',symbols={k:hex(v) for k,v in symbols.items() if k.startswith('lite2_')},
        changes=changes,roundtrip=True,hardware_written_by_packager=False,
        unresolved=['Bootloader write protocol and image size policy',
                    'Private RAM ownership against resident ROM and stack high-water mark',
                    'S mode sensor sampling, axis direction, timing and calibration on hardware',
                    'Bluetooth pairing, Android HID interpretation, reconnect and power management'],
        api_md5_note='Resolved: official API md5 hashes the decimal file length, not file contents')
    prefix.with_suffix('.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in manifest.items() if k not in ('changes','symbols')},indent=2))

if __name__=='__main__': main()
