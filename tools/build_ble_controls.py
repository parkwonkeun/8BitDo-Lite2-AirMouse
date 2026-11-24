"""Build Lite 2 BLE HID profiles from the pinned r50 with public or device-specific calibration."""
import argparse
import hashlib
import struct
import subprocess
import tempfile
import uuid
from pathlib import Path
from elftools.elf.elffile import ELFFile
from build_ble_discovery import BASE, OFF, PUBLIC, PERSONAL, ROOT, BIN, HOOKS, decrypt, cryptdo8, thumb_bl

# Each collection has its own Report characteristic (HOGP strips the report ID).
MOUSE = bytes.fromhex(
    '05 01 09 02 a1 01 85 01 09 01 a1 00 05 09 19 01 29 02 15 00 25 01 '
    '75 01 95 02 81 02 95 06 81 03 05 01 09 30 09 31 16 01 80 26 ff 7f '
    '75 10 95 02 81 06 09 38 15 81 25 7f 75 08 95 01 81 06 c0 c0')
KEYS = bytes.fromhex(
    '05 01 09 06 a1 01 85 04 05 07 09 28 19 4f 29 52 15 00 25 01 '
    '75 01 95 05 81 02 95 03 81 03 c0')
MEDIA = bytes.fromhex(
    '05 0c 09 01 a1 01 85 05 0a 24 02 0a 23 02 09 ea 09 e9 '
    '15 00 25 01 75 01 95 04 81 02 95 04 81 03 c0')

GAMEPAD = bytes.fromhex(
    '05 01 09 05 a1 01 85 01 05 09 19 01 29 10 15 00 25 01 75 01 95 10 81 02 '
    '75 04 95 01 81 03 05 01 a4 09 39 25 07 46 3b 01 65 14 81 42 b4 '
    '09 30 09 31 09 32 09 35 26 ff 00 75 08 95 04 81 02 '
    '05 02 09 c4 09 c5 95 02 81 02 c0')

def database(monitor=False,sticks=False,gamepad=False,dual=False):
    rows = []
    def attr(uuid, value):
        rows.append(struct.pack('<HBHB',len(rows)+1,2,uuid,len(value))+value)
        return len(rows)
    def char(uuid, props, value):
        attr(0x2803, bytes([props]))
        return attr(uuid,value)
    def service(uuid):
        attr(0x2800,struct.pack('<H',uuid))
    service(0x1801)
    char(0x2b3a,2,b'\0')
    service(0x1800)
    name=(b'Lite2 GamePAD' if gamepad else b'Lite2 Air Mouse') if dual else (b'Lite2 Pad' if gamepad else b'Lite2 BLE')
    char(0x2a00,2,name)
    char(0x2a01,2,bytes([0xc4 if gamepad else 0xc2,3]))
    char(0x2a04,2,struct.pack('<4H',6,9,0,300))
    service(0x1812)
    char(0x2a4a,2,b'\x11\x01\0\x02')
    char(0x2a4c,4,b'\0')
    mouse=MOUSE
    if monitor:
        # One positive Dial value carries the complete switch bitmap, including release.
        mouse=MOUSE[:-2]+bytes.fromhex('09 37 15 00 27 00 00 10 00 75 20 81 06')+MOUSE[-2:]
    char(0x2a4b,2,GAMEPAD if gamepad else mouse+KEYS+MEDIA)
    handles=[]
    for report_id,width in (((1,9),) if gamepad else ((1,10 if monitor else 6),(4,1),(5,1))):
        handles.append(char(0x2a4d,0x12,bytes(width)))
        attr(0x2902,bytes(2))
        attr(0x2908,bytes([report_id,1]))
    if sticks:
        # A separate vendor service keeps the HID report map and notification
        # rate intact. The radio's ATT rows store UUIDs least significant byte first.
        service_uuid=uuid.UUID("ee145e00-f84b-45df-834e-df1163140054").bytes[::-1]
        sample_uuid=uuid.UUID("ee145e01-f84b-45df-834e-df1163140054").bytes[::-1]
        attr(0x2800,service_uuid)
        attr(0x2803,bytes([2]))
        rows.append(struct.pack("<HB",len(rows)+1,16)+sample_uuid+bytes([20])+bytes(20))
    return b''.join(rows)+bytes(2),handles

def main(reconnect=False,monitor=False,sticks=False,gamepad=False,separate_id=False,home_power=False,public=False):
    separate_id=separate_id or home_power
    gamepad=gamepad or separate_id
    sticks=sticks or gamepad
    monitor=monitor or sticks
    reconnect=reconnect or monitor
    folder=ROOT/'build'
    base=(folder/'Lite2AirMouse-v106-s-mode-r50.bin').read_bytes()
    if public:
        assert hashlib.sha256(base).hexdigest()==PUBLIC
        assert base[0x15714:0x1571c]==bytes([128])*8 and base[-8:]==bytes([255])*8
        image=bytearray(base)
    else:
        old=(folder/'Lite2AirMouse-r50-calibrated.dat').read_bytes()
        assert hashlib.sha256(base).hexdigest()==PUBLIC and hashlib.sha256(old).hexdigest()==PERSONAL
        image=bytearray(decrypt(old))
    assert image[:0x15714]==base[:0x15714] and image[0x1571c:-8]==base[0x1571c:-8]
    stamp=image[-8:]
    # These stock loader records give the two table pointers and the exclusively
    # borrowed SDP allocation. No assumption about the adjacent radio RAM.
    for sequence in ('aa 55 02 00 8e 44 75 49','aa 55 02 00 47 45 83 46','aa 55 5a 02 83 46'):
        assert bytes.fromhex(sequence) in base
    table,handles=database(monitor,sticks,dual=separate_id)
    assert len(table)<=602 and len(table)<480
    with tempfile.TemporaryDirectory() as d:
        tmp=Path(d)
        header='static const unsigned char db[] __attribute__((section(".profile")))={'+','.join(map(str,table))+'};'
        header+='\nenum { MOUSE_HANDLE=%d, KEY_HANDLE=%d, MEDIA_HANDLE=%d };'%tuple(handles)
        if monitor:
            name=b'Lite2 Air Mouse' if separate_id else b'Lite2 BLE'
            ad=bytes.fromhex('03 02 01 05 03 19 c2 03 03 03 12 18')+bytes([len(name)+1,9])+name
            scan=bytes([31,len(name)+1,9])+name
            profile=table+ad.ljust(28,b'\0')+scan.ljust(17,b'\0')+bytes([len(name)])+name
            header=header[header.index('enum {'):]+'\nenum { DB_SIZE=%d };'%len(table)
            if sticks:
                # This superseded USB report map has no remaining pointer; the
                # actual extended S map is served by the v1.06 control handler.
                footprint=base[0x123ea:0x123ea+203]
                assert footprint.startswith(MOUSE[:14])
                assert not any(0x0102a3ea<=v[0]<0x0102a3ea+203 for v in
                               (struct.unpack_from("<I",base,i) for i in range(len(base)-3)))
                assert struct.unpack_from("<I",base,0xc220)[0]==0x0102d736
                header+='\n#define SAMPLE_ADDRESS '+hex(0xc4683+len(table)-22)
                header+='\nstatic const unsigned char profile_prefix[] __attribute__((section(".former_usb")))={'+','.join(map(str,profile[:200]))+'};'
                profile=profile[200:]
            header+='\nstatic const unsigned char radio_profile[] __attribute__((section(".profile")))={'+','.join(map(str,profile))+'};'
        if gamepad:
            pad,pad_handles=database(gamepad=True,dual=separate_id)
            assert pad_handles==handles[:1]
            offset=pad.index(GAMEPAD)-1
            pad_name=b'Lite2 GamePAD' if separate_id else b'Lite2 Pad'
            name_offset=pad.index(pad_name)+6
            if separate_id: offset=pad.index(pad_name)-1
            appearance_offset=pad.index(bytes.fromhex('012a02c403'))+3
            header+=f'\nenum {{ PAD_TAIL={offset}, PAD_NAME={name_offset}, PAD_APPEARANCE={appearance_offset} }};'
            header+='\nstatic const unsigned char pad_tail[] __attribute__((section(".pad_tail")))={'+','.join(map(str,pad[offset:]))+'};'
        (tmp/'ble_controls_profile.h').write_text(header)
        first=BASE+OFF
        early_led='*(.led_event)' if reconnect else ''
        extra_text='*(.led_apply)' if reconnect else ''
        extras=['-DBLE_RECONNECT',ROOT/'src/ble_connected_led.c'] if reconnect else []
        if monitor:
            # r50's retired Switch spans; use them for LED arbitration, as in r52.1.
            # The two transport tables are placed together so their combined slack
            # can be used by the larger BLE report without growing the flash bank.
            script=f'SECTIONS {{ .event {0x01027be0} : {{ *(.event) }} .led {0x01027fd0} : {{ *(.monitor_led) }} . = {first}; .profile : {{ *(.profile) *(.earlydata) *(.earlytext) }} . = ALIGN(4); .text : {{ *(.entry) *(.text*) *(.rodata*) }} /DISCARD/ : {{ *(.ARM.exidx*) *(.comment) }} }}'
            externals=dict(r52_link_log=0x0102141d,r52_timer=0x0102ca8d,r52_lights=0x0102ca3d,mode_led=0x0102c289,usb_active=0x01020a81)
            script=''.join(f'{k} = {v};' for k,v in externals.items())+script
            extras=['-flto','-fno-inline-functions-called-once','-DBLE_RECONNECT','-DBLE_MONITOR',*[f'-Wl,-u,{name}' for name in ('ble_visibility','ble_saved_host','ble_start','ble_poll','led_link','led_timer','led_write')],ROOT/'src/ble_mode_lights.c']
        else:
            script=f'SECTIONS {{ . = {first}; .profile : {{ *(.profile) *(.earlydata) *(.earlytext) {early_led} }} . = {BASE+len(base)}; .text : {{ *(.entry) *(.text*) *(.rodata*) {extra_text} }} /DISCARD/ : {{ *(.ARM.exidx*) *(.comment) }} }}'
        if sticks:extras+=['-DBLE_STICKS','-finline-functions-called-once']
        if sticks:script=script.replace('SECTIONS {','SECTIONS { .former_usb 0x0102a3ea : { *(.former_usb) }')
        if gamepad:
            extras+=['-DBLE_GAMEPAD']
            script=script.replace('SECTIONS {','SECTIONS { .pad_code 0x0102c868 : { *(.pad_code) *(.pad_tail) } .pad_extra 0x0102d6b4 : { *(.pad_extra) }')
        if separate_id:
            extras+=['-DBLE_DUAL','-Wl,-u,ble_switch_off','-Wl,-u,ble_link_down',ROOT/'src/ble_mode_address.S']
            script=script.replace('*(.pad_code) *(.pad_tail)','*(.pad_code) *(.pad_label)').replace('SECTIONS {','SECTIONS { .dual_more 0x01028080 : { *(.dual_more) } .dual_tail 0x01027c04 : { *(.pad_tail) } .dual_code 0x010277fc : { *(.dual_code) }')
            script='read_buttons = 0x01021b65;'+script
            script=script.replace('SECTIONS {','SECTIONS { .le_address 0x0101adf4 : { *(.le_address) }')
        (tmp/'profile.ld').write_text(script)
        subprocess.run([BIN/'arm-none-eabi-gcc.exe','-mcpu=cortex-m0','-mthumb','-Oz','-ffreestanding','-nostdlib','-Wall','-Werror','-I',tmp,'-T',tmp/'profile.ld','-Wl,--defsym=__aeabi_idiv=0x01018245,--defsym=memset=0x0102c831',ROOT/'src/ble_controls.c',*extras,'-o',tmp/'controls.elf'],check=True)
        with (tmp/'controls.elf').open('rb') as f:
            elf=ELFFile(f)
            if sticks:
                prefix=elf.get_section_by_name('.former_usb')
                assert prefix['sh_addr']==0x0102a3ea and prefix['sh_size']==200
                image[0x123ea:0x123ea+200]=prefix.data()
            data=elf.get_section_by_name('.profile').data()
            assert base[OFF:OFF+len(data)]==b'\xff'*len(data) and OFF+len(data)<=len(base)-8
            image[OFF:OFF+len(data)]=data
            code=elf.get_section_by_name('.text')
            if monitor:
                stock=(ROOT/'outputs/Lite2-v1.06-decrypted-analysis-only.bin').read_bytes()
                if separate_id:
                    for section,left,right in (('.dual_more',0x01028080,0x010280d0),('.dual_tail',0x01027c04,0x01027ce8),('.dual_code',0x010277fc,0x0102783c)):
                        seg=elf.get_section_by_name(section)
                        assert seg['sh_addr']==left and left+seg['sh_size']<=right,(section,seg['sh_size'])
                        assert base[left-BASE:right-BASE]==stock[left-BASE:right-BASE]
                        image[left-BASE:left-BASE+seg['sh_size']]=seg.data()
                    start=0x0101adf4-BASE
                    block=elf.get_section_by_name('.le_address')
                    assert block['sh_addr']==BASE+start and block['sh_size'] in (0x36,0x38)
                    assert base[start:start+0x36]==stock[start:start+0x36]
                    image[start:start+0x36]=block.data()[:0x36]
                if gamepad:
                    # Only the old Classic SDP, class, link and scan-pressure
                    # callbacks owned these ranges. Restore their stock callers.
                    for site,size,retired in ((0x0101df82,22,0x0102c868),(0x0101ae76,4,0x0102c90c),
                                              (0x0101a17c,4,0x0102c92c),(0x01021ec2,4,0x0102c960)):
                        i=site-BASE
                        assert image[i:i+4]==thumb_bl(site,retired)
                        image[i:i+size]=stock[i:i+size]
                    for name,start,end in (('.pad_code',0x0102c868,0x0102c9c4),('.pad_extra',0x0102d6b4,0x0102d6f8)):
                        block=elf.get_section_by_name(name)
                        assert block['sh_addr']==start and start+block['sh_size']<=end,(name,block['sh_size'],end-start)
                        image[start-BASE:start-BASE+block['sh_size']]=block.data()
                for name,start,end in (('.event',0x01027be0,0x01027c00),('.led',0x01027fd0,0x01028080)):
                    block=elf.get_section_by_name(name)
                    assert block['sh_addr']==start and start+block['sh_size']<=end and base[start-BASE:end-BASE]==stock[start-BASE:end-BASE]
                    image[start-BASE:start-BASE+block['sh_size']]=block.data()
                offset=code['sh_addr']-BASE
                assert offset>=OFF+len(data) and base[offset:-8]==b'\xff'*len(base[offset:-8])
                image=image[:offset]
            else: assert code['sh_addr']==BASE+len(base)
            print("Linked poll and support bytes",code["sh_size"],"prefix",len(data))
            image.extend(code.data())
            symbols=elf.get_section_by_name('.symtab')
            def addr(name): return symbols.get_symbol_by_name(name)[0]['st_value']|1
            def replace(at,prior,new):
                i=at-BASE
                assert image[i:i+4]==prior
                image[i:i+4]=new
            if separate_id:
                for at,old_target,new_name in ((0x0101db90,0x0101f664,'ble_switch_off'),(0x0101a3ae,0x0102141c,'ble_link_down')):
                    replace(at,thumb_bl(at,old_target),thumb_bl(at,addr(new_name)))
            for at in HOOKS: replace(at,thumb_bl(at,0x0101ab8c),thumb_bl(at,addr('ble_visibility')))
            if reconnect:
                for at in (0x01019f8a,0x0101b7f6):
                    replace(at,thumb_bl(at,0x01021cb4),thumb_bl(at,addr('ble_saved_host')))
                at=0x01019c8e
                replace(at,thumb_bl(at,0x0102141c),thumb_bl(at,addr('led_link' if monitor else 'ble_led_connected')))
                if monitor:
                    replace(0x01019c96,thumb_bl(0x01019c96,0x0102141c),thumb_bl(0x01019c96,addr('led_link')))
                    for at,previous_value,name in ((0x010211ac,0x0102ca8d,'led_timer'),(0x0102242c,0x0102ca3d,'led_write')):
                        replace(at,struct.pack('<I',previous_value),struct.pack('<I',addr(name)))
            at=0x01026270
            replace(at,thumb_bl(at,0x0102d4cd),thumb_bl(at,addr('ble_poll')))
            for at,before,after in ((0x0101820c,0x0102c84d,addr('ble_start')),(0x01018214,0x2fe00,0x2fb80)):
                replace(at,struct.pack('<I',before),struct.pack('<I',after))
    image.extend(b'\xff'*((-len(image)-8)%512)+stamp)
    if len(image)>0x16000: raise ValueError(f'BLE code exceeds app bank: {len(image):#x}')
    if home_power:
        # The stock HOME scanner passes this millisecond interval to the wireless power handler.
        offset=0x0101f130-BASE
        assert struct.unpack_from('<I',image,offset)[0]==2500
        struct.pack_into('<I',image,offset,2000)
    data=cryptdo8.build_section(bytes(image),cryptdo8.CIPHERS['yichip'],106,BASE,len(image),0x5112)
    assert decrypt(data)==image
    target=folder/('Lite2AirMouse-r57-home-power.dat' if home_power else 'Lite2AirMouse-r56-dual-ble.dat' if separate_id else 'Lite2AirMouse-r55-ble-gamepad.dat' if gamepad else 'Lite2AirMouse-r54-ble-sticks.dat' if sticks else 'Lite2AirMouse-r53-ble-monitor.dat' if monitor else 'Lite2AirMouse-r52-ble-reconnect.dat' if reconnect else 'Lite2AirMouse-r51-ble-controls.dat')
    if public:
        target=target.with_stem(target.stem+"-public")
    target.write_bytes(data)
    print(target.name,hashlib.sha256(data).hexdigest(),len(table),len(image))
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reconnect',action='store_true',help='Build r52 with S-mode cold start and connected LEDs')
    parser.add_argument('--monitor',action='store_true',help='Build r53 with BLE physical button snapshots')
    parser.add_argument("--sticks",action="store_true",help="Build r54 with independent monitor stick axes")
    parser.add_argument("--gamepad",action="store_true",help="Build r55 with D BLE gamepad and S BLE mouse")
    parser.add_argument("--separate-id",action="store_true",help="Build r56 with independent named BLE devices")
    parser.add_argument('--home-power',action='store_true',help='Build dual BLE with a two-second HOME power-off hold')
    parser.add_argument('--public',action='store_true',help='Build with neutral stick centers and no device-specific input files')
    args=parser.parse_args()
    main(args.reconnect,args.monitor,args.sticks,args.gamepad,args.separate_id,args.home_power,args.public)
