"""Build a separate S pairing / service-discovery experiment from pinned r50."""
import hashlib
import json
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

from build import BIN
from package_patch import BASE, ROOT, cryptdo8, thumb_bl

OFF = 0x15820
PUBLIC = "32003d10c775a24a73b443f8558c9cff2b8075302e43c380e2b895d3f0b27efa"
PERSONAL = "88bde782aa572581f6e4b502b46afc7b388d859cb780db489740f0b63fec2fd5"
HOOKS = (0x0101c662, 0x0102667e)
NAME = b"Lite2 BLE"
XY = bytes.fromhex("05 01 09 02 a1 01 85 01 09 01 a1 00 09 30 09 31 "
                   "16 01 80 26 ff 7f 75 10 95 02 81 06 c0 c0")
# Button 1 occupies one byte. The diagnostic never synthesizes clicks.
BUTTON_XY = bytes.fromhex("05 01 09 02 a1 01 85 01 05 09 09 01 25 01 75 08 95 01 81 02 "
                         "05 01 09 30 09 31 16 01 80 26 ff 7f 75 10 95 02 81 06 c0")


def profile(mouse=False, pointer=False):
    rows = []

    def attr(uuid, value):
        rows.append(struct.pack("<HBHB", len(rows)+1, 2, uuid, len(value))+value)

    def service(uuid):
        attr(0x2800, struct.pack("<H", uuid))

    def characteristic(uuid, properties, value):
        attr(0x2803, bytes([properties]))
        attr(uuid, value)

    service(0x1801)
    if pointer:
        characteristic(0x2b3a, 2, bytes(1))  # Server Supported Features, static DB.
    else:
        characteristic(0x2a05, 0x20, bytes(4))
        attr(0x2902, bytes(2))
    service(0x1800)
    characteristic(0x2a00, 2, b"Lite2" if mouse else NAME)
    characteristic(0x2a01, 2, b"\xc2\x03")
    service(0x1812)
    characteristic(0x2a4a, 2, b"\x11\x01\x00\x02")
    characteristic(0x2a4c, 4, bytes(1))
    characteristic(0x2a4b, 2, BUTTON_XY if mouse else XY)
    characteristic(0x2a4d, 0x12, bytes(5 if mouse else 4))
    attr(0x2902, bytes(2))
    attr(0x2908, b"\x01\x01")
    if pointer:
        attr(0x2901,b"000000")  # Read-only source and transmission counters, hex.
    table = b"".join(rows)+bytes(2)
    if len(table)>202:
        raise ValueError("GATT table exceeds stock allocation")
    # First stock advertising byte is a radio control field. Scan response
    # length is fixed at 31 by stock; clear unused AD structures explicitly.
    ad = bytes.fromhex("03 02 01 05 03 19 c2 03 03 03 12 18")
    label = bytes([len(NAME)+1, 9])+NAME
    return dict(hid_attributes=table.ljust(202, b"\0"),
                advertisement=(ad+label).ljust(28,b"\0"),
                scan_response=(b"\x1f"+label).ljust(17,b"\0"),
                gap_name=bytes([len(NAME)])+NAME)


def decrypt(data):
    section, = cryptdo8.parse_sections(data)
    return cryptdo8.decrypt_section(data, section, cryptdo8.CIPHERS["yichip"])


def build(mouse=False, pointer=False):
    folder = ROOT/"build"
    public = (folder/"Lite2AirMouse-v106-s-mode-r50.bin").read_bytes()
    if hashlib.sha256(public).hexdigest()!=PUBLIC:
        raise ValueError("Unexpected public base")
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        header = "".join("static const uint8_t "+k+"[]={"+",".join(map(str,v))+"};\n"
                         for k,v in profile(mouse,pointer).items())
        (tmp/"ble_discovery_profile.h").write_text(header)
        (tmp/"ble.ld").write_text(f"SECTIONS {{ . = {BASE+OFF}; .text : {{ *(.entry) *(.text*) *(.rodata*) }} /DISCARD/ : {{ *(.ARM.exidx*) *(.comment) }} }}")
        subprocess.run([BIN/"arm-none-eabi-gcc.exe","-mcpu=cortex-m0","-mthumb","-Os",
                        "-ffreestanding","-nostdlib","-Werror","-Wall","-I",tmp,
                        "-T",tmp/"ble.ld",ROOT/"src/ble_discovery.c","-o",tmp/"ble.elf"],check=True)
        subprocess.run([BIN/"arm-none-eabi-objcopy.exe","-O","binary",tmp/"ble.elf",tmp/"ble.bin"],check=True)
        code = (tmp/"ble.bin").read_bytes()
    # Preserve both the existing load region and the calibration stamp.
    if OFF+len(code)>len(public)-8 or public[OFF:OFF+len(code)]!=b"\xff"*len(code):
        raise ValueError(f"Experimental code ({len(code)} bytes) exceeds reserved padding")
    patch = bytearray(public)
    patch[OFF:OFF+len(code)] = code
    for address in HOOKS:
        offset = address-BASE
        if patch[offset:offset+4] != thumb_bl(address,0x0101ab8c):
            raise ValueError("Visibility call mismatch")
        patch[offset:offset+4] = thumb_bl(address,BASE+OFF)
    return bytes(patch)


def motion_image(image, mouse=False, pointer=False):
    address = BASE+len(image)
    with tempfile.TemporaryDirectory() as temp:
        tmp=Path(temp)
        # The original run-loop call and reset vector are patched by symbol.
        (tmp/"motion.ld").write_text(f"SECTIONS {{ . = {address}; .text : {{ *(.entry) *(.text*) *(.rodata*) }} . = ALIGN(4); .start : {{ *(.start) }} /DISCARD/ : {{ *(.ARM.exidx*) *(.comment) }} }}")
        source=ROOT/"src/ble_xy_probe.c"
        flags=[]
        if pointer:
            source=ROOT/"src/ble_pointer.c"
            table=profile(True,True)["hid_attributes"]
            report=table.index(struct.pack("<HBHB",17,2,0x2a4d,5))+6
            status=table.index(struct.pack("<HBHB",20,2,0x2901,6))+6
            flags=[f"-DREPORT_ADDR={0xc4975+report}",f"-DSTATUS_ADDR={0xc4975+status}",
                   "-Wl,--defsym=__aeabi_idiv=0x01018245,--defsym=memset=0x0102c831"]
        subprocess.run([BIN/"arm-none-eabi-gcc.exe","-mcpu=cortex-m0","-mthumb","-Os","-ffreestanding","-nostdlib",
                        "-Wall","-Werror",*flags,f"-DCURSOR_PROBE={int(mouse)}","-T",tmp/"motion.ld",source,"-o",tmp/"xy.elf"],check=True)
        from elftools.elf.elffile import ELFFile
        with (tmp/"xy.elf").open("rb") as f:
            sym=ELFFile(f).get_section_by_name(".symtab")
            start=sym.get_symbol_by_name("ble_xy_start")[0]["st_value"]|1
            poll=sym.get_symbol_by_name("ble_xy_poll")[0]["st_value"]|1
        subprocess.run([BIN/"arm-none-eabi-objcopy.exe","-O","binary",tmp/"xy.elf",tmp/"xy.bin"],check=True)
        image=bytearray(image)+(tmp/"xy.bin").read_bytes()
    for addr,old,new in ((0x0101820c,struct.pack("<I",0x0102c84d),struct.pack("<I",start)),
                         (0x01018214,struct.pack("<I",0x2fe00),struct.pack("<I",0x2fd80 if pointer else 0x2fdf8)),
                         (0x01026270,thumb_bl(0x01026270,0x0102d4cd),thumb_bl(0x01026270,poll))):
        if image[addr-BASE:addr-BASE+4]!=old:
            raise ValueError("Unexpected r50 entry")
        image[addr-BASE:addr-BASE+4]=new
    image.extend(b"\xff"*((-len(image)-8)%512+8))
    if len(image)>0x16000: raise ValueError("Application bank exceeded")
    return image


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xy",action="store_true",help="build the opt-in movement transport test")
    parser.add_argument("--mouse",action="store_true",help="include pointer buttons and radio progress lights")
    parser.add_argument("--pointer",action="store_true",help="build app-free stick and gravity-referenced gyro pointer")
    args=parser.parse_args()
    image = bytearray(build(args.mouse or args.pointer,args.pointer))
    folder = ROOT/"build"
    prior = (folder/"Lite2AirMouse-r50-calibrated.dat").read_bytes()
    if hashlib.sha256(prior).hexdigest()!=PERSONAL:
        raise ValueError("Expected the r50 package recorded in the device flash log")
    previous = decrypt(prior)
    public = (folder/"Lite2AirMouse-v106-s-mode-r50.bin").read_bytes()
    # Transfer only the two authenticated personalization regions.
    manifest = json.loads((folder/"Lite2AirMouse-v106-s-mode-r50.json").read_text())
    center = int(manifest["symbols"]["lite2_stick_centers"],16)-BASE
    if len(previous)!=len(image) or previous[:center]!=public[:center] or previous[center+8:-8]!=public[center+8:-8]:
        raise ValueError("Unexpected personalized r50")
    image[center:center+8] = previous[center:center+8]
    if args.xy or args.mouse or args.pointer:
        image=motion_image(image,args.mouse,args.pointer)
    image[-8:] = previous[-8:]
    data = cryptdo8.build_section(bytes(image),cryptdo8.CIPHERS["yichip"],106,BASE,len(image),0x5112)
    if decrypt(data)!=image:
        raise ValueError("Roundtrip verification failed")
    variant="pointer" if args.pointer else "mouse" if args.mouse else "xy" if args.xy else "discovery"
    target = folder/f"Lite2AirMouse-r50-ble-{variant}.dat"
    target.write_bytes(data)
    print(target.name,hashlib.sha256(data).hexdigest())


if __name__=="__main__":
    main()
