from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'work/python-libs')]
from build_ble_led import BASE, EVENT_CALL, REGIONS, TARGET, decrypt
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_SP

class BleConnectionLight(unittest.TestCase):
    def test_only_link_up_call_and_reserved_padding_differ(self):
        before=decrypt((ROOT/'build/Lite2AirMouse-r51-ble-controls.dat').read_bytes())
        after=decrypt(TARGET.read_bytes())
        self.assertEqual(len(before),len(after))
        allowable=set(range(EVENT_CALL-BASE,EVENT_CALL-BASE+4))
        for start,end in REGIONS.values():allowable.update(range(start,end))
        self.assertFalse(any(i not in allowable for i,(a,b) in enumerate(zip(before,after)) if a!=b))
    def test_ble_link_switches_to_selected_mode_only(self):
        for mode,table in ((0,0x4683),(0,0x4975),(1,0x4683)):
            for selected in range(3):
                u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
                u.mem_map(BASE,0x20000);u.mem_write(BASE,decrypt(TARGET.read_bytes()))
                u.mem_map(0xc4000,0x1000);u.mem_map(0x20000,0x10000)
                u.mem_write(0xc448e,struct.pack('<H',table))
                u.mem_write(0x20184,bytes([mode]))
                u.mem_write(0x2fe00+34,bytes([selected]))
                calls=[]
                def spy(uc,addr,size,user):
                    calls.append((addr,uc.reg_read(UC_ARM_REG_R0)))
                    uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
                for a in (0x0102141c,0x01022ae0,0x0102c288):u.hook_add(UC_HOOK_CODE,spy,begin=a,end=a)
                u.reg_write(UC_ARM_REG_R0,0x1019cc4);u.reg_write(UC_ARM_REG_SP,0x2f900)
                u.emu_start(EVENT_CALL|1,EVENT_CALL+4,count=200)
                self.assertEqual(calls[0],(0x0102141c,0x1019cc4))
                self.assertEqual([c[0] for c in calls[1:]], [0x01022ae0,0x0102c288] if mode==0 and table==0x4683 else [])
                if len(calls)>1:self.assertEqual(calls[-1][1],selected)
