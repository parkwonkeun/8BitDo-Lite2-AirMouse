"""Build the Cortex-M0 patch only. This script never opens a device."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get('ARM_GCC_BIN', ROOT / 'work/toolchain/xpack-arm-none-eabi-gcc-15.2.1-1.1/bin'))

def run(tool, *args):
    subprocess.run([str(BIN / (tool + '.exe')), *args], cwd=ROOT, check=True)

def main():
    (ROOT / 'build').mkdir(exist_ok=True)
    from package_patch import BT_MOUSE, KEYBOARD, CONSUMER
    original=(ROOT/'outputs/Lite2-v1.06-decrypted-analysis-only.bin').read_bytes()
    sdp=bytearray(original[0x12ee2:0x130ba])
    extra=KEYBOARD+CONSUMER
    descriptor=BT_MOUSE+extra
    # Increase the HID service record and both nested descriptor list lengths.
    assert sdp[13:16] == bytes.fromhex('36 01 50')
    assert sdp[211:219] == bytes.fromhex('35 59 35 57 08 22 25 53')
    growth=len(descriptor)-83
    sdp[14:16]=(0x150+growth).to_bytes(2,'big')
    for i in (212,214,218):
        sdp[i] += growth
    subclass=sdp.index(bytes.fromhex('09 02 02 08 80'))
    sdp[subclass+4]=0xc0  # Mouse and keyboard.
    boot=sdp.index(bytes.fromhex('09 02 0e 28 01'))
    sdp[boot+4]=0  # Report protocol only.
    sdp[219:302]=descriptor
    # The final Device ID record is already in pinned v1.06 flash. Assemble
    # the two records in the stock S service buffer passed at registration.
    device_id=original[0x13042:0x130ba]
    assert sdp.endswith(device_id)
    del sdp[-len(device_id):]
    name=b'Bluetooth Wireless Controller   '
    old=bytes([0x25,len(name)])+name
    name=b'Lite 2'
    assert sdp.count(old)==1
    sdp=sdp.replace(old,bytes([0x25,len(name)])+name,1)
    sdp[14:16]=(len(sdp)-16).to_bytes(2,'big')
    assert len(sdp)+len(device_id)<=0x229
    # Reuse runs from the stock Bluetooth tables and our S USB descriptor.
    # Each instruction is a literal run or a flash offset plus a run length.
    from package_patch import MOUSE
    sources=[(0xa3ea, MOUSE+KEYBOARD+CONSUMER), (0xaee2,original[0x12ee2:0x130ba])]
    record=bytes(sdp)+device_id
    best=[b'']*(len(record)+1)
    for i in reversed(range(len(record))):
        options=[]
        for n in range(1,min(127,len(record)-i)+1):
            chunk=record[i:i+n]
            options.append(bytes([128+n])+chunk+best[i+n])
            for offset,data in sources:
                pos=data.find(chunk)
                if pos>=0:
                    options.append(bytes([n])+(offset+pos).to_bytes(2,'little')+best[i+n])
        best[i]=min(options,key=len)
    (ROOT/'build/mouse_sdp.c').write_text(
        'const unsigned char lite2_sdp_recipe[] = {'+','.join(map(str,best[0]))+'};\n'
        'const unsigned lite2_mouse_sdp_size = '+str(len(record))+';\n')
    run('arm-none-eabi-gcc', '-mcpu=cortex-m0', '-mthumb', '-Oz', '-flto', '-fno-ipa-cp', '-fno-tree-fre', '-fno-tree-sink', '-fno-tree-dominator-opts', '-fno-inline-functions-called-once', 
        *['-Wl,-u,'+name for name in ('memset','mouse_reset','mouse_step','lite2_start','lite2_scan','lite2_scan_delay','lite2_dispatch','lite2_switch_commands','lite2_set_device_class','lite2_scan_pressure','lite2_set_sdp','lite2_mouse_name','lite2_player_lights')], '-std=c11',
        '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-builtin',
        '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '-nostdlib',
        '-Wl,-T,src/patch.ld', '-Wl,-Map,build/mouse.map', 'src/mouse_core.c',
        'src/lite2_v106_adapter.c', 'build/mouse_sdp.c', '-lgcc', '-o', 'build/mouse.elf')
    run('arm-none-eabi-objcopy', '-O', 'binary', 'build/mouse.elf', 'build/mouse.bin')
    run('arm-none-eabi-size', 'build/mouse.elf')

if __name__ == '__main__': main()


